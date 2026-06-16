# PRD: Fast-GPS Send Scheduler

## 1. Executive Summary

**Problem**: A tracker that broadcasts its GPS position on every fix would flood
the LoRa mesh and drain its battery — group airtime is scarce and shared. But
broadcasting too rarely makes live tracking useless (a moving node lags on peers'
maps) and makes a parked node disappear from the mesh entirely. The send cadence
must adapt to how fast the node is actually moving, suppress redundant beacons,
and still emit a low-rate keepalive when stationary so the node stays visible.

A parked node is the hard case: the GPS exposes no Doppler velocity, so movement
is inferred from position, yet a *stationary* fix wanders several metres (multipath)
and can jump tens of metres on atmospheric / satellite-geometry changes. Naively
differencing positions reads that jitter as travel and paints a fake track of a
node that never moved.

**Proposed Solution**: A self-scheduling beacon — `MyMesh::maybeSendFastGpsUpdate()`,
called every `loop()` — that decides on each tick whether to transmit a compact
16-byte position beacon on a dedicated fast-GPS group channel. The decision is
driven by a locally-derived ground speed:

Whether the node is "moving" or "stationary" is decided by a **hysteresis state
machine with a stationary anchor**, not by raw displacement:

- **Moving** → cadence scales with speed (5 s very-fast … 30 s walking), gated by a
  10 m minimum-movement threshold so jitter never triggers a send.
- **Stationary** → a flat **9-minute keepalive** (base == max interval, no growth),
  carrying the *de-jittered anchor* position so a parked tracker stays on peers'
  maps as a stable dot without burning airtime. 9 is an odd, non-round period
  chosen so a fleet of parked nodes doesn't converge on a shared 5/10-minute
  boundary — keepalives roll out of phase instead of colliding on air.

The anchor absorbs jitter: a parked fix stays within a radius of the anchor (which
itself slowly tracks GPS bias), and only displacement *sustained* past that radius
flips the node to moving. A single glitch fix that returns toward the anchor is
ignored, so no fake track is produced.

Receivers republish each beacon to the UI (Map / compass) and apply **geographic
repeat suppression**: a beacon from a sender within 100 m of the receiver's own fix
is flagged do-not-retransmit, since it carries no new neighbourhood information.

**Success Criteria**:
- A moving node's beacon rate tracks its speed (faster when driving, slower when
  walking) and never fires more often than the speed bucket allows.
- A stationary node sends **at most one beacon per 9 minutes**, and never goes
  fully silent.
- GPS jitter while parked — metre-scale wander *and* occasional tens-of-metres
  glitch jumps — produces no movement beacons and no fake track; the node stays a
  single stable dot.
- A genuine departure (sustained travel past the anchor radius) flips the node to
  moving within ~30 s and resumes speed-based cadence.
- A beacon from a nearby peer (≤100 m) is not re-flooded onward.
- The GPS *read* cadence is independent of this scheduler — fixes may update as
  often as the provider allows; only the *send* is gated.

---

## 2. User Experience & Functionality

### Scope

Shared MeshCore companion code (`MyMesh.cpp`), compiled into all boards with
`ENV_INCLUDE_GPS == 1`. No per-board UI; behaviour is automatic once a fast-GPS
channel is configured (Settings → Mesh → fast-GPS channel/region).

### User Personas

- **Team member on the move** — wants their dot to track smoothly on teammates'
  maps without manual action.
- **Parked / base node** — wants to remain visible on the mesh at a trickle rate
  while conserving battery and airtime.
- **Relay neighbour** — should not waste airtime re-flooding beacons from peers
  that are essentially co-located.

### User Stories

**US-1**: As a moving member, I want my beacon cadence to match my speed.
- **AC**: Ground speed ≥ very-fast bucket → 5 s; ≥ fast → 15 s; ≥ walk → 30 s;
  idle → the stationary cadence.
- **AC**: A beacon is sent only after moving > 10 m since the last send AND the
  speed-bucket interval has elapsed.
- **AC**: The first valid fix after start/movement-resume sends promptly.

**US-2**: As a parked node, I want to stay visible without spamming the mesh.
- **AC**: While not moving > 10 m, a keepalive is sent on a flat 9-minute timer.
- **AC**: The beacon's speed byte reports 0 km/h (sub-0.5 km/h rounds to 0).

**US-3**: As a relay, I don't want to re-flood co-located peers' beacons.
- **AC**: On receive, if the sender's position is within 100 m of my own valid fix,
  the packet is marked do-not-retransmit before routing.

**US-4**: As any node, I want received beacons to drive the Map/compass UI.
- **AC**: Each beacon publishes a `PositionUpdate` (pub-key prefix, lat/lon, speed,
  RX timestamp) to the model bridge; the sender's name is resolved from contacts
  when known, else shown as a hex label.

### Non-Goals

- **GPS read/poll cadence** — out of scope; the local GPS may refresh at any rate.
  This document governs only mesh transmission.
- **Per-contact direct position requests / trace** — separate features.
- **Persisting positions** — live positions are RAM-only on the receiver.
- **Exponential / growing stationary backoff** — out of scope; the stationary
  cadence is flat at 9 min, not a backoff that lengthens over time.

---

## 3. Technical Specifications

### Entry point & gating

`MyMesh::maybeSendFastGpsUpdate()` runs from `MyMesh::loop()` after
`updateGpsStatusCache()`. It returns early (and resets share state) unless:
- `_prefs.gps_enabled` and `hasGpsCustomVars()`,
- a fast-GPS channel resolves (`resolveFastGpsChannel`),
- the location provider has a valid fix.

### Ground-speed estimation (`updateGpsStatusCache`)

EMA over successive fixes, used for the reported speed byte; mirrors the UI-core
derivation in `model.cpp`:
- Displacement gate ≥ 5 m and Δt ≥ 1 s → `inst_kmh`; rejected if > 300 km/h
  (NMEA glitch); smoothed `_gps_speed_kmh = 0.4·prev + 0.6·inst`.
- Parked timeout: Δt ≥ 8 s under the gate → decay speed to 0 and re-baseline.

This EMA is *not* trusted to classify moving/stationary on its own: a single glitch
fix spikes it (e.g. a 30 m jump in 5 s ≈ 21 km/h) before it decays. Classification
is the state machine's job.

### Moving/stationary detection (`updateGpsStatusCache`, ~1 Hz)

Position-only hysteresis with a stationary anchor `_gps_ref_{lat,lon}_e6` and a
single transition timer `_gps_move_state_since_ms`:

```
if !ref_valid: ref = fix; moving = false            // first fix anchors here
ref_dist = distance(ref, fix)

if !moving:                                          // STATIONARY
    if ref_dist > MOVE_RADIUS (25 m):
        start/continue move timer
        if held >= MOVE_DWELL (15 s): moving = true; ref = fix; timer = 0
    else:
        timer = 0
        ref += (fix - ref) / 8                        // track slow GPS bias
else:                                                 // MOVING
    if ref_dist > MOVE_RADIUS: ref = fix; timer = 0   // advance along path
    else:
        start/continue stop timer
        if held >= STOP_DWELL (60 s): moving = false; ref = fix; timer = 0
```

- **Jitter / glitch while parked**: stays within 25 m of the anchor (or pokes out
  for a fix or two then returns → timer resets), so it never confirms moving. The
  `/8` bias tracking keeps slow cumulative drift from ever reaching the radius.
- **Slow walk**: outruns the 25 m radius and keeps getting farther, so the
  stop-dwell never completes — node stays moving.
- **Stop**: once fixes settle within 25 m of the anchor for 60 s, node flips to
  stationary.

### Send decision

```
should_send = !last_sent_valid                       // first beacon → send now
else if moving:                                       // state-machine MOVING
    if distance(last_sent, now) > 10 m:
        interval = movingInterval(speed_mps)         // 5 / 15 / 30 s
        should_send = elapsed >= interval
        if should_send: reset_stationary_backoff = true
else if stationary_timer elapsed:                    // state-machine STATIONARY
    should_send = true                               // keepalive (anchor position)
```

When moving (or before the first anchor), the beacon carries the live fix; when
parked it carries the anchor position, so the parked dot doesn't wander.

`calcFastGpsMovingIntervalMs(speed_mps)`:

| Speed (m/s)              | Interval |
|--------------------------|----------|
| < 0.75 (`IDLE_MAX`)      | stationary base (9 min) |
| < 1.8  (`WALK_MAX`)      | 30 s     |
| < 4.0  (`FAST_MAX`)      | 15 s     |
| ≥ 4.0                    | 5 s      |

### Stationary cadence

```
FAST_GPS_STATIONARY_BASE_INTERVAL_MS = 9 min
FAST_GPS_STATIONARY_MAX_INTERVAL_MS  = 9 min   // base == max → flat, no growth
```

After a successful send, `_fast_gps_next_stationary_send_at = now + interval`.
Because base == max, the post-send doubling clamps immediately and the keepalive
stays at a flat 9 minutes. A parked node therefore emits one beacon every 9
minutes rather than going silent. 9 is deliberately odd / non-round so a fleet of
parked nodes doesn't converge on a shared 5/10-minute boundary — keepalives roll
out of phase instead of colliding on air.

### Beacon payload (16 bytes, `DATA_TYPE_DEV`)

| Offset | Field | Size |
|--------|-------|------|
| 0      | `FAST_GPS_MAGIC` (0x47) | 1 |
| 1      | sender pub-key prefix   | 6 |
| 7      | `lat_e6` (int32)        | 4 |
| 11     | `lon_e6` (int32)        | 4 |
| 15     | speed (km/h, rounded)   | 1 |

Sent via `sendGroupData(channel, …)`. The configured `fast_gps_region` swaps a
region transport key into `send_scope` for this send only (null key = unscoped
flood), then restores the previous scope. A send-holdoff guard
(`_fast_gps_send_holdoff_until`) is checked before transmit (reserved; currently
never armed).

### Receive path (`onChannelDataRecv`)

For `DATA_TYPE_DEV` packets ≥ 16 B with the magic byte:
1. Publish a `PositionUpdate` (prefix, lat/lon, speed, local RX timestamp) to the
   UI bridge; resolve the sender's contact name by pub-key prefix when known.
2. **Geographic repeat suppression**: if the sender is within
   `NEARBY_DISTANCE_METERS` (100 m) of our own valid fix, `markDoNotRetransmit()`
   before `routeRecvPacket()` decides whether to flood it onward.

### Constants

| Name | Value | Role |
|------|-------|------|
| `FAST_GPS_MIN_MOVEMENT_METERS` | 10 m | movement gate vs last send |
| `FAST_GPS_MOVE_RADIUS_M` | 25 m | anchor jitter radius (move/stop threshold) |
| `FAST_GPS_MOVE_DWELL_MS` | 15 s | sustained-away to flip to moving |
| `FAST_GPS_STOP_DWELL_MS` | 60 s | sustained-near to flip to stationary |
| `FAST_GPS_MOVE_EVAL_MS` | 1 s | state-machine tick |
| `FAST_GPS_STATIONARY_*_INTERVAL_MS` | 9 min | flat stationary keepalive |
| `FAST_GPS_WALK / FAST / VERY_FAST_INTERVAL_MS` | 30 / 15 / 5 s | moving cadence |
| `FAST_GPS_SPEED_IDLE / WALK / FAST_MAX_MPS` | 0.75 / 1.8 / 4.0 | speed buckets |
| `NEARBY_DISTANCE_METERS` | 100 m | RX repeat-suppression radius |
| `FAST_GPS_PAYLOAD_LEN` | 16 | beacon size |

### State (reset by `resetFastGpsShareState`)

`_fast_gps_last_sent_{valid,lat_e6,lon_e6,at_ms}`, `_fast_gps_stationary_interval_ms`
(= base), `_fast_gps_next_stationary_send_at`, `_fast_gps_send_holdoff_until`.

State-machine state (reset on GPS-disable / reset): `_gps_is_moving`,
`_gps_ref_valid`, `_gps_ref_{lat,lon}_e6` (anchor), `_gps_move_state_since_ms`
(transition timer), `_gps_move_eval_ms` (last tick).

---

## 4. Files

- `src/mesh/companion/MyMesh.cpp` — `maybeSendFastGpsUpdate()`, `updateGpsStatusCache()`
  (speed EMA + moving/stationary hysteresis state machine), `onChannelDataRecv()`
  (RX + repeat suppression), interval helpers, and the `FAST_GPS_*` constants.
- `src/mesh/companion/MyMesh.h` — fast-GPS scheduler state members.
- `src/mesh/mesh_bridge.h` — `PositionUpdate` carried to the model.
- `src/model*.cpp` — `upsert_live_position()` consumes beacons for Map/compass.

## 5. Verification

1. **Build**: `wio-tracker-l1`, `t5-epaper`, `tdeck` compile clean.
2. **Moving**: walk/drive a node; confirm beacon spacing tracks speed (≈30 s
   walking, ≈5 s driving) and only after >10 m movement.
3. **Stationary**: park a node with a fix; confirm exactly one beacon ~every 9
   minutes (speed byte 0), carrying the stable anchor position, never fully silent.
4. **Jitter / glitch**: leave a node parked with a noisy fix (or simulate a
   tens-of-metres glitch jump that returns); confirm no movement-rate beacons and
   no fake track — still 9-min keepalive only, dot stays put.
5. **Departure**: walk a parked node away; confirm it flips to moving within ~30 s
   (MOVE_DWELL + a fix or two) and resumes speed-based cadence, then flips back to
   stationary ~60 s after stopping.
6. **Repeat suppression**: two co-located nodes (≤100 m); confirm neither
   re-floods the other's beacon (debug `suppress nearby GPS beacon`).
7. **UI**: a peer's beacon appears on the Map/compass with the resolved contact
   name and updates on each receive.

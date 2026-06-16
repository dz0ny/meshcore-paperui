# PRD: Provision — Device-to-Device Settings Sync

## 1. Executive Summary

**Problem**: Configuring a fresh Wio Tracker L1 by hand is slow and error-prone. A
team that runs several identical units must re-enter the same channels (name +
secret), radio parameters, clock timezone, and UI preferences on every device.
There is no way to clone one device's configuration onto another, and copying the
cryptographic identity is explicitly undesirable — every node must keep its own
keypair so it remains individually addressable on the mesh.

**Proposed Solution**: A **Provision** feature (Settings → Provision) that copies
everything *except* the identity from one device to another over a new, dedicated
BLE GATT service. One device **shares** its profile; the other **receives** it and
applies it immediately. Because BLE is role-asymmetric (one peripheral, one
central) and the vendored MeshCore companion already owns the single
`Bluefruit.begin()` call, provisioning runs as a **reboot-into-exclusive-mode**:
the request is persisted to a flag file, the device reboots, and on that boot the
companion radio is skipped so provisioning owns the BLE stack alone.

Contact lists are deliberately **not** part of the profile. Nodes discover each
other the normal mesh way — via a self-advert — so the feature pairs with two new
**Advert** actions (Settings → Mesh) that announce a node either to direct
neighbours (zero-hop) or mesh-wide (flood).

**Success Criteria**:
- A receiver inherits the sharer's channels, radio params, timezone, and UI prefs
  in one action, with no manual re-entry.
- The receiver keeps its own identity (node id/name/keypair unchanged) and remains
  individually addressable on the mesh.
- A normal (non-provisioning) boot is byte-for-byte unchanged — the companion BLE
  restore path is untouched when no request is pending.
- A failed or crashed provision can never trap the device: the flag is cleared on
  entry, so the next boot is normal.
- The shared `mesh_task.h` / `i18n.h` / `settings.cpp` / `model.*` edits build
  clean on `t5-epaper` and `tdeck` as well as `wio-tracker-l1`.

---

## 2. User Experience & Functionality

### User Personas

- **Team lead** configuring a fleet — sets up channels, radio, and clock once on a
  master unit, then clones that profile to each member device.
- **Field member** receiving a freshly-flashed unit — wants to join the team's
  channels without typing 32-byte secrets, while keeping a unique identity.

### Scope

- **Wio Tracker L1 only** (`BOARD_WIO_L1`, nRF52840 / Adafruit Bluefruit). The BLE
  state machine is nRF52/Bluefruit-specific. ESP boards (t5-epaper, tdeck) compile
  the shared edits but expose no Provision menu.

### User Stories

**US-1**: As a team lead, I want a **Share Profile** action so my device advertises
its config for nearby receivers to pull.
- **AC**: Settings → Provision → Share Profile persists `/provision = 'S'` and
  reboots into a status screen showing "Waiting for receiver".
- **AC**: The sharer stays advertising and serves receivers; the user reboots it
  (Back/B button) when done.

**US-2**: As a field member, I want a **Receive Profile** action that finds the
nearest sharer and applies its config with no further prompts.
- **AC**: Settings → Provision → Receive Profile persists `/provision = 'R'` and
  reboots into a status screen showing "Searching…".
- **AC**: The receiver auto-connects to the strongest-RSSI device advertising the
  Provision service — no pairing confirmation.
- **AC**: On success the screen shows "Transferring" → "Done", then the device
  reboots ~1.5 s later so the new prefs load.
- **AC**: With no sharer in range, after ~30 s the screen shows "Failed"; the B
  button returns to a normal boot. The device boots normally next time regardless
  (flag cleared on entry).

**US-3**: As a field member, I want my identity preserved so I'm still uniquely
addressable after receiving a profile.
- **AC**: After Receive, Mesh Info shows the device's original node id/name; the
  identity keypair in `/identity/_main.id` is never written by provisioning.

**US-4**: As a team lead, I want devices to find each other after provisioning
without a wired or BLE step.
- **AC**: Settings → Mesh → **Advert (direct)** sends a zero-hop self-advert;
  **Advert (flood)** sends a mesh-wide self-advert. Both show an "Advert sent"
  toast.
- **AC**: Receiving nodes pick up the advert through the normal MeshCore path.

**US-5**: As a user watching one channel, I want the home unread count to reflect
only the channel I selected under Display settings.
- **AC**: The dashboard unread count and lock-screen preview increment only for
  messages whose `channel_idx` equals `get_msg_channel()`.
- **AC**: The per-contact unread tally (Team badge) is unaffected and still counts
  all senders.

### Non-Goals

- **Transferring contacts.** Contact rosters do not sync; identity exchange over BLE
  was rejected because it would require multiple import/export round-trips to
  converge. Nodes use self-adverts instead.
- **Transferring the identity, node name, node lat/lon, BLE PIN, or passwords.**
- **Confirmation/pairing UX** on the receiver — nearest sharer wins, by design.
- **ESP support** for the Provision menu (no Bluefruit central role there).
- **Animations / transitions** on the status screen.

---

## 3. Technical Specifications

### Critical Constraint: single `Bluefruit.begin()`

The Nordic SoftDevice can be initialised once per boot. The vendored
`SerialBLEInterface` (companion app transport) calls `Bluefruit.begin(1, 0)`
(peripheral only). The receiver needs central role, which requires
`Bluefruit.begin(1, 1)`. We cannot edit the submodule and cannot double-`begin()`.

**Resolution**: provisioning never co-exists with the companion BLE. A pending
request reboots the device; on that boot `mesh::task::start()` starts the mesh
stack normally (so prefs/channels are available for export) but calls
`the_mesh->startInterface(null_serial)` instead of the BLE companion, then hands
the radio to `provision::begin(mode)`, which owns `Bluefruit.begin(1, 1)`.

### Roles

| Action  | BLE role           | Behaviour                                            |
|---------|--------------------|------------------------------------------------------|
| Share   | Peripheral (server)| Advertises the service, notifies the profile blob    |
| Receive | Central (client)   | Scans by service UUID, connects to strongest RSSI    |

### BLE service

- Service UUID: `6d657368-7569-7072-6f76-000000000001`
- Characteristic UUID: `6d657368-7569-7072-6f76-000000000002` (NOTIFY)
- Advertised device name: `MeshCore-Provision`

### Profile blob format (little-endian)

```
Header (chunk seq 0, 16 bytes):
  uint32 magic   = 'MPRV' (0x5652504D)
  uint16 version = 1
  uint16 total_len            // payload bytes after header
  uint32 crc32                // over payload

Payload:
  fixed:    float freq; float bw; uint8 sf; uint8 cr; int8 tx_power_dbm;
            uint8 gps_enabled; uint32 gps_interval; uint8 advert_loc_policy;
            uint8 fast_gps_channel_idx; uint8 fast_gps_region; uint8 client_repeat;
            int8 tz_offset_hours; uint8 lang; uint8 invert; uint8 msg_channel;
            uint8 buzzer_quiet;
  channels: repeated { uint8 idx; char name[32]; uint8 secret[32]; }  // non-empty only
```

Channel `idx` is preserved so the synced `msg_channel` index stays valid on the
receiver. `MAX_GROUP_CHANNELS = 40`, so the worst case (~2.6 KB) exceeds a single
GATT attribute and requires chunked transfer.

### Chunked transfer

- Each notification is **20 bytes** (`[uint16 seq][≤18 data]`), the ATT minimum, so
  the transfer is MTU-independent.
- Seq 0 carries the 16-byte header; seq 1..N carry payload data.
- The receiver verifies `magic`, `version`, `total_len ≤ buffer`, and `crc32`
  before applying. Any mismatch → Error.
- Buffer: `g_buf[3072]`.

### Apply path (receiver)

Flash writes must not run in SoftDevice (notify) context. The notify callback only
accumulates bytes and sets `g_recv_complete`; the actual `profile_import()` runs
from `receive_loop()` in the main loop. Import:
1. Writes NodePrefs fields, `setChannel(idx, ch)` per channel.
2. Persists via `store->saveChannels(the_mesh)` (MyMesh::saveChannels is private —
   routed through the DataStore) and `the_mesh->savePrefs()`.
3. Writes the UI pref byte-files `/tz`, `/lang`, `/invert` (picked up by `load_*`
   on next boot) and `set_msg_channel()`.
4. Reboots ~1.5 s later.

### State machine

```
Mode  { None, Share, Receive }
State { Idle, Advertising, Connected, Sending, Scanning, Receiving, Done, Error }
```

- `pending()` reads `/provision` ('S'/'R'); `request(m)` writes it then reboots.
- `begin(m)` clears the flag first (crash-safety), then starts the role.
- `loop()` pumps `share_loop()` or `receive_loop()`.
- `progress()` = percent of bytes sent/received.
- Scan timeout: 30 s → Error.

### Self-advert (companion to discovery)

`MyMesh::advert(bool flood = false)` gained a `flood` parameter. `false` keeps the
prior zero-hop behaviour (and the boot advert); `true` calls
`sendFlood(pkt, (uint32_t)0, _prefs.path_hash_mode + 1)`. Exposed as
`mesh::task::send_advert(bool flood)` in both backends (ESP takes the mesh mutex).

### Selected-channel unread (Wio)

`note_incoming_message(from, text, channel_idx)` gained the channel argument. On
Wio (`model_mono.cpp`) the dashboard unread count + lock-screen preview increment
only when `channel_idx == get_msg_channel()`; the per-contact Team tally still
counts all channels. On ESP (`model.cpp`) the argument is ignored — the
selected-channel setting (`set_display.cpp`) is `#ifdef BOARD_WIO_L1`-only and
`get_msg_channel` is not implemented there.

---

## 4. Files

### New
- `src/mesh/provision.h` — public API (`Mode`, `State`, `pending/request/begin/loop/reboot/state/progress`).
- `src/mesh/provision_wio.cpp` — BLE state machine (`#ifdef BOARD_WIO_L1`).
- `src/ui/screens/provision.h` / `provision.cpp` — the Share/Receive menu
  (`SCREEN_PROVISION`) and the transfer status screen (`SCREEN_PROVISION_RUN`).
- `docs/prd-provision.md` — this document.

### Modified
- `src/mesh/mesh_task.h` — declares `profile_export`/`profile_import`/`send_advert`.
- `src/mesh/mesh_task_wio.cpp` — `profile_export`/`profile_import`; start() branches
  on `provision::pending()`; loop() pumps `provision::loop()`; `send_advert`.
- `src/mesh/mesh_task.cpp` — `send_advert` (ESP, mutex-guarded).
- `src/mesh/companion/MyMesh.h` / `MyMesh.cpp` — `advert(bool flood)`.
- `src/main_wio.cpp` — registers both screens; boots to `SCREEN_PROVISION_RUN`
  when a request is pending; B button on the run screen aborts to a normal boot.
- `src/ui/screens/settings.cpp` — Provision row (Wio).
- `src/ui/screens/set_mesh.cpp` — Advert (direct) / Advert (flood) rows + toast.
- `src/ui/screen_ids.h` — `SCREEN_PROVISION = 32`, `SCREEN_PROVISION_RUN = 33`.
- `src/ui/ui_screen_mgr_mono.cpp` — `MAX_ID` 32 → 34.
- `src/ui/i18n.h` — provision + advert token groups (EN + ASCII-Slovenian).
- `src/model.h` / `model.cpp` / `model_mono.cpp` — `note_incoming_message`
  channel argument.
- `platformio.ini` — `provision_wio.cpp` + `provision.cpp` in the wio src filter.

---

## 5. Verification

1. **Build**: `uvx platformio run -e wio-tracker-l1`, `-e t5-epaper`, `-e tdeck` —
   all compile clean.
2. **Two-unit sync**: on unit A set distinct channels/tz/lang, Settings → Provision
   → Share Profile (reboots to "Waiting for receiver"). On unit B: Receive Profile
   → "Searching…" → "Transferring" → "Done" → reboot.
3. **Confirm**: unit B shows A's channels (Messages picker), radio params
   (Settings → Mesh), timezone, language, and invert state — **and keeps its own
   identity** (Mesh Info node id/name unchanged).
4. **Discovery**: on both units Settings → Mesh → Advert (flood); confirm each
   appears in the other's recently-heard / can be added as a contact.
5. **Negative**: Receive with no sharer → ~30 s → "Failed"; B returns to normal
   boot; device boots normally afterwards (flag cleared on entry).
6. **Unread filter**: with channel X selected under Display, send traffic on
   channel Y → home unread count does not increase; send on channel X → it does.

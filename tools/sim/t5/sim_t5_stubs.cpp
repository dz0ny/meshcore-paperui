// Host stand-ins for the model / mesh / board / prefs layers the LVGL T5 screens
// read, plus sample fixtures so every screen has something to draw. The real UI
// code (ui_kit_lvgl, screen mgr, theme, components, screens) is compiled
// unchanged; only these hardware/data layers are swapped.
#include "model.h"
#include "mesh/mesh_task.h"
#include "mesh/provision.h"
#include "board.h"
#include "nvs_param.h"
#include <helpers/AdvertDataHelpers.h>   // ADV_TYPE_* (sim shim)
#include "waypoint_store.h"
#include "trail_store.h"
#include "ui/i18n.h"
#include <cstring>
#include <cstdlib>

// ---- model -----------------------------------------------------------------
namespace model {
    static StoredMessage msg_store[MAX_STORED_MESSAGES];
    StoredMessage* messages = msg_store;
    int message_count = 0;

    GPS     gps     = {};
    Battery battery = {};
    Mesh    mesh    = {};
    Clock   clock   = {};
    Sleep   sleep_cfg = {};
    uint32_t epoch_now = 0;

    ContactEntry contacts[MAX_CONTACT_ENTRIES] = {};
    int contact_count = 0;
    uint32_t contacts_revision = 0;

    LivePosition live_positions[MAX_LIVE_POSITIONS] = {};
    int live_position_count = 0;
    uint32_t live_position_revision = 0;

    DiscoveryEntry discovery[MAX_DISCOVERY_ENTRIES] = {};
    int discovery_count = 0;
    uint32_t discovery_revision = 0;

    TelemetryEntry telemetry[MAX_TELEMETRY_ENTRIES] = {};
    uint32_t telemetry_revision = 0;
    TraceEntry traces[MAX_TRACE_ENTRIES] = {};
    uint32_t trace_revision = 0;

    TrailStore    trail;
    WaypointStore waypoints;

    static uint32_t s_dirty = 0;

    void init_messages() {}
    void touch_activity() {}
    bool should_sleep() { return false; }
    void delete_message(int idx) {
        if (idx < 0 || idx >= message_count) return;
        for (int i = idx; i < message_count - 1; i++) msg_store[i] = msg_store[i + 1];
        message_count--;
    }
    void note_incoming_message(const char*, const char*, uint8_t) {}
    void clear_unread_messages() { sleep_cfg.unread_messages = 0; }
    void mark_dirty(uint32_t flags) { s_dirty |= flags; }
    uint32_t take_dirty() { uint32_t d = s_dirty; s_dirty = 0; return d; }
    void ingest_bridge_events() {}
    void refresh_contacts() {}
    void refresh_discovery() {}
    void upsert_live_position(const uint8_t*, const char*, int32_t, int32_t, uint32_t, uint8_t) {}
    const ContactEntry* find_contact_by_prefix(const uint8_t*, int) { return nullptr; }
    const ContactEntry* find_contact_by_name(const char* name) {
        for (int i = 0; i < contact_count; i++)
            if (strncmp(contacts[i].name, name, 32) == 0) return &contacts[i];
        return nullptr;
    }
    const TelemetryEntry* find_telemetry(const uint8_t*, int) { return nullptr; }
    const TraceEntry* find_trace(uint32_t) { return nullptr; }
    void update_gps() {}
    void update_battery() {}
    void update_mesh() {}
    void update_clock() {}
    uint32_t gps_update_interval_ms() { return 1000; }

    // Sample data shared by all screens — tweak to preview other states.
    void sim_seed() {
        // Reset counters so re-seeding (the web Reset button) doesn't double up.
        message_count = contact_count = live_position_count = discovery_count = 0;

        const char* lang = getenv("SIM_LANG");
        i18n::set_lang(lang && lang[0] == 's' ? i18n::SL : i18n::EN);

        clock.hour = 10; clock.minute = 54;
        clock.year = 25; clock.month = 6; clock.day = 23;
        gps.has_fix = true; gps.satellites = 9; gps.module_ok = true;
        gps.lat = 46.05; gps.lng = 14.50; gps.altitude_m = 298;
        gps.status_text = "Fix OK"; gps.speed_kmh = 4.2;
        gps.heading_deg = 47; gps.heading_valid = true;
        battery.percent = 76; battery.voltage_mv = 3960; battery.charging = true;
        battery.current_ma = 120; battery.temperature_c = 27.5f;
        battery.remain_mah = 1520; battery.full_mah = 2000; battery.design_mah = 2000;
        battery.health_pct = 98; battery.charger_ok = true; battery.vbus_v = 5.02f;
        battery.vsys_v = 4.05f; battery.vbat_v = 3.96f; battery.charge_status = "Charging";
        battery.bus_status = "USB"; battery.ntc_status = "Normal";
        mesh.radio_ok = true; mesh.peer_count = 5; mesh.rx_packets = 142; mesh.tx_packets = 88;
        mesh.last_rssi = -82; mesh.last_snr = 7.5; mesh.node_name = "Bob \xF0\x9F\x9A\x80";
        mesh.freq_mhz = 869.525f; mesh.bw_khz = 250; mesh.sf = 11; mesh.cr = 5; mesh.tx_power_dbm = 22;
        mesh.ble_enabled = true;
        epoch_now = 1750000000;
        sleep_cfg.unread_messages = 2;
        // Screens reload their lists only when the revision differs from the last
        // seen (which resets to 0 on entry) — bump past 0 so they populate.
        contacts_revision = live_position_revision = discovery_revision = 1;
        telemetry_revision = trace_revision = 1;

        auto add_contact = [](const char* name, uint8_t tag, uint8_t type, bool fav,
                              int32_t lat_e6, int32_t lon_e6) {
            ContactEntry& c = contacts[contact_count++];
            memset(&c, 0, sizeof(c));
            strncpy(c.name, name, sizeof(c.name) - 1);
            c.type = type;
            c.flags = fav ? CONTACT_FLAG_FAVORITE : 0;
            c.has_path = true;
            c.gps_lat = lat_e6; c.gps_lon = lon_e6;
            for (int i = 0; i < 32; i++) c.pub_key[i] = tag + i;
            if (lat_e6 || lon_e6) {
                LivePosition& p = live_positions[live_position_count++];
                memset(&p, 0, sizeof(p));
                for (int i = 0; i < 6; i++) p.pub_key_prefix[i] = tag + i;
                strncpy(p.name, name, sizeof(p.name) - 1);
                p.lat_e6 = lat_e6; p.lon_e6 = lon_e6;
                p.timestamp = epoch_now - 120; p.valid = true; p.speed_kmh = 3;
            }
        };
        add_contact("Ana \xF0\x9F\x9A\x80", 0x10, CONTACT_TYPE_CHAT, true,  46070000, 14520000);
        add_contact("Bojan", 0x20, CONTACT_TYPE_CHAT, true,  46030000, 14480000);
        add_contact("Cilka", 0x30, CONTACT_TYPE_CHAT, false, 0, 0);
        add_contact("Repeater-1", 0x40, ADV_TYPE_REPEATER, false, 0, 0);

        for (int i = 0; i < contact_count; i++) note_contact_unread(contacts[i].name);

        auto add_msg = [](const char* who, const char* text, bool self) {
            StoredMessage& m = msg_store[message_count++];
            memset(&m, 0, sizeof(m));
            strncpy(m.sender, who, sizeof(m.sender) - 1);
            strncpy(m.text, text, sizeof(m.text) - 1);
            m.is_self = self; m.channel_idx = 0xFF;
            m.hour = 10; m.minute = 50 + message_count;
        };
        add_msg("Ana \xF0\x9F\x9A\x80", "On my way, there in 10 minutes.", false);
        add_msg("me", "OK, great. See you at the hut.", true);
        add_msg("Bojan", "Meeting point is the spring on top?", false);
        add_msg("me", "Yes — bring water, it's hot today.", true);

        auto add_disc = [](const char* name, uint8_t tag, uint8_t hops) {
            DiscoveryEntry& d = discovery[discovery_count++];
            memset(&d, 0, sizeof(d));
            strncpy(d.name, name, sizeof(d.name) - 1);
            for (int i = 0; i < 7; i++) d.pubkey_prefix[i] = tag + i;
            d.path_len = hops; d.recv_timestamp = epoch_now - 30 * discovery_count;
        };
        add_disc("Dolina", 0x50, 1);
        add_disc("Vrh", 0x60, 2);
        add_disc("Koca", 0x70, 0);

        waypoints.add(46123456, 14987654, 0, "Hut");
        waypoints.add(46200000, 15000000, 0, "Spring");

        trail.setActive(true);
        int32_t la = 46050000, lo = 14500000;
        for (int i = 0; i < 12; i++) { trail.addPoint(la, lo, epoch_now + i * 60, 5); la += 1000; lo += 500; }
    }
}

// ---- mesh::task ------------------------------------------------------------
namespace mesh { namespace task {
    void (*diag_step)(const char*) = nullptr;
    void start(int) {}
    void loop() {}
    bool is_ready() { return true; }
    void flush_storage() {}
    bool send_message(const char*, const char*) { return true; }
    bool send_to_name(const char*, const char*) { return true; }
    bool send_public(const char*) { return true; }
    bool send_channel(uint8_t, const char*) { return true; }
    uint8_t get_msg_channel() { return 0; }
    void set_msg_channel(uint8_t) {}
    const char* node_name() { return "Bob \xF0\x9F\x9A\x80"; }
    void set_node_name(const char*) {}
    int get_channels(ChannelEntry* dest, int max_num) {
        if (max_num < 1) return 0;
        dest[0].idx = 0;
        strncpy(dest[0].name, "Public", sizeof(dest[0].name) - 1);
        dest[0].name[sizeof(dest[0].name) - 1] = 0;
        return 1;
    }
    bool get_channel_alerts() { return false; }
    void set_channel_alerts(bool) {}
    float   get_freq() { return 869.525f; }
    float   get_bw()   { return 250.0f; }
    uint8_t get_sf()   { return 11; }
    uint8_t get_cr()   { return 5; }
    int8_t  get_tx_power() { return 22; }
    void    set_freq(float) {}
    void    set_bw(float) {}
    void    set_sf(uint8_t) {}
    void    set_cr(uint8_t) {}
    void    set_tx_power(int8_t) {}
    uint8_t     get_fast_gps_channel() { return 0; }
    void        set_fast_gps_channel(uint8_t) {}
    uint8_t     get_fast_gps_region() { return 0; }
    void        set_fast_gps_region(uint8_t) {}
    uint8_t     fast_gps_region_count() { return 2; }
    const char* fast_gps_region_label(uint8_t idx) { return idx == 0 ? "Unscoped" : "si"; }
    bool get_buzzer_enabled() { return true; }
    void set_buzzer_enabled(bool) {}
    bool get_gps_enabled() { return true; }
    void set_gps_enabled(bool) {}
    bool get_advert_location() { return false; }
    void set_advert_location(bool) {}
    bool get_client_repeat() { return false; }
    void set_client_repeat(bool) {}
    void ble_enable() {}
    void ble_disable() {}
    bool ble_is_enabled() { return true; }
    void set_ble_pin(uint32_t) {}
    uint32_t get_ble_pin() { return 123456; }
    uint32_t regen_ble_pin() { return 123456; }
    void send_advert(bool) {}
    int  last_import_channels() { return 0; }
    int  last_import_contacts() { return 0; }
    bool is_contact(const uint8_t*) { return true; }
    bool is_favorite(const uint8_t*) { return true; }
    bool toggle_favorite(const uint8_t*) { return true; }
    bool add_contact_by_prefix(const uint8_t*) { return true; }
    bool remove_contact_by_prefix(const uint8_t*) { return true; }
}}

// ---- sd_log (SD message persistence — no-op in the sim) ---------------------
namespace sd_log { void mark_dirty() {} }

// ---- excluded screens referenced by included ones --------------------------
// contact_detail can push to the Ping screen (excluded from the sim build);
// stub the one entry point it calls so it links. The push itself is a no-op
// because SCREEN_PING is never registered.
namespace ui { namespace screen { namespace ping {
    void set_contact(const char*, int32_t, int32_t, uint8_t, bool, const uint8_t*) {}
}}}

// ---- board -----------------------------------------------------------------
namespace board {
    bool peri_status[E_PERI_MAX] = {};
    volatile bool home_button_pressed = false;
    SemaphoreHandle_t i2c_mutex = nullptr;
    SimTouch touch;
    void init() {}
    void seed_clock_from_rtc() {}
}

// ---- provision -------------------------------------------------------------
namespace provision {
    Mode  pending() { return Mode::None; }
    void  request(Mode) {}
    void  begin(Mode) {}
    void  loop() {}
    void  reboot() {}
    State state() { return State::Idle; }
    int   progress() { return 0; }
    int   bytes_done() { return 0; }
    int   bytes_total() { return 0; }
    int   device_count() { return 0; }
    const char* device_name(int) { return ""; }
    int   device_rssi(int) { return 0; }
    void  connect_to(int) {}
}

// ---- nvs_param (prefs) -----------------------------------------------------
void nvs_param_set_i8(NVSDataID, int8_t) {}
void nvs_param_set_u8(NVSDataID, uint8_t) {}
void nvs_param_set_i16(NVSDataID, int16_t) {}
void nvs_param_set_u16(NVSDataID, uint16_t) {}
void nvs_param_set_i32(NVSDataID, int32_t) {}
void nvs_param_set_u32(NVSDataID, uint32_t) {}
void nvs_param_set_i64(NVSDataID, int64_t) {}
void nvs_param_set_u64(NVSDataID, uint64_t) {}
void nvs_param_set_ff(NVSDataID, float) {}
void nvs_param_set_dd(NVSDataID, double) {}
void nvs_param_set_bb(NVSDataID, bool) {}
void nvs_param_set_str(NVSDataID, const char*) {}
int8_t   nvs_param_get_i8(NVSDataID) { return 0; }
uint8_t  nvs_param_get_u8(NVSDataID) { return 0; }
int16_t  nvs_param_get_i16(NVSDataID) { return 0; }
uint16_t nvs_param_get_u16(NVSDataID) { return 0; }
int32_t  nvs_param_get_i32(NVSDataID) { return 0; }
uint32_t nvs_param_get_u32(NVSDataID) { return 0; }
int64_t  nvs_param_get_i64(NVSDataID) { return 0; }
uint64_t nvs_param_get_u64(NVSDataID) { return 0; }
float    nvs_param_get_ff(NVSDataID) { return 0; }
double   nvs_param_get_dd(NVSDataID) { return 0; }
bool     nvs_param_get_bb(NVSDataID) { return false; }
const char* nvs_param_get_str(NVSDataID) { return ""; }

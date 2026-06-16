#ifdef BOARD_WIO_L1

#include "provision.h"
#include <Arduino.h>
#include <bluefruit.h>
#include <InternalFileSystem.h>
#include "mesh_task.h"
#include "companion/target.h"   // mc_board.reboot()

namespace provision {

// Custom 128-bit UUIDs (not SIG-assigned). Same on both ends of the transfer.
static const BLEUuid SVC_UUID(BLEUuid("6d657368-7569-7072-6f76-000000000001"));
static const BLEUuid CHR_UUID(BLEUuid("6d657368-7569-7072-6f76-000000000002"));

static const uint32_t MAGIC   = 0x5652504D;  // 'MPRV'
static const uint16_t VERSION = 1;
static const uint16_t CHUNK   = 18;          // notify body after the 2-byte seq (fits 20-byte ATT min)

static Mode  g_mode  = Mode::None;
static State g_state = State::Idle;

static uint8_t  g_buf[3072];
static uint16_t g_total_len = 0;   // expected payload length (receiver) / payload length (sender)
static uint32_t g_crc = 0;

// sender
static BLEService        g_svc(SVC_UUID);
static BLECharacteristic g_chr(CHR_UUID);
static uint16_t g_conn = BLE_CONN_HANDLE_INVALID;
static bool     g_connected = false;
static bool     g_sent_header = false;
static uint16_t g_send_off = 0;
static uint16_t g_seq = 0;

// receiver
static BLEClientService        g_csvc(SVC_UUID);
static BLEClientCharacteristic g_cchr(CHR_UUID);
static uint16_t g_recv_off = 0;
static bool     g_have_header = false;
static volatile bool g_recv_complete = false;
static uint32_t g_done_ms = 0;   // receiver: reboot shortly after a successful apply

// Discovered advertisers, shown to the user so they can pick a sharer instead of
// blindly connecting to the strongest signal (and so it's visible whether the
// sharer is being heard at all). Filled from the scan Rx callback.
struct ScanDev {
    uint8_t addr[6];
    uint8_t addr_type;
    char    name[24];
    int8_t  rssi;
    bool    has_svc;
};
static ScanDev g_devs[12];
static volatile int g_dev_count = 0;

static uint32_t crc32(const uint8_t* p, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    return ~crc;
}

// ---- persisted request flag ------------------------------------------------
Mode pending() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f = InternalFS.open("/provision", FILE_O_READ);
    if (!f) return Mode::None;
    int b = f.read(); f.close();
    if (b == 'S') return Mode::Share;
    if (b == 'R') return Mode::Receive;
    return Mode::None;
}

static void clear_flag() {
    InternalFS.begin();
    InternalFS.remove("/provision");
}

void request(Mode m) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove("/provision");
    File f = InternalFS.open("/provision", FILE_O_WRITE);
    if (f) { uint8_t v = (m == Mode::Share) ? 'S' : 'R'; f.write(&v, 1); f.close(); }
    mc_board.reboot();
}

// ---- share (peripheral) ----------------------------------------------------
static void on_periph_connect(uint16_t conn) {
    g_conn = conn;
    g_connected = true;
    g_sent_header = false;
    g_send_off = 0;
    g_seq = 0;
    g_state = State::Connected;
}
static void on_periph_disconnect(uint16_t, uint8_t) {
    g_connected = false;
    g_conn = BLE_CONN_HANDLE_INVALID;
    g_state = State::Advertising;
}

static void begin_share() {
    g_total_len = (uint16_t)mesh::task::profile_export(g_buf, sizeof(g_buf));
    g_crc = crc32(g_buf, g_total_len);

    Bluefruit.begin(1, 1);
    Bluefruit.setName("MeshCore-Provision");
    Bluefruit.Periph.setConnectCallback(on_periph_connect);
    Bluefruit.Periph.setDisconnectCallback(on_periph_disconnect);

    g_svc.begin();
    g_chr.setProperties(CHR_PROPS_NOTIFY);
    g_chr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    g_chr.setMaxLen(20);
    g_chr.begin();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(g_svc);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.start(0);
    g_state = State::Advertising;
}

static void share_loop() {
    if (!g_connected || !g_chr.notifyEnabled(g_conn)) return;
    if (g_state == State::Done) return;
    g_state = State::Sending;

    uint8_t pkt[20];
    if (!g_sent_header) {
        pkt[0] = 0; pkt[1] = 0;
        memcpy(&pkt[2], &MAGIC, 4);
        memcpy(&pkt[6], &VERSION, 2);
        memcpy(&pkt[8], &g_total_len, 2);
        memcpy(&pkt[12], &g_crc, 4);
        if (!g_chr.notify(pkt, 16)) return;   // buffer full — retry next loop
        g_sent_header = true;
        g_seq = 1;
    }
    while (g_send_off < g_total_len) {
        uint16_t n = g_total_len - g_send_off;
        if (n > CHUNK) n = CHUNK;
        pkt[0] = g_seq & 0xFF; pkt[1] = g_seq >> 8;
        memcpy(&pkt[2], g_buf + g_send_off, n);
        if (!g_chr.notify(pkt, n + 2)) return;
        g_send_off += n;
        g_seq++;
    }
    g_state = State::Done;
}

// ---- receive (central) -----------------------------------------------------
static void on_notify(BLEClientCharacteristic*, uint8_t* data, uint16_t len) {
    if (len < 2 || g_recv_complete) return;
    uint16_t seq = data[0] | (data[1] << 8);
    const uint8_t* body = data + 2;
    uint16_t blen = len - 2;

    if (seq == 0) {
        if (blen < 12) { g_state = State::Error; return; }
        uint32_t magic, crc; uint16_t ver, tlen;
        memcpy(&magic, &body[0], 4);
        memcpy(&ver,   &body[4], 2);
        memcpy(&tlen,  &body[6], 2);
        memcpy(&crc,   &body[8], 4);
        if (magic != MAGIC || ver != VERSION || tlen > sizeof(g_buf)) { g_state = State::Error; return; }
        g_total_len = tlen; g_crc = crc; g_recv_off = 0; g_have_header = true;
        return;
    }
    if (!g_have_header) return;
    if (g_recv_off + blen > sizeof(g_buf)) { g_state = State::Error; return; }
    memcpy(g_buf + g_recv_off, body, blen);
    g_recv_off += blen;
    if (g_recv_off >= g_total_len) g_recv_complete = true;   // apply from loop(), not in SD context
}

static void on_central_connect(uint16_t conn) {
    if (!g_csvc.discover(conn) || !g_cchr.discover()) {
        Bluefruit.disconnect(conn);
        return;
    }
    g_recv_off = 0; g_total_len = 0; g_have_header = false; g_recv_complete = false;
    g_cchr.enableNotify();
    g_state = State::Receiving;
}
static void on_central_disconnect(uint16_t, uint8_t) {
    if (g_state == State::Receiving) {
        // Lost mid-transfer — drop back to the picker so the user can retry.
        g_state = State::Scanning;
        Bluefruit.Scanner.start(0);
    }
}

// Collect advertisers into g_devs[] (deduped by address). We DON'T filter on the
// service UUID here — listing everything nearby makes it obvious whether the
// sharer is being heard, and we flag the ones that carry our service. Active
// scan pulls the scan-response name ("MeshCore-Provision" for a sharer).
static void on_scan(ble_gap_evt_adv_report_t* report) {
    for (int i = 0; i < g_dev_count; i++) {
        if (memcmp(g_devs[i].addr, report->peer_addr.addr, 6) == 0) {
            g_devs[i].rssi = report->rssi;          // refresh signal strength
            Bluefruit.Scanner.resume();
            return;
        }
    }
    bool has_svc = Bluefruit.Scanner.checkReportForUuid(report, SVC_UUID);
    char name[24] = {0};
    uint8_t nlen = Bluefruit.Scanner.parseReportByType(
        report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, (uint8_t*)name, sizeof(name) - 1);
    if (!nlen)
        nlen = Bluefruit.Scanner.parseReportByType(
            report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, (uint8_t*)name, sizeof(name) - 1);

    // Skip nameless devices that aren't sharers — they'd just be noise in the list.
    if (!has_svc && name[0] == 0) { Bluefruit.Scanner.resume(); return; }

    if (g_dev_count < (int)(sizeof(g_devs) / sizeof(g_devs[0]))) {
        ScanDev& d = g_devs[g_dev_count];
        memcpy(d.addr, report->peer_addr.addr, 6);
        d.addr_type = report->peer_addr.addr_type;
        d.rssi = report->rssi;
        d.has_svc = has_svc;
        if (name[0]) { strncpy(d.name, name, sizeof(d.name) - 1); d.name[sizeof(d.name) - 1] = 0; }
        else         { strncpy(d.name, "(unknown)", sizeof(d.name) - 1); }
        g_dev_count++;
    }
    Bluefruit.Scanner.resume();   // keep listening for more advertisers
}

static void begin_receive() {
    Bluefruit.begin(1, 1);
    Bluefruit.setName("MeshCore-Provision");
    Bluefruit.Central.setConnectCallback(on_central_connect);
    Bluefruit.Central.setDisconnectCallback(on_central_disconnect);

    g_csvc.begin();
    g_cchr.setNotifyCallback(on_notify);
    g_cchr.begin(&g_csvc);

    g_dev_count = 0;
    Bluefruit.Scanner.setRxCallback(on_scan);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.useActiveScan(true);   // request scan-response → device name
    Bluefruit.Scanner.start(0);              // no timeout: keep listing until the user picks
    g_state = State::Scanning;
}

int  device_count()            { return g_dev_count; }
const char* device_name(int i) { return (i >= 0 && i < g_dev_count) ? g_devs[i].name : ""; }
int  device_rssi(int i)        { return (i >= 0 && i < g_dev_count) ? g_devs[i].rssi : 0; }
bool device_has_service(int i) { return (i >= 0 && i < g_dev_count) ? g_devs[i].has_svc : false; }

void connect_to(int i) {
    if (i < 0 || i >= g_dev_count) return;
    Bluefruit.Scanner.stop();
    ble_gap_addr_t addr = {};
    addr.addr_type = g_devs[i].addr_type;
    memcpy(addr.addr, g_devs[i].addr, 6);
    g_state = State::Connected;     // show "connecting" until notifications arrive
    Bluefruit.Central.connect(&addr);
}

static void receive_loop() {
    if (g_recv_complete) {
        g_recv_complete = false;
        if (crc32(g_buf, g_total_len) == g_crc && mesh::task::profile_import(g_buf, g_total_len)) {
            g_state = State::Done;
            g_done_ms = millis();
        } else {
            g_state = State::Error;
        }
        return;
    }
    if (g_state == State::Done && g_done_ms && (millis() - g_done_ms) > 1500)
        mc_board.reboot();   // applied — restart so load_* picks up the new prefs
}

// ---- public ----------------------------------------------------------------
void begin(Mode m) {
    clear_flag();   // clear first so a crash mid-provision can't trap the device
    g_mode = m;
    if (m == Mode::Share)        begin_share();
    else if (m == Mode::Receive) begin_receive();
}

void loop() {
    if (g_mode == Mode::Share)        share_loop();
    else if (g_mode == Mode::Receive) receive_loop();
}

void reboot() { mc_board.reboot(); }

State state() { return g_state; }

int progress() {
    if (g_total_len == 0) return 0;
    uint16_t done = (g_mode == Mode::Share) ? g_send_off : g_recv_off;
    int pct = (int)((uint32_t)done * 100 / g_total_len);
    return pct > 100 ? 100 : pct;
}

} // namespace provision

#endif // BOARD_WIO_L1

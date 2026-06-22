#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include "compass.h"
#include "../screen_ids.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../model.h"

// Arduino.h defines a DEG_TO_RAD macro that clobbers ui::geo::DEG_TO_RAD; drop
// it before pulling in geo_utils so the constexpr in the header wins.
#ifdef DEG_TO_RAD
#undef DEG_TO_RAD
#endif
#include "../components/geo_utils.h"

// ===========================================================================
//  compass — dead-reckoning needle toward the selected peer
// ===========================================================================
namespace ui::screen::compass {

using namespace ui::kit;

static uint8_t  tgt_prefix[6] = {};
static char     tgt_name[32]  = {};
static int32_t  tgt_lat_e6 = 0, tgt_lon_e6 = 0;
static bool     tgt_set = false;
static bool     tgt_by_prefix = false;   // false → fixed coord, don't re-resolve

static Handle cv = nullptr;
static Handle info_label = nullptr;   // target name (top-left)
static Handle dist_label = nullptr;   // big distance + heading (bottom-center)
static Handle hint_label = nullptr;   // "move to calibrate" (top-right, when needed)
static Timer  update_timer = nullptr;

void set_target(const uint8_t* prefix6, const char* name,
                int32_t lat_e6, int32_t lon_e6) {
    memcpy(tgt_prefix, prefix6, 6);
    tgt_name[0] = 0;
    if (name && name[0]) { strncpy(tgt_name, name, sizeof(tgt_name) - 1); tgt_name[sizeof(tgt_name) - 1] = 0; }
    tgt_lat_e6 = lat_e6;
    tgt_lon_e6 = lon_e6;
    tgt_set = true;
    tgt_by_prefix = true;
}

void set_target_pos(const char* name, int32_t lat_e6, int32_t lon_e6) {
    memset(tgt_prefix, 0, sizeof(tgt_prefix));
    tgt_name[0] = 0;
    if (name && name[0]) { strncpy(tgt_name, name, sizeof(tgt_name) - 1); tgt_name[sizeof(tgt_name) - 1] = 0; }
    tgt_lat_e6 = lat_e6;
    tgt_lon_e6 = lon_e6;
    tgt_set = true;
    tgt_by_prefix = false;
}

// Re-resolve the target's freshest position by prefix; falls back to the
// snapshot captured at selection time. Fixed-coordinate targets stay put.
static void resolve_target() {
    if (!tgt_set || !tgt_by_prefix) return;
    for (int i = 0; i < model::live_position_count && i < model::MAX_LIVE_POSITIONS; i++) {
        const model::LivePosition& p = model::live_positions[i];
        if (p.valid && memcmp(p.pub_key_prefix, tgt_prefix, 6) == 0) {
            tgt_lat_e6 = p.lat_e6;
            tgt_lon_e6 = p.lon_e6;
            if (p.name[0]) { strncpy(tgt_name, p.name, sizeof(tgt_name) - 1); tgt_name[sizeof(tgt_name) - 1] = 0; }
            return;
        }
    }
}

static void draw_needle(int cx, int cy, int r, double up_deg) {
    // up_deg is the compass angle (0=N, 90=E) that should appear straight up.
    double rad = up_deg * ui::geo::DEG_TO_RAD;
    double s = sin(rad), c = cos(rad);
    int tipx = cx + (int)(r * s);
    int tipy = cy - (int)(r * c);
    int tailx = cx - (int)(r * 0.45 * s);
    int taily = cy + (int)(r * 0.45 * c);

    // Perpendicular unit (for thickness / arrowhead width).
    double px = c, py = s;

    // Thick shaft: the centre line plus a parallel offset on each side.
    for (int o = -1; o <= 1; o++) {
        int ox = (int)lround(o * px), oy = (int)lround(o * py);
        canvas_line(cv, tailx + ox, taily + oy, tipx + ox, tipy + oy, Color::Fg);
    }

    // Filled arrowhead: two base corners set back from the tip, fanned to fill.
    double back = (up_deg + 180.0) * ui::geo::DEG_TO_RAD;
    double hl = r * 0.40, hw = r * 0.24;
    int bx = tipx + (int)(hl * sin(back));
    int by = tipy - (int)(hl * cos(back));
    int c1x = bx + (int)(hw * px), c1y = by + (int)(hw * py);
    int c2x = bx - (int)(hw * px), c2y = by - (int)(hw * py);
    for (int t = 0; t <= 8; t++) {
        int xx = c1x + (c2x - c1x) * t / 8;
        int yy = c1y + (c2y - c1y) * t / 8;
        canvas_line(cv, tipx, tipy, xx, yy, Color::Fg);
    }
    canvas_fill_circle(cv, cx, cy, 3, Color::Fg);
}

static void rebuild() {
    if (!cv) return;
    resolve_target();

    const int w = canvas_w(cv), h = canvas_h(cv);
    canvas_clear(cv, Color::Bg);
    int cx = w / 2, cy = h / 2;
    int r  = (w < h ? w : h) / 2 - 6;
    if (r < 8) { canvas_flush(cv); return; }

    // Outer ring (dotted).
    for (int a = 0; a < 360; a += 6) {
        double rad = a * ui::geo::DEG_TO_RAD;
        canvas_pixel(cv, cx + (int)(r * sin(rad)), cy - (int)(r * cos(rad)), Color::Fg);
    }
    // "Ahead" marker: a small filled triangle at the top of the ring, the
    // reference the needle's angle is measured from (your heading / North).
    int ty = cy - r;
    for (int t = 0; t <= 6; t++)
        canvas_line(cv, cx, ty + 3, cx - 4 + (8 * t) / 6, ty - 4, Color::Fg);

    bool have_target = tgt_set && (tgt_lat_e6 != 0 || tgt_lon_e6 != 0);
    bool have_me     = model::gps.has_fix;

    if (have_target && have_me) {
        double brg = ui::geo::bearing(model::gps.lat, model::gps.lng,
                                      tgt_lat_e6 / 1e6, tgt_lon_e6 / 1e6);
        // Up = the direction we're heading (dead reckoning). When heading is
        // unknown, up = North and we show the calibrate hint.
        double my_heading = model::gps.heading_valid ? model::gps.heading_deg : 0.0;
        double rel = fmod(brg - my_heading + 360.0, 360.0);
        draw_needle(cx, cy, r - 4, rel);

        double dm = ui::geo::distance_km(model::gps.lat, model::gps.lng,
                                         tgt_lat_e6 / 1e6, tgt_lon_e6 / 1e6) * 1000.0;
        char dbuf[24];
        if (dm < 1000.0) snprintf(dbuf, sizeof(dbuf), "%d m", (int)(dm + 0.5));
        else             snprintf(dbuf, sizeof(dbuf), "%.1f km", dm / 1000.0);
        if (info_label) set_text(info_label, tgt_name[0] ? tgt_name : "?");
        if (dist_label) { set_textf(dist_label, "%s  %s", dbuf, ui::geo::bearing_to_cardinal(brg)); hidden(dist_label, false); }
        if (hint_label) hidden(hint_label, model::gps.heading_valid);
    } else if (!have_target) {
        // No destination chosen — say so instead of a bare dial + "?".
        if (info_label) set_text(info_label, i18n::t(i18n::T_NO_TARGET));
        if (dist_label) hidden(dist_label, true);
        if (hint_label) hidden(hint_label, true);
    } else {
        // Target known but no fix yet — show its name, distance unknown.
        if (info_label) set_text(info_label, tgt_name[0] ? tgt_name : "?");
        if (dist_label) { set_text(dist_label, "--"); hidden(dist_label, false); }
        if (hint_label) hidden(hint_label, true);
    }

    canvas_flush(cv);
}

static void update_cb(void*) { rebuild(); }

static void create(Handle parent) {
    Handle content = content_area(parent);
    free_layout(content);
    // No border around the dial — the canvas fills the content area, so the frame
    // is pure decoration and the edge pixels are better spent on the compass ring.

    int w = px_width(content), h = px_height(content);
    cv = canvas(content, w, h);
    size(cv, pct(100), pct(100));
    pos(cv, 0, 0);

    // Target name — top-left.
    info_label = label(content, "");
    font(info_label, Font::Small);
    card(info_label);
    pad(info_label, 3);
    pos(info_label, 4, 4);

    // Big distance + heading readout — bottom-center, the headline figure.
    dist_label = label(content, "");
    font(dist_label, Font::Title);
    card(dist_label);
    pad(dist_label, 3);
    text_center(dist_label);
    align(dist_label, Align::BottomMid, 0, -3);
    hidden(dist_label, true);

    // Calibrate hint — top-right, small, only while heading is unknown.
    hint_label = label(content, i18n::t(i18n::T_MOVE_TO_CAL));
    font(hint_label, Font::Small);
    card(hint_label);
    pad(hint_label, 3);
    align(hint_label, Align::TopRight, -4, 4);
    hidden(hint_label, true);

    rebuild();
}

static void entry() {
    update_timer = every(1000, update_cb, nullptr);
    rebuild();
}

static void exit_fn() {
    if (update_timer) { stop(update_timer); update_timer = nullptr; }
}

static void destroy() {
    cv = nullptr;
    info_label = nullptr;
    dist_label = nullptr;
    hint_label = nullptr;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::compass

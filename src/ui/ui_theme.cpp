#include "ui_theme.h"
#include "../nvs_param.h"

namespace ui::theme {

lv_style_t style_menu_row;
lv_style_t style_text_button;
lv_style_t style_nav_action;
lv_style_t style_transparent;

namespace {

bool styles_initialized = false;

void reset_style(lv_style_t* s) {
    if (styles_initialized) {
        lv_style_reset(s);
    } else {
        lv_style_init(s);
    }
}

void build_styles() {
    reset_style(&style_menu_row);
    lv_style_set_bg_color(&style_menu_row, lv_color_hex(EPD_COLOR_BG));
    lv_style_set_bg_opa(&style_menu_row, LV_OPA_COVER);
    lv_style_set_border_width(&style_menu_row, 1);
    lv_style_set_border_color(&style_menu_row, lv_color_hex(EPD_COLOR_BORDER));
    lv_style_set_border_side(&style_menu_row, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_pad_all(&style_menu_row, UI_MENU_ITEM_PAD);

    reset_style(&style_text_button);
    lv_style_set_bg_color(&style_text_button, lv_color_hex(EPD_COLOR_PROMPT_BG));
    lv_style_set_bg_opa(&style_text_button, LV_OPA_COVER);
    lv_style_set_border_width(&style_text_button, UI_TEXT_BTN_BORDER);
    lv_style_set_border_color(&style_text_button, lv_color_hex(EPD_COLOR_BORDER));
    lv_style_set_radius(&style_text_button, UI_TEXT_BTN_RADIUS);
    lv_style_set_pad_all(&style_text_button, UI_TEXT_BTN_PAD);

    reset_style(&style_nav_action);
    lv_style_set_bg_color(&style_nav_action, lv_color_hex(EPD_COLOR_BG));
    lv_style_set_bg_opa(&style_nav_action, LV_OPA_COVER);
    lv_style_set_border_width(&style_nav_action, UI_ACTION_BTN_BORDER);
    lv_style_set_border_color(&style_nav_action, lv_color_hex(EPD_COLOR_BORDER));
    lv_style_set_radius(&style_nav_action, UI_ACTION_BTN_RADIUS);
    lv_style_set_pad_hor(&style_nav_action, UI_ACTION_BTN_PAD_H);
    lv_style_set_pad_ver(&style_nav_action, UI_ACTION_BTN_PAD_V);

    reset_style(&style_transparent);
    lv_style_set_bg_opa(&style_transparent, LV_OPA_0);
    lv_style_set_border_width(&style_transparent, 0);
    lv_style_set_pad_all(&style_transparent, 0);

    styles_initialized = true;
}

} // anonymous

namespace {

const palette_t palettes[] = {
    {0xFFFFFF, 0x000000, 0x000000, 0x333333, 0x91B821, 0x1E1E00, 0xFFFEE6},
};

const char* names[] = {
    "Mono",
};

theme_id current_theme = THEME_LIGHT;

} // namespace

void init() {
    uint8_t stored = nvs_param_get_u8(NVS_ID_UI_THEME);
    if (stored >= count()) {
        stored = 0;
    }
    current_theme = static_cast<theme_id>(stored);
    build_styles();
}

uint8_t count() {
    return sizeof(palettes) / sizeof(palettes[0]);
}

theme_id current() {
    return current_theme;
}

bool set(theme_id id) {
    if (static_cast<uint8_t>(id) >= count()) {
        return false;
    }
    current_theme = id;
    build_styles();
    return true;
}

theme_id next() {
    return static_cast<theme_id>((static_cast<uint8_t>(current_theme) + 1) % count());
}

const char* current_name() {
    return names[static_cast<uint8_t>(current_theme)];
}

const palette_t& colors() {
    return palettes[static_cast<uint8_t>(current_theme)];
}

void style_scrollbar_hint(lv_obj_t* obj) {
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

} // namespace ui::theme

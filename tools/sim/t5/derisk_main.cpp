// Minimal de-risk harness: prove LVGL v9 + the project's lv_conf.h + fonts build
// and render on the host. Renders one label to a PNG via the sim LVGL port.
#include "sim_lvgl_port.h"
#include "ui_port.h"
#include "lvgl.h"
#include <cstdio>

extern bool write_gray_png(const char* path, int W, int H, const unsigned char* buf);

// noto_28 is one of the project fonts (src/fonts/noto_28.c) — confirm it links.
extern const lv_font_t lv_font_noto_28;

int main() {
    sim_lvgl::set_size(540, 960);
    sim_lvgl::set_millis(1);
    ui::port::init();

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN);

    lv_obj_t* lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl, &lv_font_noto_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(lbl, "LVGL host render OK\nMeshUI T5 sim");
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    for (int i = 0; i < 5; i++) { sim_lvgl::set_millis(1 + i * 33); lv_timer_handler(); }

    if (!write_gray_png("derisk.png", sim_lvgl::width(), sim_lvgl::height(), sim_lvgl::framebuffer())) {
        fprintf(stderr, "png write failed\n");
        return 1;
    }
    printf("wrote derisk.png (%dx%d)\n", sim_lvgl::width(), sim_lvgl::height());
    return 0;
}

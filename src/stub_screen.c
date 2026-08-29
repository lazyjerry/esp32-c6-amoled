// 設定畫面的空殼，內容在 M4 才填。
//
// 參拜簿本來也共用這支，M3 有了真的紀錄之後搬去 records_screen.c。
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "screen_mgr.h"
#include "settings_screen.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

typedef struct {
    const char *title;
    lv_obj_t *scr;
} stub_t;

static stub_t s_settings = {.title = "設定"};

static esp_err_t enter_stub(stub_t *st)
{
    lvgl_port_lock(0);
    if (!st->scr) {
        st->scr = lv_obj_create(NULL);
        lv_obj_remove_flag(st->scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(st->scr, lv_color_hex(0x140808), 0);

        lv_obj_t *title = lv_label_create(st->scr);
        lv_obj_set_style_text_font(title, &font_zh_28, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xC0A060), 0);
        lv_label_set_text(title, st->title);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

        lv_obj_t *hint = lv_label_create(st->scr);
        lv_obj_set_style_text_font(hint, &font_zh_16, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x6A5A48), 0);
        lv_label_set_text(hint, "尚未開放");
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30);
    }
    lv_screen_load(st->scr);
    lvgl_port_unlock();
    return ESP_OK;
}

static esp_err_t enter_settings(void) { return enter_stub(&s_settings); }

// 側翼只有一條路：回正殿。滑哪一邊都回去，長按也回去——
// 空殼裡沒有東西可做，讓人卡住毫無意義
static bool on_event(screen_event_t ev)
{
    switch (ev) {
    case SCREEN_EV_BOOT_HOLD:
    case SCREEN_EV_SWIPE_LEFT:
    case SCREEN_EV_SWIPE_RIGHT:
        screen_mgr_goto(&shrine_screen);
        return true;
    case SCREEN_EV_BOOT_KEY:
    case SCREEN_EV_SHAKE:
    case SCREEN_EV_WAVE:
        return false;
    }
    return false;
}

const screen_t settings_screen = {
    .name = "設定",
    .enter = enter_settings,
    .exit = NULL,
    .tick = NULL,
    .on_event = on_event,
};

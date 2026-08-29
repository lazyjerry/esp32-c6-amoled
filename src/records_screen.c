// 參拜簿（正殿左側）。列出最近幾次參拜：第幾次、問什麼、求到第幾籤。
//
// 刻意沒有日期——紀錄本身就不存日期，理由見 records.h。
// 每次進來重建清單，不快取：筆數少（最多 20 行），重建的成本遠低於
// 「畫面留著舊資料」這種錯誤的除錯成本。
#include "records_screen.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "records.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

#define PAD 20

static lv_obj_t *s_scr;
static lv_obj_t *s_total;
static lv_obj_t *s_list;

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x140808), 0);
    lv_obj_set_style_pad_all(s_scr, PAD, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_flex_flow(s_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_scr, 10, 0);
    lv_obj_set_scroll_dir(s_scr, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(title, &font_zh_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xC0A060), 0);
    lv_label_set_text(title, "參拜簿");

    s_total = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_total, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_total, lv_color_hex(0x8A7458), 0);

    // 清單自己一層，重建時只清這裡，標題不動
    s_list = lv_obj_create(s_scr);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_height(s_list, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
}

static void fill_list(void)
{
    lv_obj_clean(s_list);

    record_t rec[RECORDS_MAX];
    int n = records_recent(rec, RECORDS_MAX);

    lv_label_set_text_fmt(s_total, "累計 %u 次", (unsigned)records_total());

    if (n == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_obj_set_style_text_font(empty, &font_zh_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x6A5A48), 0);
        lv_label_set_text(empty, "尚無紀錄");
        return;
    }

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_label_create(s_list);
        lv_obj_set_style_text_font(row, &font_zh_16, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xC0B098), 0);
        lv_label_set_text_fmt(row, "第 %u 次　%s　第 %u 籤",
                              (unsigned)rec[i].seq,
                              ritual_cat_name((ritual_cat_t)rec[i].cat),
                              (unsigned)rec[i].poem);
    }
}

static esp_err_t enter(void)
{
    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    fill_list();
    lv_obj_scroll_to_y(s_scr, 0, LV_ANIM_OFF);
    lv_screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

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

const screen_t records_screen = {
    .name = "參拜簿",
    .enter = enter,
    .exit = NULL,
    .tick = NULL,
    .on_event = on_event,
};

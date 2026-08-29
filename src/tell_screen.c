// 稟告。6 個類別排成 2×3，點一格就記下來並進求籤。
#include "tell_screen.h"

#include "draw_screen.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include "records.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

#define CELL_W 150
#define CELL_H 92
#define GAP    12

static const char *TAG = "tell";

static lv_obj_t *s_scr;

// 點擊在 LVGL 任務裡發生，切畫面留給主迴圈
static volatile int s_picked = RITUAL_CAT_NONE;

static void on_pick(lv_event_t *e)
{
    s_picked = (int)(intptr_t)lv_event_get_user_data(e);
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x1A0808), 0);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(title, &font_zh_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x9A8060), 0);
    lv_label_set_text(title, "所求何事");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    for (int i = 0; i < RITUAL_CAT_COUNT; i++) {
        lv_obj_t *cell = lv_button_create(s_scr);
        lv_obj_set_size(cell, CELL_W, CELL_H);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x3A1812), 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0x8A6A30), 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_style_radius(cell, 8, 0);
        int col = i % 2, row = i / 2;
        lv_obj_align(cell, LV_ALIGN_TOP_MID,
                     (col == 0 ? -1 : 1) * (CELL_W + GAP) / 2,
                     70 + row * (CELL_H + GAP));
        lv_obj_add_event_cb(cell, on_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = lv_label_create(cell);
        lv_obj_set_style_text_font(label, &font_zh_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xE0C890), 0);
        lv_label_set_text(label, ritual_cat_name(i));
        lv_obj_center(label);
    }
}

static esp_err_t enter(void)
{
    s_picked = RITUAL_CAT_NONE;
    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    lv_screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

static void tick(void)
{
    if (s_picked == RITUAL_CAT_NONE) return;
    ritual_set_category(s_picked);
    records_count_tell(s_picked);
    records_flush();   // 這個畫面沒有動畫在跑，當場落地
    ESP_LOGI(TAG, "稟告：%s", ritual_cat_name(s_picked));
    s_picked = RITUAL_CAT_NONE;
    screen_mgr_goto(&draw_screen);
}

static bool on_event(screen_event_t ev)
{
    // 選類別是觸控的事，這個畫面沒有主要動作給短按
    if (ev == SCREEN_EV_BOOT_HOLD) {
        screen_mgr_goto(&shrine_screen);
        return true;
    }
    return false;
}

const screen_t tell_screen = {
    .name = "稟告",
    .enter = enter,
    .exit = NULL,
    .tick = tick,
    .on_event = on_event,
};

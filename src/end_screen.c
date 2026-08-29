// 禮畢。解籤閣讀完之後的收尾，停幾秒自己回正殿。
//
// 為什麼要有這一頁：儀式需要一個結束的動作，不然「讀完就被丟回正殿」
// 會讓人不確定剛才那次算不算完成。這裡順便把序號講出來——
// 紀錄已經寫進 NVS 了，這是使用者唯一會看到它的地方。
#include "end_screen.h"

#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

// 停留時間。夠看完兩行字，又不會讓人等到想按鍵
#define HOLD_US (2600LL * 1000)

static lv_obj_t *s_scr;
static lv_obj_t *s_seq;
static int64_t s_entered_at_us;

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x1E0A0A), 0);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(title, &font_zh_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD8B878), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "禮畢\n平安");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    s_seq = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_seq, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_seq, lv_color_hex(0x9A8060), 0);
    lv_obj_align(s_seq, LV_ALIGN_CENTER, 0, 60);
}

static esp_err_t enter(void)
{
    s_entered_at_us = esp_timer_get_time();

    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    // 紀錄寫不進去時就不提序號，不要顯示一個假的數字
    uint16_t seq = ritual_seq();
    if (seq) {
        lv_label_set_text_fmt(s_seq, "第 %u 次參拜", (unsigned)seq);
    } else {
        lv_label_set_text(s_seq, "");
    }
    lv_screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

static void tick(void)
{
    if (esp_timer_get_time() - s_entered_at_us >= HOLD_US) screen_mgr_goto(&shrine_screen);
}

// 等不及就按鍵或滑走，長按短按都認——這一頁沒有別的事可做
static bool on_event(screen_event_t ev)
{
    (void)ev;
    screen_mgr_goto(&shrine_screen);
    return true;
}

const screen_t end_screen = {
    .name = "禮畢",
    .enter = enter,
    .exit = NULL,
    .tick = tick,
    .on_event = on_event,
};

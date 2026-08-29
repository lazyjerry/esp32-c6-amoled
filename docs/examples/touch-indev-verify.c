// M1：觸控接成 LVGL indev，以及自寫的左右滑動判定。
//
// 要看三件事，缺一不可：
//   ① 座標對得上——手指按哪裡，圓點就在哪裡。方向錯了會看到圓點鏡射或 XY 對調
//   ② 點擊有進 LVGL——按鈕會亮、計數會加。這是「稟告」那類選單能不能做的前提
//   ③ 滑動判得出來，且滑過按鈕不會誤觸——橫掃時 TAP 計數必須不動
//
// 螢幕上的字刻意全用 ASCII：中文字型是子集產生的，驗證程式不值得為它回頭改字集。
//
// 實測結論（2026-08-29）：三項全通過。
//   ① 座標 x=11~346、y=10~425，對得上 368x448，不需縮放；XY 沒對調
//      （若對調，y 會來自 0~367 而碰不到 400 以上，但實測出現 y=425）；
//      綠點跟著手指，沒有鏡射
//   ② 20 次 CLICKED，座標全落在中央按鈕範圍內
//   ③ 13 次滑動（L=9 R=4）方向與位移正負一致；橫掃過按鈕時 TAP 計數不動，
//      lv_indev_reset() 確實取消了該次觸碰；另有 6 次斜拉被「橫移>縱移×2」擋掉
//
// 這支已從 src/ 歸檔。要重跑：複製回 src/、改回 #include "verify_touch.h"、
// 在 platformio.ini 加回 [env:verify-touch]（build_flags = -DAPP_VERIFY_TOUCH），
// 並在 main.c 的分岔加回 APP_VERIFY_TOUCH 分支。
#include "touch-indev-verify.h"

#include <inttypes.h>
#include <stdbool.h>

#include "board.h"
#include "cast_ui.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "touch.h"

#define DOT 24

static const char *TAG = "verify_touch";

static lv_obj_t *s_coord;
static lv_obj_t *s_dot;
static lv_obj_t *s_btn_label;
static lv_obj_t *s_swipe;

static uint32_t s_taps;
static uint32_t s_left, s_right;
static bool s_was_down;

static void on_click(lv_event_t *e)
{
    (void)e;
    s_taps++;
    ESP_LOGI(TAG, "② 點擊 #%" PRIu32, s_taps);
    lv_label_set_text_fmt(s_btn_label, "TAP %" PRIu32, s_taps);
}

// 直接跟 LVGL 要座標，而不是問 touch.c——要驗的正是「產品透過 LVGL 拿到的值」對不對
static void follow_finger(lv_timer_t *t)
{
    (void)t;
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    bool down = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    // 按下與放開各記一筆座標。只看螢幕的話要有人在場回報，記進 log 就能事後自己判方向
    if (down != s_was_down) {
        ESP_LOGI(TAG, "① %s x=%d y=%d", down ? "按下" : "放開", (int)p.x, (int)p.y);
        s_was_down = down;
    }

    lv_label_set_text_fmt(s_coord, "x=%3d y=%3d %s", (int)p.x, (int)p.y, down ? "DOWN" : "up");
    lv_obj_set_pos(s_dot, p.x - DOT / 2, p.y - DOT / 2);
    lv_obj_set_style_bg_opa(s_dot, down ? LV_OPA_COVER : LV_OPA_30, 0);
}

static void build_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101014), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_coord = lv_label_create(scr);
    lv_obj_set_style_text_color(s_coord, lv_color_hex(0xE0E0E0), 0);
    lv_label_set_text(s_coord, "touch the screen");
    lv_obj_align(s_coord, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 220, 110);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, NULL);
    s_btn_label = lv_label_create(btn);
    lv_label_set_text(s_btn_label, "TAP 0");
    lv_obj_center(s_btn_label);

    s_swipe = lv_label_create(scr);
    lv_obj_set_style_text_color(s_swipe, lv_color_hex(0xFFC060), 0);
    lv_label_set_text(s_swipe, "swipe: L=0 R=0");
    lv_obj_align(s_swipe, LV_ALIGN_BOTTOM_MID, 0, -40);

    // 圓點畫在最上層，蓋過按鈕才看得到指尖位置
    s_dot = lv_obj_create(scr);
    lv_obj_set_size(s_dot, DOT, DOT);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(0x30FF80), 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_dot, -DOT, -DOT);

    lv_screen_load(scr);
    lv_timer_create(follow_finger, 30, NULL);
}

void verify_touch_run(void)
{
    ESP_ERROR_CHECK(board_init());
    // 借產品的顯示設定把 LVGL 起起來，驗證環境才和實機一致（會先載入擲筊畫面，隨即被換掉）
    ESP_ERROR_CHECK(cast_ui_init());
    ESP_ERROR_CHECK(touch_init());

    lvgl_port_lock(0);
    build_screen();
    lvgl_port_unlock();

    ESP_LOGI(TAG, "面板 %dx%d：① 按四角看座標 ② 點按鈕看計數 ③ 橫掃看方向且 TAP 不動",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    while (1) {
        touch_swipe_t s = touch_take_swipe();
        if (s != TOUCH_SWIPE_NONE) {
            if (s == TOUCH_SWIPE_LEFT) s_left++;
            else s_right++;
            ESP_LOGI(TAG, "③ 滑動 %s（L=%" PRIu32 " R=%" PRIu32 "，TAP 應維持 %" PRIu32 "）",
                     s == TOUCH_SWIPE_LEFT ? "LEFT" : "RIGHT", s_left, s_right, s_taps);
            lvgl_port_lock(0);
            lv_label_set_text_fmt(s_swipe, "swipe: L=%" PRIu32 " R=%" PRIu32, s_left, s_right);
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

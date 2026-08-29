// 語料讀不到時的畫面。
//
// 刻意做成死路：不接受任何輸入，也不退回擲筊。開機期就缺內容代表燒錄沒完成，
// 這時讓裝置「看起來能用」只會把問題留到使用者求到籤卻沒有籤詩的那一刻。
// PWR 鍵仍然關得掉機——那條路在 main.c 全域攔截，不經過畫面。
#include "error_screen.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

LV_FONT_DECLARE(font_zh_16)
LV_FONT_DECLARE(font_zh_28)

static const char *s_detail = "";
static lv_obj_t *s_scr;

void error_screen_set(const char *detail)
{
    if (detail) s_detail = detail;
}

static esp_err_t enter(void)
{
    lvgl_port_lock(0);

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x1A0E0E), 0);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(title, &font_zh_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0B070), 0);
    lv_label_set_text(title, "語料未燒錄");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *how = lv_label_create(s_scr);
    lv_obj_set_style_text_font(how, &font_zh_16, 0);
    lv_obj_set_style_text_color(how, lv_color_hex(0xB0A090), 0);
    lv_label_set_text(how, "接上 USB 執行\n\n  gen-content.sh\n  flash-content.sh");
    lv_obj_set_style_text_align(how, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(how, LV_ALIGN_CENTER, 0, 20);

    // 錯誤碼用 ASCII，子集字型含 0x20~0x7E
    lv_obj_t *code = lv_label_create(s_scr);
    lv_obj_set_style_text_font(code, &font_zh_16, 0);
    lv_obj_set_style_text_color(code, lv_color_hex(0x706050), 0);
    lv_label_set_text(code, s_detail);
    lv_obj_align(code, LV_ALIGN_BOTTOM_MID, 0, -30);

    screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

const screen_t error_screen = {
    .name = "語料錯誤",
    .enter = enter,
    .exit = NULL,
    .tick = NULL,
    .on_event = NULL,
};

#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

LV_FONT_DECLARE(font_zh_16)
LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_num_56)
LV_FONT_DECLARE(font_icon_72)

// 面板 368 寬、每列 2 bytes；32 列約 23KB，雙緩衝 46KB。
// 再大會和 TLS handshake 搶記憶體。
#define DRAW_BUF_ROWS 32
#define QR_SIZE       208

static const char *TAG = "ui";

static lv_obj_t *s_weather_scr;
static lv_obj_t *s_prov_scr;

static lv_obj_t *s_status;
static lv_obj_t *s_icon;
static lv_obj_t *s_temp;
static lv_obj_t *s_cond;
static lv_obj_t *s_detail;
static lv_obj_t *s_updated;

static lv_obj_t *s_qr;
static lv_obj_t *s_prov_ssid;
static lv_obj_t *s_prov_pop;

// SH8601 要求繪圖區起點為偶數、終點為奇數，否則整片畫面會歪掉。
// LVGL 9 取消了 v8 的 rounder_cb，改在 INVALIDATE_AREA 事件裡調整。
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    // AMOLED 黑像素不發光，底色用純黑同時省電
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    return scr;
}

static void build_weather_screen(void)
{
    s_weather_scr = make_screen();

    lv_obj_t *place = make_label(s_weather_scr, &font_zh_28, lv_color_white());
    lv_label_set_text(place, APP_LOCATION_NAME);
    lv_obj_align(place, LV_ALIGN_TOP_LEFT, 16, 12);

    s_status = make_label(s_weather_scr, &font_zh_16, lv_color_hex(0x9AA0A6));
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -16, 20);

    s_icon = make_label(s_weather_scr, &font_icon_72, lv_color_hex(0xFFC44D));
    lv_label_set_text(s_icon, "");
    lv_obj_align(s_icon, LV_ALIGN_TOP_MID, 0, 72);

    s_temp = make_label(s_weather_scr, &font_num_56, lv_color_white());
    lv_label_set_text(s_temp, "--");
    lv_obj_align(s_temp, LV_ALIGN_TOP_MID, 0, 178);

    s_cond = make_label(s_weather_scr, &font_zh_28, lv_color_hex(0xC8CBD0));
    lv_label_set_text(s_cond, "");
    lv_obj_align(s_cond, LV_ALIGN_TOP_MID, 0, 254);

    s_detail = make_label(s_weather_scr, &font_zh_16, lv_color_hex(0x9AA0A6));
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail, BOARD_LCD_H_RES - 32);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_detail, "");
    lv_obj_align(s_detail, LV_ALIGN_TOP_MID, 0, 316);

    s_updated = make_label(s_weather_scr, &font_zh_16, lv_color_hex(0x5F6368));
    lv_label_set_text(s_updated, "");
    lv_obj_align(s_updated, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static void build_prov_screen(void)
{
    s_prov_scr = make_screen();

    lv_obj_t *title = make_label(s_prov_scr, &font_zh_28, lv_color_white());
    lv_label_set_text(title, "設定 WiFi");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *hint = make_label(s_prov_scr, &font_zh_16, lv_color_hex(0x9AA0A6));
    lv_label_set_text(hint, "手機掃描 QR 配網");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 62);

    s_qr = lv_qrcode_create(s_prov_scr);
    lv_qrcode_set_size(s_qr, QR_SIZE);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    // QR 需要靜區，否則多數掃描器讀不到
    lv_obj_set_style_border_color(s_qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_qr, 8, 0);
    lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 96);

    s_prov_ssid = make_label(s_prov_scr, &font_zh_16, lv_color_hex(0xC8CBD0));
    lv_label_set_text(s_prov_ssid, "");
    lv_obj_align(s_prov_ssid, LV_ALIGN_BOTTOM_MID, 0, -52);

    s_prov_pop = make_label(s_prov_scr, &font_zh_16, lv_color_hex(0xC8CBD0));
    lv_label_set_text(s_prov_pop, "");
    lv_obj_align(s_prov_pop, LV_ALIGN_BOTTOM_MID, 0, -24);
}

esp_err_t ui_init(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = board_panel_io(),
        .panel_handle = board_panel(),
        .buffer_size = BOARD_LCD_H_RES * DRAW_BUF_ROWS,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            // 面板吃的是 byte-swap 過的 RGB565（紅是 0x00F8 不是 0xF800）
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "add disp");

    lv_display_add_event_cb(disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lvgl_port_lock(0);
    build_weather_screen();
    build_prov_screen();
    lv_screen_load(s_weather_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

void ui_set_status(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_status, text);
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -16, 20);
    lvgl_port_unlock();
}

void ui_show_weather(const weather_t *w)
{
    char temp[24], detail[96], updated[32];

    snprintf(temp, sizeof(temp), "%.1f°C", w->temp_c);
    snprintf(detail, sizeof(detail), "體感 %.0f°C    濕度 %d%%    風速 %.0f km/h",
             w->feels_c, w->humidity, w->wind_kmh);

    // Open-Meteo 回的是 "2026-08-09T11:00"，只取後面的時間
    const char *t = strchr(w->observed_at, 'T');
    snprintf(updated, sizeof(updated), "更新 %s", t ? t + 1 : "--:--");

    lvgl_port_lock(0);
    lv_label_set_text(s_icon, weather_code_symbol(w->wmo_code));
    lv_label_set_text(s_temp, temp);
    lv_label_set_text(s_cond, weather_code_text(w->wmo_code));
    lv_label_set_text(s_detail, detail);
    lv_label_set_text(s_updated, updated);
    lv_obj_align(s_icon, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_align(s_temp, LV_ALIGN_TOP_MID, 0, 178);
    lv_obj_align(s_cond, LV_ALIGN_TOP_MID, 0, 254);
    lv_obj_align(s_updated, LV_ALIGN_BOTTOM_MID, 0, -20);
    lvgl_port_unlock();
}

void ui_show_provisioning(const char *ap_ssid, const char *pop, const char *qr_payload)
{
    char ssid_line[64], pop_line[48];
    snprintf(ssid_line, sizeof(ssid_line), "熱點 %s", ap_ssid);
    snprintf(pop_line, sizeof(pop_line), "密碼 %s", pop);

    lvgl_port_lock(0);
    lv_qrcode_update(s_qr, qr_payload, strlen(qr_payload));
    lv_label_set_text(s_prov_ssid, ssid_line);
    lv_label_set_text(s_prov_pop, pop_line);
    lv_obj_align(s_prov_ssid, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_align(s_prov_pop, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_screen_load(s_prov_scr);
    lvgl_port_unlock();
}

void ui_show_weather_screen(void)
{
    lvgl_port_lock(0);
    lv_screen_load(s_weather_scr);
    lvgl_port_unlock();
}

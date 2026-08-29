#include "display.h"

#include "board.h"
#include "esp_check.h"
#include "esp_lvgl_port.h"

// 天氣看板受限於 TLS 用 32 列；這個產品沒有網路，加倍換每幀更少次 flush
#define DRAW_BUF_ROWS 64

static const char *TAG = "display";

// SH8601 要求繪圖區起點偶數、終點奇數，否則整片畫面會歪掉。
// LVGL 9 取消了 v8 的 rounder_cb，改在 INVALIDATE_AREA 事件裡調整
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

esp_err_t display_init(void)
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
    return ESP_OK;
}

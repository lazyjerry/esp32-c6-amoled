#pragma once

#include "esp_err.h"
#include "weather.h"

esp_err_t ui_init(void);

// 以下都可從任意 task 呼叫，內部自己取 LVGL 鎖
void ui_set_status(const char *text);
void ui_show_weather(const weather_t *w);

// 配網畫面：QR 給 ESP SoftAP Prov App 掃，底下附熱點名稱與 PoP 供手動輸入
void ui_show_provisioning(const char *ap_ssid, const char *pop, const char *qr_payload);

// 切回天氣畫面（配網完成後）
void ui_show_weather_screen(void);

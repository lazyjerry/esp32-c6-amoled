#pragma once

#include "esp_err.h"

typedef struct {
    float temp_c;
    float feels_c;
    float wind_kmh;
    int humidity;
    int wmo_code;
    char observed_at[24];   // Open-Meteo 回的當地時間 "2026-08-09T11:00"
} weather_t;

// 阻塞式 HTTPS GET，呼叫前要先確定網路已連上
esp_err_t weather_fetch(weather_t *out);

// WMO weather code 轉中文；未知碼回「無資料」
const char *weather_code_text(int wmo_code);

// 對應的 LVGL symbol 圖示
const char *weather_code_symbol(int wmo_code);

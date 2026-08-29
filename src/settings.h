// 音量與亮度：使用者調過就記在 NVS，下次開機沿用。
//
// 這裡只保管數值與持久化，不負責套用——亮度要寫面板暫存器、音量要進 codec，
// 兩者都得在拿得到 LVGL 鎖的地方做，那是呼叫端才知道的事。
#pragma once

#include <stdint.h>

#include "esp_err.h"

#define SETTINGS_VOL_DEFAULT    75
#define SETTINGS_BRIGHT_DEFAULT 255
// 亮度下限。0 是全黑，調到看不見就沒有辦法再調回來了
#define SETTINGS_BRIGHT_MIN     30

esp_err_t settings_init(void);

uint8_t settings_volume(void);      // 0~100
uint8_t settings_brightness(void);  // SETTINGS_BRIGHT_MIN~255

// 存進 NVS。拖動滑桿時不要每一格都呼叫，放開再存
esp_err_t settings_set_volume(uint8_t percent);
esp_err_t settings_set_brightness(uint8_t level);

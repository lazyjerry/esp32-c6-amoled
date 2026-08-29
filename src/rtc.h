// PCF85063 即時時鐘（I2C 0x51）。
//
// 這顆掛在板上的紐扣電池／超級電容上，理論上 CPU 斷電後仍會走時。
// 「AXP2101 軟關機之後時間還在不在」是 M0/S2 要驗的事——結果決定參拜簿能不能存日期。
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

esp_err_t rtc_init(void);

// 讀出目前時間。tm_wday 由晶片提供，tm_yday / tm_isdst 不填
esp_err_t rtc_get_time(struct tm *out);

esp_err_t rtc_set_time(const struct tm *t);

// 振盪器停過就代表時間不可信（掉電或首次上電）。
// PCF85063 用秒暫存器的 bit7 記這件事，讀到 true 時 rtc_get_time 的結果沒有意義
esp_err_t rtc_oscillator_stopped(bool *stopped);

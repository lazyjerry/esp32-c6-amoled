// 參拜紀錄：存在 NVS，跨軟關機保留。
//
// 刻意不存日期。這片板子的 RTC 保時沒有驗過也決定不驗（企劃 §8 S2），
// 而使用模型是「開機 → 走完一輪 → 軟關機」，一次開機就是一次參拜，
// 用遞增序號表達「第幾次」就夠了，不需要知道那是哪一天。
#pragma once

#include <stdint.h>

#include "esp_err.h"

// 保留最近幾筆。參拜簿是「翻翻最近拜過什麼」，不是完整帳本，
// 存滿整個 NVS 分區只會換來更長的捲動
#define RECORDS_MAX 20

typedef struct {
    uint16_t seq;    // 第幾次參拜，從 1 開始
    uint8_t cat;     // ritual_cat_t 的序號
    uint8_t poem;    // 籤號
} record_t;

// 開 NVS。失敗時參拜仍然走得完，只是留不下紀錄
esp_err_t records_init(void);

// 累計參拜次數。含已經被環形覆蓋掉的那些
uint32_t records_total(void);

// 記一筆，回傳這次的序號。out_seq 可為 NULL
esp_err_t records_add(int cat, int poem, uint16_t *out_seq);

// 取最近的幾筆，新的在前。回傳實際填入的筆數
int records_recent(record_t *out, int max);

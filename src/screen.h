// 畫面介面。每個畫面實作這組函式，由 screen_mgr 持有目前畫面並轉發事件。
//
// 為什麼要這層：現行 main.c 是一支 while(1) 直接管擲筊的狀態，
// 加到企劃規劃的 7 個畫面之後，狀態組合會爆炸。把「誰在前景」與「誰處理輸入」分開，
// 每個畫面只管自己的事，main.c 退回成單純的輸入分派。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    SCREEN_EV_BOOT_KEY,   // BOOT 鍵短按
    SCREEN_EV_SHAKE,      // IMU 上下甩
    SCREEN_EV_SWIPE,      // IMU 左右晃
} screen_event_t;

typedef struct screen_s {
    const char *name;

    // 進入畫面時呼叫。建立／載入這個畫面的 LVGL 物件
    esp_err_t (*enter)(void);

    // 離開畫面時呼叫，可為 NULL
    void (*exit)(void);

    // 主迴圈每輪呼叫一次，處理動畫狀態、冷卻計時之類的事，可為 NULL
    void (*tick)(void);

    // 收到輸入事件。回傳 true 表示已處理，false 讓 screen_mgr 交給預設行為
    bool (*on_event)(screen_event_t ev);
} screen_t;

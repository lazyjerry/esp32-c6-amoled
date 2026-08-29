// 畫面介面。每個畫面實作這組函式，由 screen_mgr 持有目前畫面並轉發事件。
//
// 為什麼要這層：現行 main.c 是一支 while(1) 直接管擲筊的狀態，
// 加到企劃規劃的 7 個畫面之後，狀態組合會爆炸。把「誰在前景」與「誰處理輸入」分開，
// 每個畫面只管自己的事，main.c 退回成單純的輸入分派。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// BOOT 的語意固定成兩條，跨畫面一致：
//   短按 = 這個畫面的主要動作（沒有主要動作的畫面就不接）
//   長按 = 離開，回正殿
// 原本半數畫面把短按當返回、半數當功能鍵，同一顆鍵在不同畫面做相反的事，記不住
typedef enum {
    SCREEN_EV_BOOT_KEY,      // BOOT 鍵短按
    SCREEN_EV_BOOT_HOLD,     // BOOT 鍵長按。按滿就發，不等放開
    SCREEN_EV_SHAKE,         // IMU 上下甩
    SCREEN_EV_WAVE,          // IMU 左右晃。刻意不叫 swipe，那是手指的動作
    SCREEN_EV_SWIPE_LEFT,    // 手指往左掃
    SCREEN_EV_SWIPE_RIGHT,
} screen_event_t;

// 把畫面推上前景，帶統一的淡入轉場。各畫面的 enter() 用這支，
// 不要直接 lv_screen_load()——轉場的型態與長短是全域一致的事，散在十個畫面裡會走樣。
// 要在 LVGL 鎖內呼叫（enter() 本來就是）
struct _lv_obj_t;
void screen_load(struct _lv_obj_t *scr);

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

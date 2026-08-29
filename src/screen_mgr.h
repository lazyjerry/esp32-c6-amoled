// 畫面管理器：持有目前畫面，轉發輸入事件與 tick。
//
// 刻意不做畫面堆疊（返回上一頁）——企劃的導覽是「一條主軸 + 兩個側翼」，
// 深層流程只能循序前進或按 BOOT 返回，用不到堆疊。需要時再加。
#pragma once

#include "screen.h"

esp_err_t screen_mgr_init(const screen_t *initial);

// 切到另一個畫面。會先呼叫舊畫面的 exit()，再呼叫新畫面的 enter()。
// 切到目前這個畫面是無操作，不會重跑 enter()
esp_err_t screen_mgr_goto(const screen_t *next);

void screen_mgr_tick(void);

// 把事件交給目前畫面。回傳畫面是否處理了它
bool screen_mgr_dispatch(screen_event_t ev);

const screen_t *screen_mgr_current(void);

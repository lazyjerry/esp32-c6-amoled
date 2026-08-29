// 顯示層：把 LVGL 與面板接起來。與任何單一畫面無關。
//
// 原本這段埋在 cast_ui_init() 裡，等於「LVGL 要靠擲筊畫面才起得來」。
// 加入錯誤畫面之後這行不通了——掛載失敗時根本不該建立擲筊畫面。
#pragma once

#include "esp_err.h"

// 要在 board_init() 之後呼叫。回來之後就可以建立 LVGL 物件與 indev
esp_err_t display_init(void);

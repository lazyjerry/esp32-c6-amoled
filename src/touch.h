// 觸控：裸讀 0x38 的座標，接成 LVGL 的指標輸入，並自行判定左右滑動。
//
// 這顆只是 I2C 位址與 FT5x06 相同，暫存器語意不同，官方元件的 init 會把它寫到不回應 I2C
// （docs/notes/touch-not-real-ft5x06.md），所以不用 esp_lcd_touch，也沒有現成的手勢引擎。
#pragma once

#include "esp_err.h"

typedef enum {
    TOUCH_SWIPE_NONE = 0,
    TOUCH_SWIPE_LEFT,    // 手指往左掃
    TOUCH_SWIPE_RIGHT,
} touch_swipe_t;

// 要在 LVGL 起來之後呼叫：indev 建立時會綁到當下的預設 display
esp_err_t touch_init(void);

// 取走一次已判定的滑動，取走即清除。沒有就回 TOUCH_SWIPE_NONE
touch_swipe_t touch_take_swipe(void);

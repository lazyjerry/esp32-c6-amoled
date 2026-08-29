// 擲筊畫面：待機提示、拋擲動畫、結果特寫
#pragma once

#include <stdbool.h>

#include "cast.h"
#include "esp_err.h"

esp_err_t cast_ui_init(void);

// 把這個畫面推上前景。由 cast_screen 的 enter() 呼叫
void cast_ui_show(void);

// 播一次擲筊動畫。鏡頭拉近後就停在該筊象上，不會自己收掉
void cast_ui_play(cast_result_t r);

bool cast_ui_busy(void);      // 動畫還在跑
bool cast_ui_holding(void);   // 結果停在畫面上，等人來收

void cast_ui_reset(void);     // 收掉結果回待機。提示要等 cast_ui_show_hint() 才出現
void cast_ui_show_hint(void); // 冷卻結束，可以再擲了

// 待機提示的兩行字。求籤後的擲筊確認要換一組說法，傳 NULL 恢復預設
void cast_ui_set_prompt(const char *big, const char *small);

// 結果停住時在下方加一行字，說明接下來會怎樣。傳 NULL 或空字串就不顯示。
// 自由擲筊刻意不用它——那個畫面只有放大的筊，沒有文字
void cast_ui_show_note(const char *text);

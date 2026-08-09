// 擲筊畫面：待機提示、拋擲動畫、結果特寫
#pragma once

#include <stdbool.h>

#include "cast.h"
#include "esp_err.h"

esp_err_t cast_ui_init(void);

// 播一次擲筊動畫並停在 r 的筊象。動畫跑完會自己回待機畫面
void cast_ui_play(cast_result_t r);

bool cast_ui_busy(void);

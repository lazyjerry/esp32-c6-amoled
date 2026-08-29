// 開機期致命錯誤的告知畫面。目前唯一的來源是語料掛載失敗。
#pragma once

#include "screen.h"

// 要在 screen_mgr 切進來之前設定，detail 只存指標，必須是常駐字串
void error_screen_set(const char *detail);

extern const screen_t error_screen;

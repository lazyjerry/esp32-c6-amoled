// 解籤閣：顯示求到的那首籤，可上下捲動閱讀。
#pragma once

#include "screen.h"

extern const screen_t reading_screen;

// 從參拜簿翻閱指定的籤。與儀式無關，離開時回參拜簿而不是禮畢頁
void reading_screen_browse(int poem_no, int cat);

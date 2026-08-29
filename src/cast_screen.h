// 擲筊畫面：把原本散在 main.c 主迴圈裡的擲筊狀態機包成一個 screen。
//
// 兩種模式：
//   自由擲筊（預設）——從正殿按 BOOT 進來，擲完收掉就留在原地
//   擲筊確認——求籤後進來，收掉結果時依筊象決定去哪：聖筊進解籤、笑筊重擲、陰筊回求籤
#pragma once

#include "screen.h"

extern const screen_t cast_screen;

// 切成「擲筊確認」模式。要在 screen_mgr_goto(&cast_screen) 之前呼叫；
// 離開這個畫面時會自己切回自由擲筊
void cast_screen_confirm(void);

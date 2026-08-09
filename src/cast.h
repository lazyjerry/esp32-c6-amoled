// 擲筊結果與對應文字
#pragma once

typedef enum {
    CAST_SHENG,   // 聖筊：一平一凸
    CAST_XIAO,    // 笑筊：兩片平面朝上
    CAST_YIN,     // 陰筊：兩片平面朝下
    CAST_LI,      // 立筊：一片立起來
} cast_result_t;

cast_result_t cast_draw(void);

// 畫面上只呈現筊象本身，這個名稱目前只用在序列埠 log
const char *cast_result_name(cast_result_t r);

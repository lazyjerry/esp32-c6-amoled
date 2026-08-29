// 一次參拜的過程狀態：稟告了什麼、求到第幾籤。
//
// 這些值跨越好幾個畫面（稟告 → 求籤 → 擲筊確認 → 解籤閣），
// 放在畫面裡就得互相 include 對方去拿，改成一份共用狀態，畫面之間彼此不認識。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 稟告的類別。順序就是選單上的排列順序
typedef enum {
    RITUAL_CAT_NONE = -1,
    RITUAL_CAT_CAREER = 0,   // 事業
    RITUAL_CAT_LOVE,         // 姻緣
    RITUAL_CAT_WEALTH,       // 財運
    RITUAL_CAT_HEALTH,       // 健康
    RITUAL_CAT_STUDY,        // 學業
    RITUAL_CAT_HOME,         // 家宅
    RITUAL_CAT_COUNT,
} ritual_cat_t;

const char *ritual_cat_name(ritual_cat_t c);

// 從正殿進入儀式時清空
void ritual_begin(void);

void ritual_set_category(ritual_cat_t c);
ritual_cat_t ritual_category(void);

void ritual_set_poem(int no);
int ritual_poem(void);   // 0 表示還沒求籤

// 神明應允（聖筊）時把這一次寫進參拜紀錄，回傳是第幾次。
// 呼叫的人不必認識 records——擲筊畫面只知道「神明允了」這件事
uint16_t ritual_commit(void);
uint16_t ritual_seq(void);   // 這一次的序號，0 表示還沒記下來

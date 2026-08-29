// 參拜紀錄：存在 NVS，跨軟關機保留。
//
// 刻意不存日期。這片板子的 RTC 保時沒有驗過也決定不驗（企劃 §8 S2），
// 而使用模型是「開機 → 走完一輪 → 軟關機」，一次開機就是一次參拜，
// 用遞增序號表達「第幾次」就夠了，不需要知道那是哪一天。
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "ritual.h"

// 保留最近幾筆。參拜簿是「翻翻最近拜過什麼」，不是完整帳本，
// 存滿整個 NVS 分區只會換來更長的捲動
#define RECORDS_MAX 20

typedef struct {
    uint16_t seq;    // 第幾次參拜，從 1 開始
    uint8_t cat;     // ritual_cat_t 的序號
    uint8_t poem;    // 籤號
} record_t;

// 籤號上限。六十甲子籤實際 63 首，多留一格讓索引直接用籤號
#define RECORDS_POEM_MAX 64

// 累計統計。全部只增不減，環形被覆蓋掉的那些也還算在裡面
typedef struct {
    uint32_t worships;                  // 完成的參拜次數（走到神明應允）
    uint32_t casts;                     // 擲筊次數，含從正殿直接進去的自由擲筊
    uint32_t sheng, xiao, yin, li;      // 四種筊象各自的次數
    uint32_t tells[RITUAL_CAT_COUNT];   // 各類別稟告了幾次
} stats_t;

// 求中的籤：所問類別 + 籤號 + 次數
typedef struct {
    uint8_t cat;
    uint8_t poem;
    uint16_t count;
} poem_stat_t;

// 開 NVS。失敗時參拜仍然走得完，只是留不下紀錄
esp_err_t records_init(void);

// 累計參拜次數。含已經被環形覆蓋掉的那些
uint32_t records_total(void);

// 記一筆，回傳這次的序號。out_seq 可為 NULL
esp_err_t records_add(int cat, int poem, uint16_t *out_seq);

// 取最近的幾筆，新的在前。回傳實際填入的筆數
int records_recent(record_t *out, int max);

const stats_t *records_stats(void);

// 計數。都只改記憶體並標記待寫入，實際寫 NVS 等 records_flush()——
// 擲筊當下正在跑動畫，不該為了記帳去等 flash
void records_count_cast(int result);   // cast_result_t
void records_count_tell(int cat);

// 把待寫入的計數落地。挑沒有動畫在跑的時機呼叫
esp_err_t records_flush(void);

// 最近求中的籤，新的在前。同一支籤重複求中只留最新那一次的位置，
// 次數仍是全歷史累計。順序來自最近 RECORDS_MAX 筆參拜紀錄——
// 更早求中過的籤不在這份清單裡，那是「最近」這個詞的代價
int records_recent_poems(poem_stat_t *out, int max);

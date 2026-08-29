#include "ritual.h"

#include "records.h"

static const char *const CAT_NAMES[RITUAL_CAT_COUNT] = {
    "事業", "姻緣", "財運", "健康", "學業", "家宅",
};

static ritual_cat_t s_cat = RITUAL_CAT_NONE;
static int s_poem;
static uint16_t s_seq;

const char *ritual_cat_name(ritual_cat_t c)
{
    if (c < 0 || c >= RITUAL_CAT_COUNT) return "";
    return CAT_NAMES[c];
}

void ritual_begin(void)
{
    s_cat = RITUAL_CAT_NONE;
    s_poem = 0;
    s_seq = 0;
}

void ritual_set_category(ritual_cat_t c) { s_cat = c; }
ritual_cat_t ritual_category(void) { return s_cat; }

void ritual_set_poem(int no) { s_poem = no; }
int ritual_poem(void) { return s_poem; }

// 寫不進去就當作沒有這次紀錄。參拜本身已經完成，不該為了記帳把人擋在流程外
uint16_t ritual_commit(void)
{
    if (s_seq) return s_seq;   // 同一次參拜只記一筆
    if (records_add(s_cat, s_poem, &s_seq) != ESP_OK) s_seq = 0;
    return s_seq;
}

uint16_t ritual_seq(void) { return s_seq; }

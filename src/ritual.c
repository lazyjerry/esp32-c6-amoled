#include "ritual.h"

static const char *const CAT_NAMES[RITUAL_CAT_COUNT] = {
    "事業", "姻緣", "財運", "健康", "學業", "家宅",
};

static ritual_cat_t s_cat = RITUAL_CAT_NONE;
static int s_poem;

const char *ritual_cat_name(ritual_cat_t c)
{
    if (c < 0 || c >= RITUAL_CAT_COUNT) return "";
    return CAT_NAMES[c];
}

void ritual_begin(void)
{
    s_cat = RITUAL_CAT_NONE;
    s_poem = 0;
}

void ritual_set_category(ritual_cat_t c) { s_cat = c; }
ritual_cat_t ritual_category(void) { return s_cat; }

void ritual_set_poem(int no) { s_poem = no; }
int ritual_poem(void) { return s_poem; }

// 參拜簿（正殿左側）：累計統計，加上求中過的籤——點一列就翻到那首。
//
// 刻意沒有日期——紀錄本身就不存日期，理由見 records.h。
// 統計全部從 records 的累計值來，不是從最近 20 筆算的：那 20 筆會被環形覆蓋掉，
// 拿它當統計來源會讓數字隨著參拜次數變多而愈來愈小。
//
// 每次進來重建整頁：筆數少，重建的成本遠低於「畫面留著舊資料」的除錯成本。
#include "records_screen.h"

#include <stdarg.h>
#include <stdio.h>

#include "cast.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "reading_screen.h"
#include "records.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

static const char *TAG = "records-ui";

#define PAD 20
// 求中的籤最多列幾支。超過就是次數最少的那些看不到，
// 但一頁捲到底本來就有極限，列太多只是把有用的資訊推到看不見的地方
#define POEM_ROWS 24

static lv_obj_t *s_scr;
static lv_obj_t *s_body;

// 點擊在 LVGL 任務裡發生，切畫面留給主迴圈
static volatile int s_open_poem;
static volatile int s_open_cat;

static void on_poem(lv_event_t *e)
{
    uint32_t packed = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    s_open_cat = (int)(packed >> 8);
    s_open_poem = (int)(packed & 0xFF);
}

static lv_obj_t *make_label(const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(s_body);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

// 小節標題：把一頁數字切成看得懂的幾段
static void section(const char *name)
{
    lv_obj_t *l = make_label(&font_zh_16, 0x8A6A40);
    lv_label_set_text(l, name);
    lv_obj_set_style_pad_top(l, 8, 0);
}

// LVGL 的 set_text_fmt 是 printf 風格的可變參數，沒有吃 va_list 的版本，
// 所以自己先格式化到 buffer 再交出去
static void line(const char *fmt, ...)
{
    char buf[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    lv_obj_t *l = make_label(&font_zh_16, 0xC0B098);
    lv_label_set_text(l, buf);
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x140808), 0);
    lv_obj_set_style_pad_all(s_scr, PAD, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_flex_flow(s_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_scr, 8, 0);
    lv_obj_set_scroll_dir(s_scr, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(title, &font_zh_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xC0A060), 0);
    lv_label_set_text(title, "參拜簿");

    // 內容自己一層，重建時只清這裡，標題不動
    s_body = lv_obj_create(s_scr);
    lv_obj_set_width(s_body, lv_pct(100));
    lv_obj_set_height(s_body, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_pad_all(s_body, 0, 0);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
}

static void fill_poems(void)
{
    poem_stat_t items[POEM_ROWS];
    int n = records_poem_stats(items, POEM_ROWS);
    if (n == 0) return;

    section("求中的籤");
    for (int i = 0; i < n; i++) {
        lv_obj_t *btn = lv_button_create(s_body);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A1410), 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x6A4A28), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_pad_all(btn, 8, 0);
        // 類別與籤號塞進一個整數，省掉一份要跟著畫面活著的陣列
        uint32_t packed = ((uint32_t)items[i].cat << 8) | items[i].poem;
        lv_obj_add_event_cb(btn, on_poem, LV_EVENT_CLICKED, (void *)(uintptr_t)packed);

        lv_obj_t *l = lv_label_create(btn);
        lv_obj_set_style_text_font(l, &font_zh_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xD8C8A8), 0);
        lv_label_set_text_fmt(l, "%s　第 %u 籤　%u 次",
                              ritual_cat_name((ritual_cat_t)items[i].cat),
                              (unsigned)items[i].poem, (unsigned)items[i].count);
        lv_obj_center(l);
    }
}

static void fill_body(void)
{
    lv_obj_clean(s_body);

    const stats_t *st = records_stats();
    ESP_LOGI(TAG, "統計：參拜 %u、擲筊 %u、聖 %u 笑 %u 陰 %u 立 %u",
             (unsigned)st->worships, (unsigned)st->casts, (unsigned)st->sheng,
             (unsigned)st->xiao, (unsigned)st->yin, (unsigned)st->li);

    if (st->worships == 0 && st->casts == 0) {
        lv_obj_t *empty = make_label(&font_zh_16, 0x6A5A48);
        lv_label_set_text(empty, "尚無紀錄");
        return;
    }

    section("累計");
    line("參拜 %u 次", (unsigned)st->worships);
    line("擲筊 %u 次", (unsigned)st->casts);
    line("聖筊 %u　笑筊 %u　陰筊 %u", (unsigned)st->sheng, (unsigned)st->xiao,
         (unsigned)st->yin);
    // 立筊十萬分之一。沒擲出過就不必為它留一行
    if (st->li) line("立筊 %u", (unsigned)st->li);

    section("稟告");
    for (int c = 0; c < RITUAL_CAT_COUNT; c++) {
        if (!st->tells[c]) continue;
        line("%s %u 次", ritual_cat_name((ritual_cat_t)c), (unsigned)st->tells[c]);
    }

    fill_poems();
}

static esp_err_t enter(void)
{
    s_open_poem = 0;
    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    fill_body();
    lv_obj_scroll_to_y(s_scr, 0, LV_ANIM_OFF);
    screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

static void tick(void)
{
    if (!s_open_poem) return;
    reading_screen_browse(s_open_poem, s_open_cat);
    s_open_poem = 0;
    screen_mgr_goto(&reading_screen);
}

static bool on_event(screen_event_t ev)
{
    switch (ev) {
    case SCREEN_EV_BOOT_HOLD:
    case SCREEN_EV_SWIPE_LEFT:
    case SCREEN_EV_SWIPE_RIGHT:
        screen_mgr_goto(&shrine_screen);
        return true;
    case SCREEN_EV_BOOT_KEY:
    case SCREEN_EV_SHAKE:
    case SCREEN_EV_WAVE:
        return false;
    }
    return false;
}

const screen_t records_screen = {
    .name = "參拜簿",
    .enter = enter,
    .exit = NULL,
    .tick = tick,
    .on_event = on_event,
};

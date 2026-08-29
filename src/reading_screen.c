// 解籤閣。從 SPIFFS 讀出這次求到的籤，排成可捲動的一頁。
//
// 籤號、干支、卦象、屬性方位、籤詩本文屬公有領域的籤譜資料。
// 白話解讀是**本專案自行撰寫**的（data/readings.json），依籤詩意象寫成，分批補齊；
// 還沒寫到的籤就只顯示籤詩本文。來源網站的語譯與籤意是該站著作，一律不收錄。
//
// 捲動走 LVGL 原生的：容器設成可捲動，觸控 indev 的連續座標就會帶著它捲。
// 左右滑動不接——閱讀時橫向手抖很容易誤觸。讀完按「禮畢」走收尾頁，
// 長按 BOOT 則是中途離開，直接回正殿、不經過禮畢。
#include "reading_screen.h"

#include "content.h"
#include "end_screen.h"
#include "records_screen.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

#define PAD 24
// 段與段之間的距離。LVGL 的 flex 預設 pad_row 對這種純文字頁太鬆，一頁塞不了幾行
#define GAP 10

static const char *TAG = "reading";

static lv_obj_t *s_scr;
static lv_obj_t *s_head;
static lv_obj_t *s_meta;
static lv_obj_t *s_text;
static lv_obj_t *s_cat;
static lv_obj_t *s_reading;
static lv_obj_t *s_btn_label;

static volatile bool s_leave;

// 翻閱模式：籤號與類別由參拜簿指定，不看這一次參拜的狀態
static bool s_browse;
static int s_browse_poem;
static int s_browse_cat;

void reading_screen_browse(int poem_no, int cat)
{
    s_browse = true;
    s_browse_poem = poem_no;
    s_browse_cat = cat;
}

// 籤詩本文在語料裡是用全形空白分隔的四句。一句一行才是籤詩的樣子，
// 交給 LVGL 自動折行會斷在句子中間
static void poem_lines(const char *src, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = src; *p && o + 4 < cap; ) {
        if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 &&
            (unsigned char)p[2] == 0x80) {   // U+3000 全形空白
            out[o++] = '\n';
            p += 3;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
}

static void on_leave(lv_event_t *e)
{
    (void)e;
    s_leave = true;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(l, 4, 0);
    return l;
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x140808), 0);
    lv_obj_set_style_pad_all(s_scr, PAD, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    // 內容比一頁高就靠觸控捲動
    lv_obj_set_flex_flow(s_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_scr, GAP, 0);
    lv_obj_set_scroll_dir(s_scr, LV_DIR_VER);

    s_head = make_label(s_scr, &font_zh_28, 0xE8C880);
    s_cat  = make_label(s_scr, &font_zh_16, 0x9A8060);
    s_meta = make_label(s_scr, &font_zh_16, 0x8A7458);
    s_text = make_label(s_scr, &font_zh_28, 0xE8DCC0);
    s_reading = make_label(s_scr, &font_zh_16, 0xC0B098);

    lv_obj_t *btn = lv_button_create(s_scr);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A1812), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8A6A30), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_add_event_cb(btn, on_leave, LV_EVENT_CLICKED, NULL);
    s_btn_label = lv_label_create(btn);
    lv_obj_set_style_text_font(s_btn_label, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_btn_label, lv_color_hex(0xE0C890), 0);
    lv_obj_center(s_btn_label);
}

static esp_err_t enter(void)
{
    s_leave = false;

    content_poem_t p;
    int no = s_browse ? s_browse_poem : ritual_poem();
    int cat = s_browse ? s_browse_cat : ritual_category();
    esp_err_t err = content_get_poem(no, &p);

    lvgl_port_lock(0);
    if (!s_scr) build_screen();

    if (err == ESP_OK) {
        lv_label_set_text(s_head, p.name);
        lv_label_set_text_fmt(s_cat, "所問：%s", ritual_cat_name((ritual_cat_t)cat));
        lv_label_set_text_fmt(s_meta, "%s　%s　%s", p.ganzhi, p.trigram, p.attr);

        char lines[sizeof(p.text) + 8];
        poem_lines(p.text, lines, sizeof(lines));
        lv_label_set_text(s_text, lines);

        char reading[CONTENT_READING_MAX];
        if (content_get_reading(no, cat, reading, sizeof(reading)) == ESP_OK) {
            lv_label_set_text(s_reading, reading);
        } else {
            lv_label_set_text(s_reading, "這首的白話解讀尚未寫入。");
        }
    } else {
        ESP_LOGE(TAG, "第 %d 籤讀不到（%s）", no, esp_err_to_name(err));
        lv_label_set_text(s_head, "籤詩讀不到");
        lv_label_set_text(s_cat, "");
        lv_label_set_text(s_meta, "");
        lv_label_set_text(s_text, "");
        lv_label_set_text(s_reading, "");
    }

    // 翻閱是從參拜簿進來的，出口也該指回參拜簿
    lv_label_set_text(s_btn_label, s_browse ? "回參拜簿" : "禮畢");

    lv_obj_scroll_to_y(s_scr, 0, LV_ANIM_OFF);
    screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

static void tick(void)
{
    if (s_leave) {
        s_leave = false;
        // 翻閱是從參拜簿來的，禮畢頁只屬於剛走完的那次參拜
        if (s_browse) {
            s_browse = false;
            screen_mgr_goto(&records_screen);
        } else {
            screen_mgr_goto(&end_screen);
        }
    }
}

static bool on_event(screen_event_t ev)
{
    // 讀完離開走長按或底部的按鈕。捲動中誤觸短按不該把人踢出去
    if (ev == SCREEN_EV_BOOT_HOLD) {
        s_browse = false;
        screen_mgr_goto(&shrine_screen);
        return true;
    }
    return false;
}

const screen_t reading_screen = {
    .name = "解籤閣",
    .enter = enter,
    .exit = NULL,
    .tick = tick,
    .on_event = on_event,
};

// 設定（正殿右側）：音量與亮度，外加一行電量。
//
// 兩個控制項是**直立的格子條**，上下拖動調整——不是滑桿。
// 橫向的滑桿會和「左右滑動切換畫面」搶同一個手勢：拖滑桿拖到一半就換頁，
// 或是想換頁卻把音量拉到底。改成垂直之後兩者用的是不同方向，互不干涉。
//
// 拖動即時套用、放開才寫 NVS——拖一次會經過幾十個中間值，每一格都寫 flash
// 只是在磨損 NVS，而聽得到、看得到本來就不需要等寫入完成。
//
// 事件回呼跑在 LVGL 任務裡，那裡已經持有 LVGL 鎖，
// 所以寫面板亮度暫存器（與畫面資料共用 QSPI）不必也不能再上一次鎖。
#include "settings_screen.h"

#include "audio.h"
#include "board.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "screen_mgr.h"
#include "settings.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

#define PAD      20
#define SEGS     12      // 格子數。再多就看不出一格一格，再少就調不細
#define BAR_W    120
#define CELL_H   18
#define CELL_GAP 4

#define CELL_ON  0xE8C880
#define CELL_OFF 0x3A2018

typedef struct {
    lv_obj_t *cells[SEGS];
    lv_obj_t *value;
    int32_t min, max;
    int32_t val;
    void (*apply)(int32_t v);
    void (*store)(int32_t v);
} bar_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_batt;
static bar_t s_vol;
static bar_t s_bright;

// 目前值換算成幾格亮。至少亮一格——全暗看起來像壞掉，而不是「調到最小」
static int lit_cells(const bar_t *b)
{
    int32_t span = b->max - b->min;
    if (span <= 0) return 1;
    int n = (int)(((b->val - b->min) * SEGS + span - 1) / span);   // 進位，剛動到就看得出來
    if (n < 1) n = 1;
    if (n > SEGS) n = SEGS;
    return n;
}

static void refresh(bar_t *b)
{
    int lit = lit_cells(b);
    for (int i = 0; i < SEGS; i++) {
        // cells[0] 在最上面，亮的是下半段
        bool on = (SEGS - i) <= lit;
        lv_obj_set_style_bg_color(b->cells[i], lv_color_hex(on ? CELL_ON : CELL_OFF), 0);
    }
    lv_label_set_text_fmt(b->value, "%d", (int)((b->val - b->min) * 100 / (b->max - b->min)));
}

// 觸控落在條上的哪個高度就是哪個值——和 iPhone 的音量條一樣是絕對位置，
// 不是相對拖動。點一下就到位，不必從目前值慢慢推
static void on_drag(lv_event_t *e)
{
    bar_t *b = lv_event_get_user_data(e);
    lv_obj_t *box = lv_event_get_target(e);

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_area_t a;
    lv_obj_get_coords(box, &a);
    int32_t h = lv_area_get_height(&a);
    if (h <= 0) return;

    int32_t from_bottom = a.y2 - p.y;
    if (from_bottom < 0) from_bottom = 0;
    if (from_bottom > h) from_bottom = h;

    b->val = b->min + (b->max - b->min) * from_bottom / h;
    b->apply(b->val);
    refresh(b);

    if (lv_event_get_code(e) == LV_EVENT_RELEASED) b->store(b->val);
}

static void build_bar(lv_obj_t *parent, bar_t *b, const char *name,
                      int32_t min, int32_t max, int32_t val,
                      void (*apply)(int32_t), void (*store)(int32_t))
{
    b->min = min;
    b->max = max;
    b->val = val;
    b->apply = apply;
    b->store = store;

    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, BAR_W, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(col);
    lv_obj_set_style_text_font(title, &font_zh_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x9A8060), 0);
    lv_label_set_text(title, name);

    // 格子條本身。整條收觸控，個別格子不收——手指落在格與格之間的縫也要算數
    lv_obj_t *box = lv_obj_create(col);
    lv_obj_set_size(box, BAR_W, SEGS * CELL_H + (SEGS - 1) * CELL_GAP);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_row(box, CELL_GAP, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, on_drag, LV_EVENT_PRESSED, b);
    lv_obj_add_event_cb(box, on_drag, LV_EVENT_PRESSING, b);
    lv_obj_add_event_cb(box, on_drag, LV_EVENT_RELEASED, b);

    for (int i = 0; i < SEGS; i++) {
        b->cells[i] = lv_obj_create(box);
        lv_obj_set_size(b->cells[i], BAR_W, CELL_H);
        lv_obj_set_style_border_width(b->cells[i], 0, 0);
        lv_obj_set_style_radius(b->cells[i], 4, 0);
        // 格子不吃觸控，事件一律由整條處理
        lv_obj_remove_flag(b->cells[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    b->value = lv_label_create(col);
    lv_obj_set_style_text_font(b->value, &font_zh_16, 0);
    lv_obj_set_style_text_color(b->value, lv_color_hex(0xC0B098), 0);

    refresh(b);
}

static void apply_volume(int32_t v)
{
    audio_set_volume((uint8_t)v);
}

static void store_volume(int32_t v)
{
    settings_set_volume((uint8_t)v);
    audio_play_clack(100);   // 放開時試聽一聲，不然調了也不知道調成怎樣
}

static void apply_brightness(int32_t v)
{
    board_display_brightness((uint8_t)v);
}

static void store_brightness(int32_t v)
{
    settings_set_brightness((uint8_t)v);
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x140808), 0);
    lv_obj_set_style_pad_all(s_scr, PAD, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_flex_flow(s_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_scr, 12, 0);

    lv_obj_t *title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(title, &font_zh_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xC0A060), 0);
    lv_label_set_text(title, "設定");

    s_batt = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_batt, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(0x8A7458), 0);

    lv_obj_t *row = lv_obj_create(s_scr);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    build_bar(row, &s_vol, "音量", 0, 100, settings_volume(), apply_volume, store_volume);
    build_bar(row, &s_bright, "亮度", SETTINGS_BRIGHT_MIN, 255, settings_brightness(),
              apply_brightness, store_brightness);
}

static esp_err_t enter(void)
{
    lvgl_port_lock(0);
    if (!s_scr) build_screen();

    // 電量每次進來讀一次就夠。這一頁不會待太久，做成定時更新只是多一個 timer
    uint8_t pct = 0;
    if (board_battery_percent(&pct) == ESP_OK) {
        lv_label_set_text_fmt(s_batt, "電量 %u%%", pct);
    } else {
        lv_label_set_text(s_batt, "電量 --");
    }

    lv_screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
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

const screen_t settings_screen = {
    .name = "設定",
    .enter = enter,
    .exit = NULL,
    .tick = NULL,
    .on_event = on_event,
};

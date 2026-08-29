// 正殿。靜態背景 + 香爐上方的香煙局部動畫。
//
// 畫面全部用 LVGL 幾何圖形畫，沒有點陣圖：神像與神龕目前是**佔位造型**，
// 不是最終美術。這樣做的好處是這一版不必先有美術資源就能把導覽與動畫驗完，
// 之後換成 gen-sprites.sh 產的圖只要動 build_screen()。
//
// 香煙刻意只在 120x160 的範圍裡動——LVGL 只會重畫髒區域，
// 整片背景不會每幀重算（S4 已證實瓶頸在算圖不在傳輸）。
#include "shrine_screen.h"

#include "board.h"
#include "cast_screen.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "records_screen.h"
#include "screen_mgr.h"
#include "settings_screen.h"

#define CENSER_W 150
#define CENSER_H 52
#define CENSER_Y 372          // 香爐頂緣的 y

#define PUFFS      5
#define SMOKE_RISE 160        // 香煙上升的高度
#define SMOKE_DRIFT 26        // 左右飄移的振幅
#define SMOKE_STEP 14         // 每幀的相位增量（滿相位 1000）
#define SMOKE_MS   50

static lv_obj_t *s_scr;
static lv_obj_t *s_puff[PUFFS];
static int32_t s_phase[PUFFS];
static lv_timer_t *s_timer;

// 點擊在 LVGL 任務裡發生，畫面切換一律留給主迴圈做——
// screen_mgr 沒有上鎖，兩個任務同時動它會踩到彼此
static volatile bool s_goto_cast;

static void on_click(lv_event_t *e)
{
    (void)e;
    s_goto_cast = true;
}

static void smoke_tick(lv_timer_t *t)
{
    (void)t;
    for (int i = 0; i < PUFFS; i++) {
        s_phase[i] += SMOKE_STEP;
        if (s_phase[i] >= 1000) s_phase[i] -= 1000;

        int32_t p = s_phase[i];
        int32_t y = CENSER_Y - p * SMOKE_RISE / 1000;
        // 越高越淡、越大、飄得越開，才像煙而不像一串珠子
        int32_t size = 9 + p * 15 / 1000;
        int32_t opa = 150 - p * 150 / 1000;
        int32_t angle = (p * 2 + i * 700) % 3600;
        int32_t dx = lv_trigo_sin(angle) * SMOKE_DRIFT * p / (32767 * 1000);

        lv_obj_set_size(s_puff[i], size, size);
        lv_obj_set_pos(s_puff[i], BOARD_LCD_H_RES / 2 - size / 2 + dx, y);
        lv_obj_set_style_bg_opa(s_puff[i], opa, 0);
    }
}

static lv_obj_t *plain_box(lv_obj_t *parent, int32_t w, int32_t h, uint32_t bg, uint32_t border)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(o, border ? 2 : 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x2A0A0A), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(0x120404), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    // 整片正殿都可點，進入擲筊
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, on_click, LV_EVENT_CLICKED, NULL);

    // 神龕
    lv_obj_t *niche = plain_box(s_scr, 244, 268, 0x3A1410, 0x8A6A30);
    lv_obj_set_style_radius(niche, 12, 0);
    lv_obj_align(niche, LV_ALIGN_TOP_MID, 0, 40);

    // 神像佔位：頭 + 身，之後換成真圖
    lv_obj_t *body = plain_box(s_scr, 116, 150, 0xC09A50, 0);
    lv_obj_set_style_radius(body, 58, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 150);

    lv_obj_t *head = plain_box(s_scr, 62, 62, 0xD8B878, 0);
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 100);

    // 香煙要畫在香爐之下的圖層順序之前，煙才會從爐口後面冒出來
    for (int i = 0; i < PUFFS; i++) {
        s_puff[i] = plain_box(s_scr, 10, 10, 0xB0B0B0, 0);
        lv_obj_set_style_radius(s_puff[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_puff[i], 0, 0);
        s_phase[i] = i * (1000 / PUFFS);
    }

    lv_obj_t *censer = plain_box(s_scr, CENSER_W, CENSER_H, 0x6A4A28, 0x9A7838);
    lv_obj_set_style_radius(censer, 8, 0);
    lv_obj_set_pos(censer, BOARD_LCD_H_RES / 2 - CENSER_W / 2, CENSER_Y);

    s_timer = lv_timer_create(smoke_tick, SMOKE_MS, NULL);
    lv_timer_pause(s_timer);
}

static esp_err_t enter(void)
{
    s_goto_cast = false;
    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    lv_screen_load(s_scr);
    lv_timer_resume(s_timer);
    lvgl_port_unlock();
    return ESP_OK;
}

static void exit_(void)
{
    lvgl_port_lock(0);
    lv_timer_pause(s_timer);
    lvgl_port_unlock();
}

static void tick(void)
{
    if (s_goto_cast) {
        s_goto_cast = false;
        screen_mgr_goto(&cast_screen);
    }
}

static bool on_event(screen_event_t ev)
{
    switch (ev) {
    case SCREEN_EV_BOOT_KEY:
        screen_mgr_goto(&cast_screen);
        return true;

    // 導覽圖是「← 參拜簿　正殿　設定 →」：手指往左掃，畫面往左讓開，露出右邊的設定
    case SCREEN_EV_SWIPE_LEFT:
        screen_mgr_goto(&settings_screen);
        return true;
    case SCREEN_EV_SWIPE_RIGHT:
        screen_mgr_goto(&records_screen);
        return true;

    case SCREEN_EV_SHAKE:
    case SCREEN_EV_WAVE:
        return false;
    }
    return false;
}

const screen_t shrine_screen = {
    .name = "正殿",
    .enter = enter,
    .exit = exit_,
    .tick = tick,
    .on_event = on_event,
};

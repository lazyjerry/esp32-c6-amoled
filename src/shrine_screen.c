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
#include "bow_screen.h"
#include "cast_screen.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "records_screen.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "settings_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

#define PILLAR_W 26           // 兩側廊柱
#define PLAQUE_W 250          // 匾額
#define PLAQUE_H 52
#define NICHE_W  236          // 神龕
#define NICHE_H  208
#define NICHE_Y  84
#define TABLE_W  312          // 供桌
#define TABLE_H  22
#define TABLE_Y  322
#define CENSER_W 132
#define CENSER_H 48
#define CENSER_Y 352          // 香爐頂緣的 y

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
static volatile bool s_goto_ritual;

static void on_click(lv_event_t *e)
{
    (void)e;
    s_goto_ritual = true;
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
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x3A0E0C), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(0x140404), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    // 整片正殿都可點，進入儀式
    lv_obj_add_flag(s_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scr, on_click, LV_EVENT_CLICKED, NULL);

    // 兩側廊柱。把畫面框起來，中間才像一個殿內的空間
    for (int i = 0; i < 2; i++) {
        lv_obj_t *pillar = plain_box(s_scr, PILLAR_W, BOARD_LCD_V_RES, 0x5A1410, 0);
        lv_obj_set_style_radius(pillar, 0, 0);
        lv_obj_set_pos(pillar, i == 0 ? 0 : BOARD_LCD_H_RES - PILLAR_W, 0);

        lv_obj_t *cap = plain_box(s_scr, PILLAR_W + 8, 14, 0x8A6A30, 0);
        lv_obj_set_style_radius(cap, 0, 0);
        lv_obj_set_pos(cap, i == 0 ? 0 : BOARD_LCD_H_RES - PILLAR_W - 8, 96);
    }

    // 匾額
    lv_obj_t *plaque = plain_box(s_scr, PLAQUE_W, PLAQUE_H, 0x2A0A08, 0xC0902C);
    lv_obj_set_style_radius(plaque, 4, 0);
    lv_obj_align(plaque, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *plaque_text = lv_label_create(plaque);
    lv_obj_set_style_text_font(plaque_text, &font_zh_28, 0);
    lv_obj_set_style_text_color(plaque_text, lv_color_hex(0xE8C060), 0);
    lv_label_set_text(plaque_text, "成功廟");
    lv_obj_center(plaque_text);

    // 神龕：外框金邊，內裡壓暗，神像才浮得出來
    lv_obj_t *niche = plain_box(s_scr, NICHE_W, NICHE_H, 0x24100C, 0x8A6A30);
    lv_obj_set_style_radius(niche, 10, 0);
    lv_obj_align(niche, LV_ALIGN_TOP_MID, 0, NICHE_Y);

    // 神像佔位：光暈 + 頭 + 身，之後換成真圖
    lv_obj_t *halo = plain_box(s_scr, 110, 110, 0x5A3A18, 0);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(halo, LV_ALIGN_TOP_MID, 0, NICHE_Y + 22);

    lv_obj_t *body = plain_box(s_scr, 118, 116, 0xB8933F, 0);
    lv_obj_set_style_radius(body, 56, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, NICHE_Y + 88);

    lv_obj_t *head = plain_box(s_scr, 58, 58, 0xD8B878, 0);
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, NICHE_Y + 48);

    // 香煙要在香爐與供桌之前建，煙才會從爐口後面冒出來
    for (int i = 0; i < PUFFS; i++) {
        s_puff[i] = plain_box(s_scr, 10, 10, 0xB0B0B0, 0);
        lv_obj_set_style_radius(s_puff[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_puff[i], 0, 0);
        s_phase[i] = i * (1000 / PUFFS);
    }

    // 供桌
    lv_obj_t *table = plain_box(s_scr, TABLE_W, TABLE_H, 0x7A3A1C, 0x9A6838);
    lv_obj_set_style_radius(table, 3, 0);
    lv_obj_set_pos(table, BOARD_LCD_H_RES / 2 - TABLE_W / 2, TABLE_Y);

    lv_obj_t *cloth = plain_box(s_scr, TABLE_W - 40, 74, 0x9A2018, 0x6A1410);
    lv_obj_set_style_radius(cloth, 2, 0);
    lv_obj_set_pos(cloth, BOARD_LCD_H_RES / 2 - (TABLE_W - 40) / 2, TABLE_Y + TABLE_H);

    // 香爐坐在供桌上
    lv_obj_t *censer = plain_box(s_scr, CENSER_W, CENSER_H, 0x6A4A28, 0x9A7838);
    lv_obj_set_style_radius(censer, 8, 0);
    lv_obj_set_pos(censer, BOARD_LCD_H_RES / 2 - CENSER_W / 2, CENSER_Y);

    // 爐身上的字。深色壓在銅色爐身上，像鑄上去的，不該比匾額搶眼
    lv_obj_t *censer_text = lv_label_create(censer);
    lv_obj_set_style_text_font(censer_text, &font_zh_16, 0);
    lv_obj_set_style_text_color(censer_text, lv_color_hex(0x3A2410), 0);
    lv_label_set_text(censer_text, "有求必應");
    lv_obj_center(censer_text);

    // 爐耳，兩側各一個，讓它不只是一個方塊
    for (int i = 0; i < 2; i++) {
        lv_obj_t *ear = plain_box(s_scr, 14, 20, 0x8A6430, 0);
        lv_obj_set_style_radius(ear, 4, 0);
        lv_obj_set_pos(ear,
                       BOARD_LCD_H_RES / 2 + (i == 0 ? -CENSER_W / 2 - 10 : CENSER_W / 2 - 4),
                       CENSER_Y + 8);
    }

    s_timer = lv_timer_create(smoke_tick, SMOKE_MS, NULL);
    lv_timer_pause(s_timer);
}

static esp_err_t enter(void)
{
    s_goto_ritual = false;
    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    screen_load(s_scr);
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
    if (s_goto_ritual) {
        s_goto_ritual = false;
        ritual_begin();
        screen_mgr_goto(&bow_screen);
    }
}

static bool on_event(screen_event_t ev)
{
    switch (ev) {
    // 點擊走完整儀式，BOOT 是自由擲筊——原本的擲筊功能不因為多了儀式就消失
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

    // 已經在正殿了，長按沒有地方可去
    case SCREEN_EV_BOOT_HOLD:
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

// 求籤。搖動板子 → 籤筒左右晃 + 竹籤碰撞聲 → 一支籤升出筒口 → 顯示籤號 → 進擲筊確認。
//
// 搖籤筒是上下抖，不是左右擺——實際動作是把籤筒抖到有一支跳出來，
// 所以動畫和 IMU 判定的手勢（上下甩）方向一致。
//
// 籤筒與竹籤同樣是 LVGL 幾何佔位造型，換成真圖只要改 build_screen()。
// 籤號在動畫開始時就抽好，動畫只是把已定的結果演出來——和擲筊的作法一致。
#include "draw_screen.h"

#include "audio.h"
#include "board.h"
#include "cast_screen.h"
#include "content.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "shrine_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

#define TUBE_W  120
#define TUBE_H  240
#define TUBE_Y  150
#define HINT_Y  56       // 籤筒上方的空白帶，避開露出筒口的竹籤（TUBE_Y-46 起）
#define STICKS  7
#define STICK_W 10

#define TICK_MS   33
#define T_SHAKE   1300   // 晃動持續時間
#define T_RISE    1900   // 抽出的那支升到頂
#define T_TOTAL   2600   // 顯示籤號
#define SWAY_PX   30     // 筒身上下擺幅
#define RISE_PX   120    // 籤升出來的高度

// 顯示籤號之後停一下，再進擲筊確認
#define HOLD_US (1800LL * 1000)

static const char *TAG = "draw";

static lv_obj_t *s_scr;
static lv_obj_t *s_tube;
static lv_obj_t *s_stick[STICKS];
static lv_obj_t *s_chosen;
static lv_obj_t *s_hint;
static lv_obj_t *s_number;

static lv_timer_t *s_timer;
static int32_t s_t;
static uint8_t s_clacked;
// 動畫跑在 LVGL 任務、狀態判斷在主迴圈，兩邊都碰的旗標要 volatile
static volatile bool s_busy;
static volatile int64_t s_shown_at_us;

static const struct { int32_t at_ms; uint8_t vol; } CLACKS[] = {
    {60, 90}, {330, 80}, {600, 70}, {880, 55}, {1150, 40},
};
#define CLACK_N (sizeof(CLACKS) / sizeof(CLACKS[0]))

static void anim_tick(lv_timer_t *t);

static int32_t stick_x(int i)
{
    return BOARD_LCD_H_RES / 2 - (STICKS * (STICK_W + 4)) / 2 + i * (STICK_W + 4);
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x1A0808), 0);

    // 竹籤先畫，才會被筒身擋住下半截
    for (int i = 0; i < STICKS; i++) {
        s_stick[i] = lv_obj_create(s_scr);
        lv_obj_set_size(s_stick[i], STICK_W, 110);
        lv_obj_remove_flag(s_stick[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_width(s_stick[i], 0, 0);
        lv_obj_set_style_radius(s_stick[i], 4, 0);
        lv_obj_set_style_bg_color(s_stick[i], lv_color_hex(0xC8A868), 0);
        lv_obj_set_pos(s_stick[i], stick_x(i), TUBE_Y - 46);
    }

    // 抽中的那一支，顏色深一點，動畫時才看得出是它被抽出來
    s_chosen = lv_obj_create(s_scr);
    lv_obj_set_size(s_chosen, STICK_W + 2, 130);
    lv_obj_remove_flag(s_chosen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(s_chosen, 0, 0);
    lv_obj_set_style_radius(s_chosen, 4, 0);
    lv_obj_set_style_bg_color(s_chosen, lv_color_hex(0xE8D0A0), 0);
    lv_obj_add_flag(s_chosen, LV_OBJ_FLAG_HIDDEN);

    s_tube = lv_obj_create(s_scr);
    lv_obj_set_size(s_tube, TUBE_W, TUBE_H);
    lv_obj_remove_flag(s_tube, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_tube, lv_color_hex(0x6A3A20), 0);
    lv_obj_set_style_border_color(s_tube, lv_color_hex(0x9A6838), 0);
    lv_obj_set_style_border_width(s_tube, 3, 0);
    lv_obj_set_style_radius(s_tube, 10, 0);
    lv_obj_set_pos(s_tube, BOARD_LCD_H_RES / 2 - TUBE_W / 2, TUBE_Y);

    // 提示放在籤筒上方的空白帶。原本貼著畫面底邊，會被籤筒下緣壓住
    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x9A8060), 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, HINT_Y);

    // 籤號蓋在筒身正中央——籤是從這支筒裡出來的，數字離開筒就沒有那個因果關係
    s_number = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_number, &font_zh_28, 0);
    lv_obj_set_style_text_color(s_number, lv_color_hex(0xE8C880), 0);
    lv_obj_align(s_number, LV_ALIGN_TOP_MID, 0, TUBE_Y + TUBE_H / 2 - 18);

    s_timer = lv_timer_create(anim_tick, TICK_MS, NULL);
    lv_timer_pause(s_timer);
}

static void tube_reset(void)
{
    lv_obj_set_pos(s_tube, BOARD_LCD_H_RES / 2 - TUBE_W / 2, TUBE_Y);
    for (int i = 0; i < STICKS; i++) lv_obj_set_pos(s_stick[i], stick_x(i), TUBE_Y - 46);
    lv_obj_add_flag(s_chosen, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_number, "");
    lv_label_set_text(s_hint, "搖一搖　求籤");
}

static void anim_tick(lv_timer_t *t)
{
    (void)t;
    s_t += TICK_MS;

    for (int i = 0; i < CLACK_N; i++) {
        if (!(s_clacked & (1 << i)) && s_t >= CLACKS[i].at_ms) {
            s_clacked |= 1 << i;
            audio_play_clack(CLACKS[i].vol);
        }
    }

    if (s_t <= T_SHAKE) {
        // 擺幅隨時間衰減，像真的抖到停下來
        int32_t amp = SWAY_PX * (T_SHAKE - s_t) / T_SHAKE;
        int32_t angle = (s_t * 1600 / 100) % 3600;
        int32_t dy = lv_trigo_sin(angle) * amp / 32767;
        lv_obj_set_pos(s_tube, BOARD_LCD_H_RES / 2 - TUBE_W / 2, TUBE_Y + dy);
        // 竹籤跟著抖，但幅度大一些且相位落後，看起來才像在筒裡被震得跳動
        int32_t sang = (angle + 900) % 3600;
        int32_t sdy = lv_trigo_sin(sang) * amp * 3 / (32767 * 2);
        for (int i = 0; i < STICKS; i++) {
            // 每支的相位錯開，免得整排像一塊板子
            int32_t pang = (sang + i * 300) % 3600;
            int32_t each = lv_trigo_sin(pang) * amp / (32767 * 2);
            lv_obj_set_pos(s_stick[i], stick_x(i), TUBE_Y - 46 + sdy + each);
        }
    } else if (s_t <= T_RISE) {
        int32_t p = (s_t - T_SHAKE) * 1000 / (T_RISE - T_SHAKE);
        lv_obj_remove_flag(s_chosen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_chosen, BOARD_LCD_H_RES / 2 - (STICK_W + 2) / 2,
                       TUBE_Y - 60 - RISE_PX * p / 1000);
    } else if (s_t <= T_TOTAL) {
        if (!s_shown_at_us) {
            lv_label_set_text_fmt(s_number, "第 %d 籤", ritual_poem());
            lv_label_set_text(s_hint, "");
            s_shown_at_us = esp_timer_get_time();
        }
    } else {
        lv_timer_pause(s_timer);
        s_busy = false;
    }
}

// 抽一支有本文的籤。留空的籤還沒核對完，不該被抽到
static int pick_poem(void)
{
    int count = content_poem_count();
    if (count <= 0) return 0;

    content_poem_t p;
    for (int tries = 0; tries < 12; tries++) {
        int no = (int)(esp_random() % (uint32_t)count) + 1;
        if (content_get_poem(no, &p) == ESP_OK) return no;
    }
    ESP_LOGE(TAG, "連抽 12 次都取不到有本文的籤");
    return 0;
}

static void start_draw(void)
{
    int no = pick_poem();
    if (no == 0) return;
    ritual_set_poem(no);
    ESP_LOGI(TAG, "%s：第 %d 籤", ritual_cat_name(ritual_category()), no);

    s_t = 0;
    s_clacked = 0;
    s_shown_at_us = 0;
    s_busy = true;

    lvgl_port_lock(0);
    tube_reset();
    lv_timer_resume(s_timer);
    lvgl_port_unlock();
}

static esp_err_t enter(void)
{
    s_busy = false;
    s_shown_at_us = 0;
    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    tube_reset();
    screen_load(s_scr);
    lvgl_port_unlock();
    return ESP_OK;
}

static void exit_(void)
{
    lvgl_port_lock(0);
    lv_timer_pause(s_timer);
    lvgl_port_unlock();
    s_busy = false;
}

// 求到籤一定要擲筊確認，不能直接看解籤
static void go_confirm(void)
{
    s_shown_at_us = 0;
    cast_screen_confirm();
    screen_mgr_goto(&cast_screen);
}

static void tick(void)
{
    if (s_busy || !s_shown_at_us) return;
    if (esp_timer_get_time() - s_shown_at_us >= HOLD_US) go_confirm();
}

static bool on_event(screen_event_t ev)
{
    switch (ev) {
    // BOOT 在這裡與擲筊畫面同義：籤號停著時直接進下一步，否則求一支（不用搖，測試方便）
    case SCREEN_EV_BOOT_KEY:
        if (s_shown_at_us) {
            go_confirm();
            return true;
        }
        if (!s_busy) {
            start_draw();
            return true;
        }
        return false;

    case SCREEN_EV_SHAKE:
        if (!s_busy && !s_shown_at_us) {
            start_draw();
            return true;
        }
        return false;

    // 短按 BOOT 被拿去求籤了，離開走長按或左右滑。動畫跑到一半不讓走，
    // 否則 timer 會繼續在看不見的畫面上跑
    case SCREEN_EV_BOOT_HOLD:
    case SCREEN_EV_SWIPE_LEFT:
    case SCREEN_EV_SWIPE_RIGHT:
        if (s_busy) return false;
        screen_mgr_goto(&shrine_screen);
        return true;

    default:
        return false;
    }
}

const screen_t draw_screen = {
    .name = "求籤",
    .enter = enter,
    .exit = exit_,
    .tick = tick,
    .on_event = on_event,
};

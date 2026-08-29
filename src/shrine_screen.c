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
#include "esp_random.h"
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

// 三支線香插在香灰裡。爐子自己不會冒煙——煙是從香頭燒出來的，
// 起點要在香的頂端而不是爐口。
//
// 每次進正殿三支各自抽一個長度：香本來就不會燒得一樣快，三支等長反而假。
// 這裡只是隨機，不對照香譜——那需要一份逐譜核對過的長短資料，沒有就不做。
#define INCENSE_N      3
#define INCENSE_W      5
#define INCENSE_LEVELS 4      // 長度等級
#define INCENSE_STEP   14     // 每一級差多少
#define INCENSE_SHOW   74     // 最短那一級露出爐口的長度
// 只沒入爐口一小段。埋太深會蓋到爐身上的字——爐口就是視覺上香消失的地方，
// 真正的香灰在爐裡本來就看不見
#define INCENSE_SINK 10
#define INCENSE_TIP  4        // 頂端的燃點
#define INCENSE_GAP  20       // 三支之間的間距

#define PUFFS      6          // 三支香各分兩縷，太少會看得出是同一顆在繞
#define SMOKE_RISE 120        // 香煙上升的高度。從香頭起算，再高就頂到匾額了
#define SMOKE_DRIFT 26        // 左右飄移的振幅
#define SMOKE_STEP 14         // 每幀的相位增量（滿相位 1000）
#define SMOKE_MS   50

static lv_obj_t *s_scr;
static lv_obj_t *s_stick[INCENSE_N];
static lv_obj_t *s_stick_tip[INCENSE_N];
static lv_obj_t *s_puff[PUFFS];
// 每縷煙固定屬於某一支香，起點才不會在三支之間跳
static int32_t s_puff_x[PUFFS];
static int32_t s_puff_y[PUFFS];
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

// 第 i 支香的 x（中心）
static int32_t incense_x(int i)
{
    return BOARD_LCD_H_RES / 2 + (i - INCENSE_N / 2) * INCENSE_GAP;
}

static void smoke_tick(lv_timer_t *t)
{
    (void)t;
    for (int i = 0; i < PUFFS; i++) {
        s_phase[i] += SMOKE_STEP;
        if (s_phase[i] >= 1000) s_phase[i] -= 1000;

        int32_t p = s_phase[i];
        int32_t y = s_puff_y[i] - p * SMOKE_RISE / 1000;
        // 越高越淡、越大、飄得越開，才像煙而不像一串珠子。
        // 起手比原本小：從一支香冒出來的煙，一開始只有香那麼細
        int32_t size = 5 + p * 16 / 1000;
        int32_t opa = 140 - p * 140 / 1000;
        int32_t angle = (p * 2 + i * 700) % 3600;
        int32_t dx = lv_trigo_sin(angle) * SMOKE_DRIFT * p / (32767 * 1000);

        lv_obj_set_size(s_puff[i], size, size);
        lv_obj_set_pos(s_puff[i], s_puff_x[i] - size / 2 + dx, y);
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

    // 每縷煙固定綁一支香。相位平均錯開，同一支的兩縷就會一前一後而不是疊在一起
    for (int i = 0; i < PUFFS; i++) {
        s_puff[i] = plain_box(s_scr, 10, 10, 0xB0B0B0, 0);
        lv_obj_set_style_radius(s_puff[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_puff[i], 0, 0);
        s_puff_x[i] = incense_x(i % INCENSE_N);
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
    // 往爐身下半擺，讓出上緣給插香的位置
    lv_obj_align(censer_text, LV_ALIGN_CENTER, 0, 8);

    // 三支線香。先畫香身，燃點另外疊一小段在頂端。長度在 enter() 才決定
    for (int i = 0; i < INCENSE_N; i++) {
        s_stick[i] = plain_box(s_scr, INCENSE_W, INCENSE_SHOW, 0x7A5636, 0);
        lv_obj_set_style_radius(s_stick[i], 1, 0);

        s_stick_tip[i] = plain_box(s_scr, INCENSE_W, INCENSE_TIP, 0xE0621C, 0);
        lv_obj_set_style_radius(s_stick_tip[i], 1, 0);
    }

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

// 每次進正殿重抽三支香的長度，順便把每縷煙的起點移到自己那支香的香頭
static void randomize_incense(void)
{
    for (int i = 0; i < INCENSE_N; i++) {
        int32_t show = INCENSE_SHOW + (int32_t)(esp_random() % INCENSE_LEVELS) * INCENSE_STEP;
        int32_t tip_y = CENSER_Y - show;
        int32_t x = incense_x(i) - INCENSE_W / 2;

        // 香身連同沒入爐口的那一截一起畫，爐子會蓋住下半
        lv_obj_set_size(s_stick[i], INCENSE_W, show + INCENSE_SINK);
        lv_obj_set_pos(s_stick[i], x, tip_y);
        lv_obj_set_pos(s_stick_tip[i], x, tip_y);

        for (int k = i; k < PUFFS; k += INCENSE_N) s_puff_y[k] = tip_y;
    }
}

static esp_err_t enter(void)
{
    s_goto_ritual = false;
    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    randomize_incense();
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

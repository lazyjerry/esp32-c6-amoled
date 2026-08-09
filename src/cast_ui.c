// 由上往下看的第一人稱擲筊。整片螢幕就是地面，沒有地平線，所以景深全靠三件事：
//   高度 h → 越高離眼睛越近 → 畫得越大、在畫面上往上挪
//   影子   → 留在地面座標上不隨高度移動，和筊之間的距離就是高度感
//   翻滾   → 平面／凸面兩張圖交替，落地才定住筊象
// 整段跑在 LVGL 自己的 timer 裡，所以回呼內不需要再上 lvgl_port_lock。
#include "cast_ui.h"

#include <inttypes.h>
#include <math.h>

#include "audio.h"
#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

LV_FONT_DECLARE(font_zh_16)
LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_64)
LV_IMAGE_DECLARE(blk_flat);
LV_IMAGE_DECLARE(blk_round);

// 天氣看板受限於 TLS 用 32 列；擲筊沒有網路，加倍換每幀更少次 flush
#define DRAW_BUF_ROWS 64
#define BLK_W 132
#define BLK_H 70
#define BLOCKS 2

#define TICK_MS 33
#define PI_F 3.14159265f

// 各階段的累計時間（ms）
#define T_FALL   1000    // 出手 → 落地（拋物線的一整段）
#define T_SETTLE 1520    // 兩次彈跳
#define T_ZOOM   2140    // 鏡頭拉近
#define T_END    6700    // 結果停留

#define B1_MS 300        // 第一次彈跳
#define B2_MS 220

// 鏡頭中心：拉近時所有東西都往這點的反方向散開
#define CX 184
#define CY 250

#define S_GROUND 170     // 貼在地面時的縮放（256 = 原尺寸）
#define H_HAND    90     // 出手時手離地的高度，單位是螢幕像素
#define H_REF    420     // 高度換成放大倍率的參考值。放大得越少，最高點兩片越不會疊在一起
#define H_LIFT    55     // 高度換成畫面上移的比例（%）
#define Z_MAX    150     // 鏡頭拉近後的倍率（%）。再大兩片就要頂到畫面左右邊

#define SHADOW_OPA 135
#define SHADOW_W   58    // 佔筊寬的百分比。做太大會從月牙的凹口露出來
#define SHADOW_H   52
#define SHADOW_DX   6    // 光從左上來，影子往右下偏
#define SHADOW_DY   5

static const char *TAG = "cast_ui";

typedef struct {
    lv_obj_t *img;
    lv_obj_t *shadow;
    int32_t x_hand, y_hand;
    int32_t x_land, y_land;
    float spin;            // 翻滾速度，圈/秒；正負是方向
    int32_t idle_rot;      // 0.1 度
    int32_t final_rot;
    bool final_round;
    int8_t showing;        // 目前貼的圖：-1 未設、0 平面、1 凸面
} block_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_hint;
static lv_obj_t *s_hint2;
static lv_obj_t *s_name;
static lv_obj_t *s_desc;

static block_t s_blk[BLOCKS];
static lv_timer_t *s_timer;
static uint32_t s_start_tick;
static uint32_t s_frames;
static uint32_t s_anim_frames;   // 只算到鏡頭停下為止，結果停留那幾秒沒有重繪不列入
static int32_t s_t;
static uint8_t s_clacked;      // 已播過的音效位元遮罩
static bool s_result_shown;
static cast_result_t s_result;
static volatile bool s_busy;

static const struct {
    int32_t at_ms;
    uint8_t vol;
} CLACKS[] = {
    {T_FALL,       100},
    {T_FALL + 70,   88},
    {T_FALL + 300,  52},
    {T_FALL + 360,  42},
    {T_SETTLE,      26},
};

// SH8601 要求繪圖區起點偶數、終點奇數，否則整片畫面會歪掉。
// LVGL 9 取消了 v8 的 rounder_cb，改在 INVALIDATE_AREA 事件裡調整。ui.c 有天氣版的孿生體
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static float ease_out_cubic(float u)
{
    float d = 1.0f - u;
    return 1.0f - d * d * d;
}

static int32_t lerp(int32_t a, int32_t b, float u) { return a + (int32_t)((b - a) * u); }

// 從 H_HAND 拋出、在 u=1 落地的拋物線；最高點刻意壓在前段，落下比拋起久
static float flight_height(float u)
{
    const float g = 6.25f * H_HAND;
    const float v = 0.84f * g;
    float h = H_HAND + v * u - g * u * u;
    return h > 0.0f ? h : 0.0f;
}

static float bounce_height(int32_t sub)
{
    if (sub < B1_MS) return 26.0f * sinf(PI_F * sub / B1_MS);

    float u = (float)(sub - B1_MS) / B2_MS;
    if (u > 1.0f) u = 1.0f;
    return 10.0f * sinf(PI_F * u);
}

static float height_at(int32_t t)
{
    if (t <= T_FALL) return flight_height((float)t / T_FALL);
    if (t < T_SETTLE) return bounce_height(t - T_FALL);
    return 0.0f;
}

// 鏡頭倍率：落定前是 1，之後拉近到 Z_MAX
static float zoom_at(int32_t t)
{
    if (t <= T_SETTLE) return 1.0f;
    if (t >= T_ZOOM) return Z_MAX / 100.0f;

    float u = (float)(t - T_SETTLE) / (T_ZOOM - T_SETTLE);
    return 1.0f + (Z_MAX / 100.0f - 1.0f) * ease_out_cubic(u);
}

static void set_face(block_t *b, bool round)
{
    if (b->showing == (int8_t)round) return;
    lv_image_set_src(b->img, round ? (const void *)&blk_round : (const void *)&blk_flat);
    lv_image_set_pivot(b->img, BLK_W / 2, BLK_H / 2);
    b->showing = round;
}

// 把地面座標 + 高度投影到畫面上，順便擺好影子
static void project(block_t *b, int32_t gx, int32_t gy, float h, float z, int32_t rot)
{
    int32_t sx = CX + (int32_t)((gx - CX) * z);
    int32_t sy = CY + (int32_t)((gy - CY) * z);

    // 影子留在地面座標，隨高度縮一點、淡一點，落地時最實
    float shrink = 1.0f - 0.25f * h / H_REF;
    int32_t sw = (int32_t)(BLK_W * S_GROUND * z * SHADOW_W / 25600.0f * shrink);
    int32_t sh = (int32_t)(BLK_H * S_GROUND * z * SHADOW_H / 25600.0f * shrink);
    lv_obj_set_size(b->shadow, sw, sh);
    lv_obj_set_pos(b->shadow, sx - sw / 2 + (int32_t)(SHADOW_DX * z),
                   sy - sh / 2 + (int32_t)(SHADOW_DY * z));
    lv_obj_set_style_bg_opa(b->shadow, (lv_opa_t)(SHADOW_OPA * (1.0f - 0.55f * h / H_REF)), 0);

    int32_t scale = (int32_t)(S_GROUND * z * (1.0f + h / H_REF));
    lv_obj_set_pos(b->img, sx - BLK_W / 2, sy - BLK_H / 2 - (int32_t)(h * H_LIFT / 100.0f * z));
    lv_image_set_scale(b->img, scale);
    lv_image_set_rotation(b->img, rot);
}

// 翻滾角度。落地後角速度線性衰減，看起來才像被地面吃掉動能
static void tumble(const block_t *b, int32_t t_ms, int32_t *rot, bool *round)
{
    float f;
    if (t_ms <= T_FALL) {
        f = (float)t_ms;
    } else {
        float sub = (float)(t_ms - T_FALL);
        float span = (float)(T_SETTLE - T_FALL);
        f = T_FALL + sub * (1.0f - sub / (2.0f * span));
    }

    // 從待機的角度接著轉，開拋時才不會瞬間跳角度
    float deg = b->idle_rot / 10.0f + b->spin * 0.36f * f;
    int32_t r = (int32_t)(deg * 10.0f) % 3600;
    if (r < 0) r += 3600;
    *rot = r;
    // 每轉半圈換一面
    *round = ((((int)floorf(deg / 180.0f)) % 2) + 2) % 2;
}

static void show_idle(void)
{
    for (int i = 0; i < BLOCKS; i++) {
        block_t *b = &s_blk[i];
        set_face(b, i == 1);
        project(b, b->x_hand, b->y_hand, H_HAND, 1.0f, b->idle_rot);
        lv_image_set_antialias(b->img, true);
    }
    lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_hint2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_name, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_desc, LV_OBJ_FLAG_HIDDEN);
}

static void show_result(void)
{
    lv_label_set_text(s_name, cast_result_name(s_result));
    lv_label_set_text(s_desc, cast_result_desc(s_result));
    lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_align(s_desc, LV_ALIGN_TOP_MID, 0, 106);
    lv_obj_remove_flag(s_name, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_desc, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < BLOCKS; i++) lv_image_set_antialias(s_blk[i].img, true);
}

static void play_due_clacks(void)
{
    for (size_t i = 0; i < sizeof(CLACKS) / sizeof(CLACKS[0]); i++) {
        if (!(s_clacked & (1 << i)) && s_t >= CLACKS[i].at_ms) {
            s_clacked |= 1 << i;
            audio_play_clack(CLACKS[i].vol);
        }
    }
}

static void tick(lv_timer_t *timer)
{
    // 時間軸取自真實時鐘而不是累加 TICK_MS：畫面來不及畫時要掉幀，
    // 不能變成慢動作，否則音效會和落地的瞬間脫拍
    s_t = (int32_t)lv_tick_elaps(s_start_tick);
    s_frames++;
    play_due_clacks();

    float h = height_at(s_t);
    float z = zoom_at(s_t);
    // 地面上的行進是等速的，出手之後水平方向沒有外力
    float travel = s_t >= T_FALL ? 1.0f : (float)s_t / T_FALL;

    for (int i = 0; i < BLOCKS; i++) {
        block_t *b = &s_blk[i];
        int32_t gx = lerp(b->x_hand, b->x_land, travel);
        int32_t gy = lerp(b->y_hand, b->y_land, travel);

        if (s_t < T_SETTLE) {
            int32_t rot;
            bool round;
            tumble(b, s_t, &rot, &round);
            set_face(b, round);
            project(b, gx, gy, h, z, rot);
        } else {
            // 最後一次落地就定住筊象，之後只有鏡頭在動
            set_face(b, b->final_round);
            project(b, gx, gy, 0.0f, z, b->final_rot);
        }
    }

    if (s_t >= T_ZOOM && !s_result_shown) {
        s_result_shown = true;
        s_anim_frames = s_frames;
        show_result();
    }
    if (s_t >= T_END) {
        lv_timer_pause(timer);
        show_idle();
        s_busy = false;
        ESP_LOGI(TAG, "拋擲 %" PRIu32 " 幀 / %d ms（%" PRIu32 " fps）",
                 s_anim_frames, T_ZOOM, s_anim_frames * 1000 / T_ZOOM);
    }
}

// 各筊象對應的落地姿態：平面朝上就貼平面圖，凸面朝上貼凸面圖
static void setup_pose(cast_result_t r)
{
    for (int i = 0; i < BLOCKS; i++) {
        block_t *b = &s_blk[i];
        b->final_rot = i == 0 ? 250 : 3380;   // 各歪幾度，避免像貼上去的
        b->spin = i == 0 ? 1.7f : -2.3f;
    }

    switch (r) {
        case CAST_SHENG:
            s_blk[0].final_round = false;
            s_blk[1].final_round = true;
            break;
        case CAST_XIAO:
            s_blk[0].final_round = false;
            s_blk[1].final_round = false;
            break;
        case CAST_YIN:
            s_blk[0].final_round = true;
            s_blk[1].final_round = true;
            break;
        default:
            // 立筊：俯視看到的是側立的窄邊，用轉九十度的凸面充當
            s_blk[0].final_round = true;
            s_blk[0].final_rot = 900;
            s_blk[1].final_round = false;
            break;
    }
}

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    // 整片螢幕就是地面。純黑的話影子看不見，用壓到很暗的暖色磚地
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x1A0E0D), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(0x33191A), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh_28, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_white(), 0);
    lv_label_set_text(s_hint, "搖一搖　擲筊");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 44);

    s_hint2 = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint2, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_hint2, lv_color_hex(0x9AA0A6), 0);
    lv_label_set_text(s_hint2, "請誠心默念所求之事");
    lv_obj_align(s_hint2, LV_ALIGN_TOP_MID, 0, 92);

    s_name = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_name, &font_zh_64, 0);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0xFFD470), 0);
    lv_label_set_text(s_name, "");

    s_desc = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_desc, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_desc, lv_color_hex(0xC8CBD0), 0);
    lv_label_set_text(s_desc, "");

    // 影子先建，才會壓在筊底下
    for (int i = 0; i < BLOCKS; i++) {
        s_blk[i].shadow = lv_obj_create(s_scr);
        lv_obj_remove_flag(s_blk[i].shadow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(s_blk[i].shadow, 0, 0);
        lv_obj_set_style_radius(s_blk[i].shadow, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_blk[i].shadow, lv_color_black(), 0);
    }
    for (int i = 0; i < BLOCKS; i++) {
        s_blk[i].img = lv_image_create(s_scr);
        s_blk[i].showing = -1;
        // 兩片全程保持間距，任何時刻都不重疊
        s_blk[i].x_hand = i == 0 ? 112 : 256;
        s_blk[i].y_hand = i == 0 ? 432 : 438;
        s_blk[i].x_land = i == 0 ? 108 : 260;
        s_blk[i].y_land = i == 0 ? 238 : 266;
        // 待機時弦邊朝內、圓弧朝外，就是雙手捧筊的樣子
        s_blk[i].idle_rot = i == 0 ? 900 : 2700;
    }
    setup_pose(CAST_SHENG);
    show_idle();
}

esp_err_t cast_ui_init(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = board_panel_io(),
        .panel_handle = board_panel(),
        .buffer_size = BOARD_LCD_H_RES * DRAW_BUF_ROWS,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            // 面板吃的是 byte-swap 過的 RGB565（紅是 0x00F8 不是 0xF800）
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "add disp");

    lv_display_add_event_cb(disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lvgl_port_lock(0);
    build_screen();
    lv_screen_load(s_scr);
    s_timer = lv_timer_create(tick, TICK_MS, NULL);
    lv_timer_pause(s_timer);
    lvgl_port_unlock();
    return ESP_OK;
}

void cast_ui_play(cast_result_t r)
{
    if (s_busy) return;

    lvgl_port_lock(0);
    s_busy = true;
    s_result = r;
    s_t = 0;
    s_frames = 0;
    s_anim_frames = 0;
    s_start_tick = lv_tick_get();
    s_clacked = 0;
    s_result_shown = false;

    setup_pose(r);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hint2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_name, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_desc, LV_OBJ_FLAG_HIDDEN);
    // 飛行中改用最近鄰取樣：兩片小圖每幀都在轉，雙線性差值省不下但很吃 CPU
    for (int i = 0; i < BLOCKS; i++) lv_image_set_antialias(s_blk[i].img, false);

    lv_timer_reset(s_timer);
    lv_timer_resume(s_timer);
    lvgl_port_unlock();
}

bool cast_ui_busy(void) { return s_busy; }

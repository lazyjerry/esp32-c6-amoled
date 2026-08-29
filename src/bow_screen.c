// 三拜。拜的動作是「把板子往前傾下去再回正」，用重力方向判定；BOOT 短按也算一拜。
//
// 為什麼不用既有的甩動偵測：甩動看的是動態加速度，拜是慢動作，
// 加速度小到門檻碰不到。拜是姿勢變化，該看的是重力向量轉了多少角度。
//
// 基準在進入畫面時取，所以板子怎麼拿都算得出來——躺著拜、立著拜都行。
#include "bow_screen.h"

#include <math.h>

#include "audio.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "imu.h"
#include "lvgl.h"
#include "ritual.h"
#include "screen_mgr.h"
#include "shrine_screen.h"
#include "tell_screen.h"

LV_FONT_DECLARE(font_zh_28)
LV_FONT_DECLARE(font_zh_16)

#define BOWS_NEEDED 3
// 傾到 30 度算拜下去，回到 12 度內算起身。兩個門檻分開才不會在邊界上抖動連發
#define BOW_DOWN_COS 0.866f   // cos(30°)
#define BOW_UP_COS   0.978f   // cos(12°)
// 禮成之後停一下讓人看到，再進稟告
#define DONE_HOLD_US (1200LL * 1000)
// 進來的動作是「點螢幕」，手一定在動。等姿勢穩下來再取基準，
// 否則基準會停在按下去的那一瞬間，第一拜就算不準
#define SETTLE_US (400LL * 1000)

static const char *TAG = "bow";

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_count;

static float s_ref[3];
static bool s_ref_ready;
static bool s_down;        // 目前是否在「拜下去」的狀態
static int s_bows;
static int64_t s_done_at_us;
static int64_t s_entered_at_us;

static void build_screen(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x1E0A0A), 0);

    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_title, &font_zh_28, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xD8B878), 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -40);

    s_count = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_count, &font_zh_16, 0);
    lv_obj_set_style_text_color(s_count, lv_color_hex(0x9A8060), 0);
    lv_obj_align(s_count, LV_ALIGN_CENTER, 0, 40);
}

static void refresh(void)
{
    lvgl_port_lock(0);
    if (s_bows >= BOWS_NEEDED) {
        lv_label_set_text(s_title, "禮成");
        lv_label_set_text(s_count, "");
    } else {
        lv_label_set_text(s_title, "向神明\n行三拜");
        lv_label_set_text(s_count, s_bows == 0 ? "把機身向前傾下再起身"
                                              : (s_bows == 1 ? "一拜" : "二拜"));
    }
    lvgl_port_unlock();
}

static esp_err_t enter(void)
{
    s_ref_ready = false;
    s_down = false;
    s_bows = 0;
    s_done_at_us = 0;
    s_entered_at_us = esp_timer_get_time();

    lvgl_port_lock(0);
    if (!s_scr) build_screen();
    screen_load(s_scr);
    lvgl_port_unlock();
    refresh();
    return ESP_OK;
}

// 兩個單位向量的夾角餘弦。1 = 同向，0 = 垂直
static float cos_to_ref(const float g[3])
{
    float dot = 0, n = 0, m = 0;
    for (int i = 0; i < 3; i++) {
        dot += g[i] * s_ref[i];
        n += g[i] * g[i];
        m += s_ref[i] * s_ref[i];
    }
    float d = sqrtf(n) * sqrtf(m);
    return d > 0.01f ? dot / d : 1.0f;
}

// 前傾與 BOOT 短按共用同一個計數。手不方便晃板子（接著 USB、坐著測）時按鍵一樣拜得完，
// 三次滿了就進稟告，兩條路徑沒有差別
static void count_bow(const char *why)
{
    s_bows++;
    ESP_LOGI(TAG, "%s：第 %d 拜", why, s_bows);
    if (s_bows >= BOWS_NEEDED) {
        s_done_at_us = esp_timer_get_time();
        audio_play_bell(90);   // 三拜完成敲一聲，是這段的句點
    }
    refresh();
}

static void tick(void)
{
    if (s_bows >= BOWS_NEEDED) {
        if (s_done_at_us && esp_timer_get_time() - s_done_at_us >= DONE_HOLD_US) {
            screen_mgr_goto(&tell_screen);
        }
        return;
    }

    float g[3];
    if (!imu_gravity(g)) return;

    // 基準取姿勢穩下來之後的第一筆，就是「捧著板子站好」的姿勢
    if (!s_ref_ready) {
        if (esp_timer_get_time() - s_entered_at_us < SETTLE_US) return;
        for (int i = 0; i < 3; i++) s_ref[i] = g[i];
        s_ref_ready = true;
        return;
    }

    float c = cos_to_ref(g);
    if (!s_down && c < BOW_DOWN_COS) {
        s_down = true;
    } else if (s_down && c > BOW_UP_COS) {
        s_down = false;
        count_bow("前傾");
    }
}

static bool on_event(screen_event_t ev)
{
    // 深層流程沒有上一頁，只有前進或離開。離開統一走長按
    if (ev == SCREEN_EV_BOOT_HOLD) {
        screen_mgr_goto(&shrine_screen);
        return true;
    }

    // 短按算一拜。禮成之後的那 1.2 秒不再收，免得多按一下把計數推過頭
    if (ev == SCREEN_EV_BOOT_KEY && s_bows < BOWS_NEEDED) {
        count_bow("BOOT 鍵");
        return true;
    }
    return false;
}

const screen_t bow_screen = {
    .name = "每日參拜",
    .enter = enter,
    .exit = NULL,
    .tick = tick,
    .on_event = on_event,
};

// 擲筊畫面。行為與重構前的 main.c 主迴圈一致：
//   待機 → 搖一搖或按 BOOT → 動畫 → 結果停住 → BOOT 或左右晃收掉 → 冷卻 3 秒 → 顯示提示
//
// 原本 main.c 在每個分支都要呼叫 drop_gestures() 把沒輪到的手勢取走，
// 否則動畫或冷卻期間累積的一次晃動會在解禁瞬間立刻生效。改成事件模型之後，
// main.c 每輪都把手勢取出來派發，這個畫面不處理就等於丟掉，效果相同但不必到處記得清。
#include "cast_screen.h"

#include "cast.h"
#include "cast_ui.h"
#include "esp_log.h"
#include "esp_timer.h"

#define COOLDOWN_US   (3LL * 1000 * 1000)
// 結果剛出現時手通常還在動，這段時間只認按鍵，不然筊象會來不及看就被收掉
#define HOLD_GUARD_US (800LL * 1000)

static const char *TAG = "cast";

static int64_t s_ready_at_us;
static int64_t s_hold_since_us;
static bool s_hint_shown;

static bool can_throw(void)
{
    return !cast_ui_busy() && !cast_ui_holding() && esp_timer_get_time() >= s_ready_at_us;
}

static void throw_blocks(const char *why)
{
    cast_result_t r = cast_draw();
    ESP_LOGI(TAG, "%s：%s", why, cast_result_name(r));
    cast_ui_play(r);
}

static void clear_result(const char *why)
{
    ESP_LOGI(TAG, "%s：收掉結果，冷卻 %lld 秒", why, COOLDOWN_US / 1000000);
    cast_ui_reset();
    s_ready_at_us = esp_timer_get_time() + COOLDOWN_US;
    s_hold_since_us = 0;
    s_hint_shown = false;
}

static esp_err_t enter(void)
{
    s_ready_at_us = 0;
    s_hold_since_us = 0;
    s_hint_shown = false;
    return ESP_OK;
}

static void tick(void)
{
    int64_t now = esp_timer_get_time();

    // 結果剛停住的那一刻記時，左右晃的保護期從這裡算起
    if (cast_ui_holding()) {
        if (s_hold_since_us == 0) s_hold_since_us = now;
        return;
    }
    s_hold_since_us = 0;

    // 冷卻結束才把「搖一搖　擲筊」提示放出來
    if (!s_hint_shown && !cast_ui_busy() && now >= s_ready_at_us) {
        cast_ui_show_hint();
        s_hint_shown = true;
    }
}

static bool on_event(screen_event_t ev)
{
    switch (ev) {
    case SCREEN_EV_BOOT_KEY:
        // BOOT 是功能鍵：結果停著時收掉，否則直接擲一次（不用搖，測試方便）
        if (cast_ui_holding()) {
            clear_result("BOOT 鍵");
            return true;
        }
        if (can_throw()) {
            throw_blocks("BOOT 鍵");
            return true;
        }
        return false;

    case SCREEN_EV_SHAKE:
        if (can_throw()) {
            throw_blocks("搖動");
            return true;
        }
        return false;

    case SCREEN_EV_SWIPE:
        // 只在結果停著、且過了保護期之後才認
        if (cast_ui_holding() && s_hold_since_us != 0 &&
            esp_timer_get_time() - s_hold_since_us >= HOLD_GUARD_US) {
            clear_result("左右晃動");
            return true;
        }
        return false;
    }
    return false;
}

const screen_t cast_screen = {
    .name = "擲筊",
    .enter = enter,
    .exit = NULL,
    .tick = tick,
    .on_event = on_event,
};

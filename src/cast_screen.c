// 擲筊畫面。行為與重構前的 main.c 主迴圈一致：
//   待機 → 搖一搖或按 BOOT → 動畫 → 結果停住 → BOOT／左右晃／等 3 秒收掉 → 冷卻 3 秒 → 顯示提示
//
// 原本 main.c 在每個分支都要呼叫 drop_gestures() 把沒輪到的手勢取走，
// 否則動畫或冷卻期間累積的一次晃動會在解禁瞬間立刻生效。改成事件模型之後，
// main.c 每輪都把手勢取出來派發，這個畫面不處理就等於丟掉，效果相同但不必到處記得清。
#include "cast_screen.h"

#include "cast.h"
#include "cast_ui.h"
#include "draw_screen.h"
#include "reading_screen.h"
#include "screen_mgr.h"
#include "shrine_screen.h"
#include "esp_log.h"
#include "esp_timer.h"

#define COOLDOWN_US   (3LL * 1000 * 1000)
// 結果剛出現時手通常還在動，這段時間只認按鍵，不然筊象會來不及看就被收掉
#define HOLD_GUARD_US (800LL * 1000)
// 結果停住 3 秒沒人收就自己收掉。儀式要能一路走完，不該卡在等使用者做一個沒有意義的動作
#define AUTO_CLEAR_US (3LL * 1000 * 1000)

static const char *TAG = "cast";

static int64_t s_ready_at_us;
static int64_t s_hold_since_us;
static bool s_hint_shown;

// 擲筊確認模式：求籤後進來，收掉結果時依筊象決定去向
static bool s_confirm;
static cast_result_t s_last_result;
// 切畫面要等收掉結果之後才做，不能在 clear_result 裡直接跳——
// 那支是從主迴圈的事件派發呼叫的，跳完還會繼續碰到已經不是目前畫面的狀態
static const screen_t *s_pending_goto;

void cast_screen_confirm(void) { s_confirm = true; }

static bool can_throw(void)
{
    return !cast_ui_busy() && !cast_ui_holding() && esp_timer_get_time() >= s_ready_at_us;
}

static void throw_blocks(const char *why)
{
    cast_result_t r = cast_draw();
    s_last_result = r;
    ESP_LOGI(TAG, "%s：%s", why, cast_result_name(r));
    cast_ui_play(r);
}

static void clear_result(const char *why)
{
    // 聖筊進解籤閣、陰筊回求籤，兩者都是換畫面，就不要先回到預備畫面——
    // 那一閃看起來像在叫人再擲一次。畫面留在結果上直到下一輪 tick 換走，
    // 待機狀態改在 enter() 復原
    if (s_confirm && (s_last_result == CAST_SHENG || s_last_result == CAST_YIN)) {
        s_pending_goto = (s_last_result == CAST_SHENG) ? &reading_screen : &draw_screen;
        ESP_LOGI(TAG, "%s：%s，直接進「%s」", why, cast_result_name(s_last_result),
                 s_pending_goto->name);
        s_hold_since_us = 0;
        return;
    }

    // 笑筊與立筊都是「神明沒給答案」，留在原地重擲。確認模式下冷卻歸零，
    // 不必等三秒才能再問；立筊十萬分之一，不另做處置
    int64_t cooldown = s_confirm ? 0 : COOLDOWN_US;
    ESP_LOGI(TAG, "%s：收掉結果，冷卻 %lld 秒", why, cooldown / 1000000);
    cast_ui_reset();
    cast_ui_show_note(NULL);
    s_ready_at_us = cooldown ? esp_timer_get_time() + cooldown : 0;
    s_hold_since_us = 0;
    s_hint_shown = false;
}

// 確認模式的說法要指向下一步，不然使用者不知道收掉結果之後會發生什麼
static void apply_prompt(void)
{
    if (s_confirm) {
        cast_ui_set_prompt("搖一搖　請示", "求得此籤　擲筊請示神明");
    } else {
        cast_ui_set_prompt(NULL, NULL);
    }
}

static const char *result_note(cast_result_t r)
{
    switch (r) {
    case CAST_SHENG: return "聖筊　神明應允　收筊後入解籤閣";
    case CAST_XIAO:  return "笑筊　神明未答　請再擲一次";
    case CAST_YIN:   return "陰筊　神明不允　收筊後重新求籤";
    case CAST_LI:    return "立筊　極為罕見　請再擲一次";
    }
    return "";
}

static esp_err_t enter(void)
{
    cast_ui_show();
    // 上一次可能是停在結果上直接換走的（聖筊、陰筊），待機狀態在這裡復原
    cast_ui_reset();
    apply_prompt();
    cast_ui_show_note(NULL);
    s_pending_goto = NULL;
    s_ready_at_us = 0;
    s_hold_since_us = 0;
    s_hint_shown = false;
    return ESP_OK;
}

static void exit_(void)
{
    // 模式不跨畫面殘留：下次從正殿進來一定是自由擲筊
    s_confirm = false;
    s_pending_goto = NULL;
}

static void tick(void)
{
    if (s_pending_goto) {
        const screen_t *next = s_pending_goto;
        s_pending_goto = NULL;
        screen_mgr_goto(next);
        return;
    }

    int64_t now = esp_timer_get_time();

    // 結果剛停住的那一刻記時，左右晃的保護期從這裡算起
    if (cast_ui_holding()) {
        if (s_hold_since_us == 0) {
            s_hold_since_us = now;
            // 自由擲筊維持原樣：結果畫面只有放大的筊，沒有文字
            if (s_confirm) cast_ui_show_note(result_note(s_last_result));
        } else if (now - s_hold_since_us >= AUTO_CLEAR_US) {
            clear_result("停留 3 秒");
        }
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

    case SCREEN_EV_WAVE:
        // 只在結果停著、且過了保護期之後才認
        if (cast_ui_holding() && s_hold_since_us != 0 &&
            esp_timer_get_time() - s_hold_since_us >= HOLD_GUARD_US) {
            clear_result("左右晃動");
            return true;
        }
        return false;

    // 長按與左右滑都是離開。動畫跑到一半不讓走——筊還在空中就換頁，
    // 動畫的 timer 會繼續在看不見的畫面上跑
    case SCREEN_EV_BOOT_HOLD:
    case SCREEN_EV_SWIPE_LEFT:
    case SCREEN_EV_SWIPE_RIGHT:
        if (cast_ui_busy()) return false;
        cast_ui_reset();
        s_ready_at_us = 0;
        s_hold_since_us = 0;
        s_hint_shown = false;
        screen_mgr_goto(&shrine_screen);
        return true;
    }
    return false;
}

const screen_t cast_screen = {
    .name = "擲筊",
    .enter = enter,
    .exit = exit_,
    .tick = tick,
    .on_event = on_event,
};

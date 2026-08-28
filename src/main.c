// 擲筊：把板子當筊杯，快速上下甩一次就擲一次，畫面演出兩片筊落地並拉近看筊象。
// 結果會一直停在畫面上，按鍵或左右晃動才收掉；收掉後要等冷卻時間才能再擲。
//
// 按鍵分工：PWR 專職電源，短按軟關機（AXP2101 切電，μA 等級）、長按是 PMIC 硬體斷電；
// BOOT 是功能鍵，擲筊與收結果都走它。PWR 不佔 GPIO，讀 AXP2101 的 INTSTS2 就知道有沒有短按。
#include <inttypes.h>
#include <stdbool.h>

#include "audio.h"
#include "board.h"
#include "cast.h"
#include "cast_ui.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.h"

#define POLL_MS     50
#define COOLDOWN_US (3LL * 1000 * 1000)
// 開機淡入、關機淡出：面板亮度（SH8601 0x51），讓兩個狀態轉換看得出來
#define BRIGHT_FULL 0xFF
#define FADE_IN_MS  800
#define FADE_OUT_MS 600
// 結果剛出現時手通常還在動，這段時間只認按鍵，不然筊象會來不及看就被收掉
#define HOLD_GUARD_US (800LL * 1000)

static const char *TAG = "app";

static int64_t s_ready_at_us;
static int64_t s_hold_since_us;
static bool s_hint_shown;

static void wait_button_released(void)
{
    while (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳
}

// LVGL 起來後，面板暫存器和畫面資料共用同一條 QSPI。
// 不先拿走 LVGL 的鎖就寫亮度，指令會回報成功但面板不理，畫面完全不動
static void fade(uint8_t from, uint8_t to, uint32_t ms)
{
    lvgl_port_lock(0);
    board_display_fade(from, to, ms);
    lvgl_port_unlock();
}

static void power_off(void)
{
    ESP_LOGI(TAG, "PWR 鍵短按：軟關機，按 PWR 鍵才會再開機");
    fade(BRIGHT_FULL, 0, FADE_OUT_MS);
    board_display_on(false);
    board_speaker_power(false);

    esp_err_t err = board_power_off();

    // 正常會在上一行斷電。還活著表示 PMIC 沒切（例如 USB 仍在供電），把畫面還原繼續跑
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGW(TAG, "軟關機未生效（%s），繼續運作", esp_err_to_name(err));
    board_speaker_power(true);
    board_display_on(true);
    fade(0, BRIGHT_FULL, FADE_IN_MS);
}

static void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

// 手勢是背景任務產生的，沒輪到它的狀態也要定期取走，
// 否則動畫或冷卻期間累積的一次晃動會在解禁的瞬間立刻生效
static void drop_gestures(void)
{
    imu_take_shake();
    imu_take_swipe();
}

static void clear_result(const char *why)
{
    ESP_LOGI(TAG, "%s：收掉結果，冷卻 %lld 秒", why, COOLDOWN_US / 1000000);
    cast_ui_reset();
    s_ready_at_us = esp_timer_get_time() + COOLDOWN_US;
    s_hint_shown = false;
    drop_gestures();
}

static void throw_blocks(const char *why)
{
    cast_result_t r = cast_draw();
    ESP_LOGI(TAG, "%s：%s", why, cast_result_name(r));
    cast_ui_play(r);
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(cast_ui_init());
    button_init();

    // 音效與感測器各自壞掉都還有另一半可以玩，不值得讓整台開不起來
    esp_err_t err = audio_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "音訊初始化失敗（%s），本次無聲", esp_err_to_name(err));

    err = imu_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "IMU 初始化失敗（%s），只能用 BOOT 鍵擲", esp_err_to_name(err));

    ESP_LOGI(TAG, "就緒，剩餘堆積 %" PRIu32 " bytes", esp_get_free_heap_size());
    board_pmic_log();

    // 開機動畫：畫面已經備好，壓暗再淡入。亮度控制若無效，畫面就是維持全亮，不會變黑磚
    lvgl_port_lock(0);
    board_display_brightness(0);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    fade(0, BRIGHT_FULL, FADE_IN_MS);

    int prev_level = 1;
    while (1) {
        bool pwr = board_pwrkey_short_pressed();
        bool boot = false;

        int level = gpio_get_level(BOARD_BOOT_GPIO);
        if (prev_level == 1 && level == 0) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳後再確認一次
            if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
                wait_button_released();
                boot = true;
                level = 1;
            }
        }
        prev_level = level;

        // PWR 是電源鍵，任何狀態下短按都關機
        if (pwr) {
            power_off();
            drop_gestures();
        } else if (cast_ui_holding()) {
            int64_t now = esp_timer_get_time();
            if (s_hold_since_us == 0) s_hold_since_us = now;

            bool by_gesture = now - s_hold_since_us >= HOLD_GUARD_US && imu_take_swipe();
            if (boot || by_gesture) {
                clear_result(boot ? "BOOT 鍵" : "左右晃動");
                s_hold_since_us = 0;
            } else {
                drop_gestures();
            }
        } else if (cast_ui_busy() || esp_timer_get_time() < s_ready_at_us) {
            drop_gestures();
        } else {
            if (!s_hint_shown) {
                cast_ui_show_hint();
                s_hint_shown = true;
            }
            if (imu_take_shake()) {
                throw_blocks("搖動");
            } else if (boot) {
                throw_blocks("BOOT 鍵");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

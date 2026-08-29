// 擲筊：把板子當筊杯，快速上下甩一次就擲一次，畫面演出兩片筊落地並拉近看筊象。
// 結果會一直停在畫面上，按鍵或左右晃動才收掉；收掉後要等冷卻時間才能再擲。
//
// 按鍵分工：PWR 專職電源，短按軟關機（AXP2101 切電，μA 等級）、長按是 PMIC 硬體斷電；
// BOOT 是功能鍵，擲筊與收結果都走它。PWR 不佔 GPIO，讀 AXP2101 的 INTSTS2 就知道有沒有短按。
#include <inttypes.h>
#include <stdbool.h>

#include "audio.h"
#include "board.h"
#include "cast_screen.h"
#include "cast_ui.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.h"
#include "screen_mgr.h"
#include "verify_s3.h"

// M0/S3 驗證建置（pio run -e verify-s3）：擲筊整支讓開，見 verify_s3.c。
// 分岔放在這裡而不是只包住 app_main，否則驗證建置下所有 static 函式都會變成 unused
#ifdef APP_VERIFY_S3

void app_main(void) { verify_s3_run(); }

#else

#define POLL_MS     50
// 開機淡入、關機淡出：面板亮度（SH8601 0x51），讓兩個狀態轉換看得出來
#define BRIGHT_FULL 0xFF
#define FADE_IN_MS  800
#define FADE_OUT_MS 600

static const char *TAG = "app";

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

// PWR 是電源鍵，任何畫面下短按都關機，所以留在主迴圈全域攔截，不進 screen
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

    ESP_ERROR_CHECK(screen_mgr_init(&cast_screen));

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
        // PWR 專職電源，不經過 screen
        if (board_pwrkey_short_pressed()) {
            power_off();
        }

        int level = gpio_get_level(BOARD_BOOT_GPIO);
        if (prev_level == 1 && level == 0) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳後再確認一次
            if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
                wait_button_released();
                screen_mgr_dispatch(SCREEN_EV_BOOT_KEY);
                level = 1;
            }
        }
        prev_level = level;

        // 手勢每輪都取走再派發。目前畫面不處理就等於丟掉——
        // 這樣動畫或冷卻期間累積的晃動不會在解禁瞬間突然生效
        if (imu_take_shake()) screen_mgr_dispatch(SCREEN_EV_SHAKE);
        if (imu_take_swipe()) screen_mgr_dispatch(SCREEN_EV_SWIPE);

        screen_mgr_tick();
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

#endif   // APP_VERIFY_S3

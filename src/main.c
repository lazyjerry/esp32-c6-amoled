// 擲筊：把板子當筊杯，快速上下甩一次就擲一次，畫面演出兩片筊落地並拉近看筊象。
// 結果會一直停在畫面上，按鍵或左右晃動才收掉；收掉後要等冷卻時間才能再擲。
//
// 按鍵分工：PWR 專職電源，短按軟關機（AXP2101 切電，μA 等級）、長按是 PMIC 硬體斷電；
// BOOT 是功能鍵，短按做目前畫面的主要動作、長按回正殿。
// PWR 不佔 GPIO，讀 AXP2101 的 INTSTS2 就知道有沒有短按。
#include <inttypes.h>
#include <stdbool.h>

#include "audio.h"
#include "board.h"
#include "cast_ui.h"
#include "content.h"
#include "display.h"
#include "driver/gpio.h"
#include "error_screen.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.h"
#include "records.h"
#include "screen_mgr.h"
#include "settings.h"
#include "shrine_screen.h"
#include "touch.h"
#include "verify_s3.h"

// 驗證建置（pio run -e verify-s3）：擲筊整支讓開。
// 分岔放在這裡而不是只包住 app_main，否則驗證建置下所有 static 函式都會變成 unused
#ifdef APP_VERIFY_S3

void app_main(void) { verify_s3_run(); }

#else

#define POLL_MS     50
// 按滿這麼久就算長按（離開目前畫面）。短於此的放開才算短按
#define HOLD_MS     900
// 開機淡入、關機淡出：面板亮度（SH8601 0x51），讓兩個狀態轉換看得出來。
// 目標亮度是使用者在設定頁調過的值，不是寫死的滿亮
#define FADE_IN_MS  800
#define FADE_OUT_MS 600

static const char *TAG = "app";

// 按住期間就地等，跟改造前一樣不讓事件連發。長按滿 HOLD_MS 當場發出去，
// 不等放開——手指還壓著就看到畫面切走，才知道長按已經生效
static void dispatch_boot_key(void)
{
    int64_t pressed_at = esp_timer_get_time();
    bool held = false;

    while (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
        if (!held && esp_timer_get_time() - pressed_at >= HOLD_MS * 1000LL) {
            held = true;
            screen_mgr_dispatch(SCREEN_EV_BOOT_HOLD);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳

    if (!held) screen_mgr_dispatch(SCREEN_EV_BOOT_KEY);
}

// LVGL 起來後，面板暫存器和畫面資料共用同一條 QSPI，寫亮度前要先拿走 LVGL 的鎖，
// 不然兩邊會同時用同一條匯流排
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
    fade(settings_brightness(), 0, FADE_OUT_MS);
    board_display_on(false);
    board_speaker_power(false);

    esp_err_t err = board_power_off();

    // 正常會在上一行斷電。還活著表示 PMIC 沒切（例如 USB 仍在供電），把畫面還原繼續跑
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGW(TAG, "軟關機未生效（%s），繼續運作", esp_err_to_name(err));
    board_speaker_power(true);
    board_display_on(true);
    fade(0, settings_brightness(), FADE_IN_MS);
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
    ESP_ERROR_CHECK(display_init());
    button_init();

    // 音效與感測器各自壞掉都還有另一半可以玩，不值得讓整台開不起來
    esp_err_t err = audio_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "音訊初始化失敗（%s），本次無聲", esp_err_to_name(err));

    err = imu_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "IMU 初始化失敗（%s），只能用 BOOT 鍵擲", esp_err_to_name(err));

    // 觸控壞掉還有 BOOT 鍵與 IMU 可以操作，不值得讓整台開不起來
    err = touch_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "觸控初始化失敗（%s），本次無觸控", esp_err_to_name(err));

    // 紀錄壞掉只是留不下參拜簿，儀式本身照樣走得完
    err = records_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "參拜紀錄初始化失敗（%s），本次不留紀錄", esp_err_to_name(err));

    // 音量與亮度是使用者調過的偏好，讀不回來就用預設值，不值得中斷開機
    err = settings_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "設定讀取失敗（%s），本次用預設值", esp_err_to_name(err));
    audio_set_volume(settings_volume());

    // 語料讀不到就停在錯誤畫面，不建立擲筊畫面。
    // 讓裝置「看起來能用」只會把問題留到求到籤卻沒有籤詩的那一刻
    esp_err_t content_err = content_mount();
    if (content_err != ESP_OK) {
        error_screen_set(esp_err_to_name(content_err));
        ESP_ERROR_CHECK(screen_mgr_init(&error_screen));
    } else {
        ESP_ERROR_CHECK(cast_ui_init());
        ESP_ERROR_CHECK(screen_mgr_init(&shrine_screen));
    }

    ESP_LOGI(TAG, "就緒，剩餘堆積 %" PRIu32 " bytes", esp_get_free_heap_size());
    board_pmic_log();

    // 開機動畫：畫面已經備好，壓暗再淡入。亮度控制若無效，畫面就是維持全亮，不會變黑磚
    lvgl_port_lock(0);
    board_display_brightness(0);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    fade(0, settings_brightness(), FADE_IN_MS);

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
                dispatch_boot_key();
                level = 1;
            }
        }
        prev_level = level;

        // 手勢每輪都取走再派發。目前畫面不處理就等於丟掉——
        // 這樣動畫或冷卻期間累積的晃動不會在解禁瞬間突然生效
        if (imu_take_shake()) screen_mgr_dispatch(SCREEN_EV_SHAKE);
        if (imu_take_swipe()) screen_mgr_dispatch(SCREEN_EV_WAVE);

        // 觸控滑動同樣是取走即清除：判定在 LVGL 任務裡做，這裡只負責派發
        switch (touch_take_swipe()) {
        case TOUCH_SWIPE_LEFT:  screen_mgr_dispatch(SCREEN_EV_SWIPE_LEFT); break;
        case TOUCH_SWIPE_RIGHT: screen_mgr_dispatch(SCREEN_EV_SWIPE_RIGHT); break;
        case TOUCH_SWIPE_NONE:  break;
        }

        screen_mgr_tick();
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

#endif   // 驗證建置分岔

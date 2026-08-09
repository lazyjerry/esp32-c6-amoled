// 擲筊：把板子當筊杯，快速上下甩一次就擲一次，畫面演出兩片筊落地並拉近看筊象。
//
// 按鍵配置沿用天氣看板的結論：BOOT 在 GPIO9 喚不醒 deep sleep，所以休眠只能走 light sleep
// 且喚醒源只能是它；PWR 鍵不佔 GPIO，讀 AXP2101 的 INTSTS2 就知道有沒有短按。
#include <inttypes.h>
#include <stdbool.h>

#include "audio.h"
#include "board.h"
#include "cast.h"
#include "cast_ui.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu.h"

#define POLL_MS 50

static const char *TAG = "app";

static void wait_button_released(void)
{
    while (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳
}

static void enter_sleep(void)
{
    ESP_LOGI(TAG, "進入 light sleep，按 BOOT 鍵喚醒（USB 序列埠會斷線，醒來後重新列舉）");
    vTaskDelay(pdMS_TO_TICKS(200));
    board_display_on(false);
    board_speaker_power(false);

    ESP_ERROR_CHECK(gpio_wakeup_enable(BOARD_BOOT_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    esp_light_sleep_start();
    gpio_wakeup_disable(BOARD_BOOT_GPIO);

    wait_button_released();
    board_speaker_power(true);
    board_display_on(true);
    ESP_LOGI(TAG, "已喚醒");
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

static void throw_blocks(const char *why)
{
    if (cast_ui_busy()) return;

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
    if (err != ESP_OK) ESP_LOGW(TAG, "IMU 初始化失敗（%s），只能用 PWR 鍵擲", esp_err_to_name(err));

    ESP_LOGI(TAG, "就緒，剩餘堆積 %" PRIu32 " bytes", esp_get_free_heap_size());

    int prev_level = 1;
    while (1) {
        int level = gpio_get_level(BOARD_BOOT_GPIO);
        if (prev_level == 1 && level == 0) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳後再確認一次
            if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
                wait_button_released();
                enter_sleep();
                level = 1;
            }
        }
        prev_level = level;

        if (imu_take_shake()) throw_blocks("搖動");
        if (board_pwrkey_short_pressed()) throw_blocks("PWR 鍵");

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

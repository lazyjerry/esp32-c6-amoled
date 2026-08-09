// 天氣看板：每小時更新一次，PWR 鍵手動刷新，BOOT 鍵切休眠／長按重新配網。
//
// 按鍵配置的由來：C6 只有 GPIO0~7 是 LP IO，而這片板子 0~5 給了 LCD QSPI、
// 6 是 SD CS、7 是 I2C SCL，沒有一支接到按鍵。BOOT 鍵在 GPIO9，喚不醒 deep sleep，
// 所以電源鍵走 light sleep，而且喚醒源只能是 BOOT，刷新只好交給 PWR 鍵。
#include <inttypes.h>
#include <stdbool.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "ui.h"
#include "weather.h"

#define UPDATE_INTERVAL_US (3600LL * 1000 * 1000)
#define RETRY_INTERVAL_US  (300LL * 1000 * 1000)
#define WIFI_TIMEOUT_MS    15000
#define POLL_MS            50
#define LONG_PRESS_MS      3000

static const char *TAG = "app";

static int64_t s_last_update_us;
static bool s_have_data;

static void update_weather(void)
{
    ui_set_status("更新中");

    if (net_connect(WIFI_TIMEOUT_MS) == ESP_OK) {
        weather_t w = {0};
        esp_err_t err = weather_fetch(&w);
        net_disconnect();   // 一小時才用一次射頻，取完就關

        if (err == ESP_OK) {
            ui_show_weather(&w);
            ui_set_status("");
            s_have_data = true;
        } else {
            ui_set_status("更新失敗");
        }
    } else {
        ui_set_status("網路失敗");
    }

    // 失敗也要記時間，否則重試條件永遠成立會變成連續重打
    s_last_update_us = esp_timer_get_time();

    // LVGL 雙緩衝與 TLS handshake 都吃堆積，餘量掉太低就要調小 DRAW_BUF_ROWS
    ESP_LOGI(TAG, "剩餘堆積 %" PRIu32 " bytes", esp_get_free_heap_size());
}

static void run_provisioning(void)
{
    net_prov_info_t info = {0};
    ESP_ERROR_CHECK(net_start_provisioning(&info));
    ui_show_provisioning(info.ap_ssid, info.pop, info.qr_payload);

    net_wait_provisioning();   // 阻塞到使用者設定完成
    ESP_LOGI(TAG, "配網完成");
    ui_show_weather_screen();
}

// 回傳按住的毫秒數；沒按著回 0
static int wait_button_released(void)
{
    int held = 0;
    while (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        held += POLL_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳
    return held;
}

static void enter_sleep(void)
{
    ESP_LOGI(TAG, "進入 light sleep，按 BOOT 鍵喚醒（USB 序列埠會斷線，醒來後重新列舉）");
    ui_set_status("休眠");
    vTaskDelay(pdMS_TO_TICKS(300));
    board_display_on(false);
    net_disconnect();

    ESP_ERROR_CHECK(gpio_wakeup_enable(BOARD_BOOT_GPIO, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    esp_light_sleep_start();
    gpio_wakeup_disable(BOARD_BOOT_GPIO);

    wait_button_released();
    board_display_on(true);
    ui_set_status("");
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

// 短按切休眠，長按清掉 WiFi 憑證重開回配網
static void handle_boot_key(void)
{
    int held = wait_button_released();

    if (held >= LONG_PRESS_MS) {
        ESP_LOGW(TAG, "BOOT 長按 %d ms，重新配網", held);
        ui_set_status("重新配網");
        net_erase_credentials();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    enter_sleep();
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(ui_init());
    button_init();

    ui_set_status("連線中");
    ESP_ERROR_CHECK(net_init());

    if (net_needs_provisioning()) {
        run_provisioning();
    }
    update_weather();

    int prev_level = 1;
    while (1) {
        int level = gpio_get_level(BOARD_BOOT_GPIO);
        if (prev_level == 1 && level == 0) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));   // 去彈跳後再確認一次
            if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
                handle_boot_key();
                level = 1;
            }
        }
        prev_level = level;

        if (board_pwrkey_short_pressed()) {
            ESP_LOGI(TAG, "PWR 鍵短按，手動刷新");
            update_weather();
        }

        int64_t now = esp_timer_get_time();
        // 沒資料時不等滿一小時，讓開機失敗的情況能自己補回來
        int64_t due = s_have_data ? UPDATE_INTERVAL_US : RETRY_INTERVAL_US;
        if (now - s_last_update_us >= due) {
            update_weather();
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

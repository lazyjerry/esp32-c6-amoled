// 【已歸檔】第一段（驅動可用性）已通過並寫進 src/rtc.c；
// 第二段（AXP2101 軟關機保時）決定不驗，直接採用不通過的處置——
// 參拜紀錄只存遞增序號，農曆／初一十五／日夜氛圍整批砍除。見企劃 §8 S2。
// 日後若要補驗保時，把這支放回 src/ 並恢復 verify-s2 環境即可。
// M0/S2：PCF85063 讀寫，以及 AXP2101 軟關機之後時間還在不在。
//
// 這一項決定 M3 的範圍：保時就能存參拜日期、做農曆與初一十五；
// 不保時就只能存「第 N 次」，農曆整批砍掉。
//
// 分兩段，因為第二段需要人：
//   第一段（自動）：驅動可用性——設定時間、讀回、確認真的在走
//   第二段（要人）：把時間寫進 NVS 當基準 → 軟關機 → 等 → 按 PWR 開機 → 比對
//
// 開機時若 NVS 裡有上次關機前的基準，會自動做第二段的比對並印出結論，
// 所以流程是：燒進去 → 跑第一段 → 按 BOOT 觸發軟關機 → 等十分鐘 → 按 PWR → 看 log。
//
// 燒錄：pio run -e verify-s2 -t upload
#include "verify_s2.h"

#include <string.h>
#include <time.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rtc.h"

#define NVS_NS       "s2"
#define KEY_MARK     "mark"      // 軟關機前記下的 RTC 時間
#define KEY_UPTIME   "uptime"    // 記錄當下已開機多久，用來估算關機時長的下限

static const char *TAG = "s2";

static void fmt(const struct tm *t, char *buf, size_t n)
{
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d（週%d）",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec, t->tm_wday);
}

// 回傳兩個時間相差幾秒；用 mktime 換算，避免自己處理跨月跨年
static long diff_seconds(const struct tm *a, const struct tm *b)
{
    struct tm ta = *a, tb = *b;
    return (long)difftime(mktime(&ta), mktime(&tb));
}

// 開機時若 NVS 有基準，代表上一輪是走軟關機關掉的，比對就是 S2 的答案
static void check_after_power_cycle(nvs_handle_t nvs)
{
    struct tm mark;
    size_t len = sizeof(mark);
    if (nvs_get_blob(nvs, KEY_MARK, &mark, &len) != ESP_OK || len != sizeof(mark)) {
        ESP_LOGI(TAG, "NVS 沒有上次關機前的基準，這是第一輪");
        return;
    }

    struct tm now;
    if (rtc_get_time(&now) != ESP_OK) {
        ESP_LOGE(TAG, "讀不到 RTC，無法比對");
        return;
    }

    bool stopped = false;
    rtc_oscillator_stopped(&stopped);

    char a[48], b[48];
    fmt(&mark, a, sizeof(a));
    fmt(&now, b, sizeof(b));
    long delta = diff_seconds(&now, &mark);

    ESP_LOGW(TAG, "===== 軟關機保時比對 =====");
    ESP_LOGW(TAG, "  關機前 %s", a);
    ESP_LOGW(TAG, "  開機後 %s", b);
    ESP_LOGW(TAG, "  相差 %ld 秒（%.1f 分鐘）", delta, delta / 60.0);
    ESP_LOGW(TAG, "  振盪器停止旗標：%s", stopped ? "有停過（時間不可信）" : "沒停過");

    if (stopped) {
        ESP_LOGE(TAG, "  → S2 不通過：軟關機切斷了 RTC 供電");
        ESP_LOGE(TAG, "    處置：參拜簿只存「第 N 次」，農曆／初一十五整批砍掉");
    } else if (delta <= 0) {
        ESP_LOGE(TAG, "  → 可疑：時間沒有前進或倒退了，再跑一次確認");
    } else {
        ESP_LOGW(TAG, "  → S2 通過：軟關機期間 RTC 持續走時");
        ESP_LOGW(TAG, "    M3 可以存日期，農曆與初一十五可做");
    }
    nvs_erase_key(nvs, KEY_MARK);
    nvs_commit(nvs);
}

static void arm_and_power_off(nvs_handle_t nvs)
{
    struct tm now;
    if (rtc_get_time(&now) != ESP_OK) {
        ESP_LOGE(TAG, "讀不到 RTC，不進行軟關機");
        return;
    }
    char s[48];
    fmt(&now, s, sizeof(s));
    ESP_LOGW(TAG, "記下基準 %s，即將軟關機", s);

    ESP_ERROR_CHECK(nvs_set_blob(nvs, KEY_MARK, &now, sizeof(now)));
    int64_t up = esp_timer_get_time();
    ESP_ERROR_CHECK(nvs_set_i64(nvs, KEY_UPTIME, up));
    ESP_ERROR_CHECK(nvs_commit(nvs));

    ESP_LOGW(TAG, "等十分鐘以上再按 PWR 開機，開機後會自動比對並印出結論");
    vTaskDelay(pdMS_TO_TICKS(500));

    board_display_on(false);
    board_speaker_power(false);
    esp_err_t err = board_power_off();
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGW(TAG, "軟關機未生效（%s）——USB 還在供電的話 PMIC 切不掉，"
                  "拔掉 USB 用電池跑才測得準", esp_err_to_name(err));
}

void verify_s2_run(void)
{
    ESP_LOGI(TAG, "M0/S2 開始：PCF85063 讀寫與軟關機保時");
    ESP_ERROR_CHECK(board_init());

    esp_err_t nerr = nvs_flash_init();
    if (nerr == ESP_ERR_NVS_NO_FREE_PAGES || nerr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nerr = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nerr);

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &nvs));

    if (rtc_init() != ESP_OK) {
        ESP_LOGE(TAG, "PCF85063 初始化失敗——I2C 掃得到 0x51 嗎？");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 先比對上一輪的結果（若有），這是 S2 真正的答案
    check_after_power_cycle(nvs);

    // 第一段：驅動可用性
    bool stopped = false;
    ESP_ERROR_CHECK(rtc_oscillator_stopped(&stopped));
    ESP_LOGW(TAG, "① 振盪器停止旗標：%s", stopped ? "有停過" : "沒停過");

    struct tm t = {0};
    if (stopped) {
        // 沒有網路可對時，設一個固定的已知時間當起點就夠了
        t.tm_year = 2026 - 1900;
        t.tm_mon = 8 - 1;
        t.tm_mday = 29;
        t.tm_hour = 10;
        t.tm_min = 0;
        t.tm_sec = 0;
        t.tm_wday = 6;   // 2026-08-29 是週六
        ESP_ERROR_CHECK(rtc_set_time(&t));
        ESP_LOGW(TAG, "② 時間不可信，已設為 2026-08-29 10:00:00");
    } else {
        ESP_LOGW(TAG, "② 時間可信，不覆寫");
    }

    struct tm a, b;
    ESP_ERROR_CHECK(rtc_get_time(&a));
    char sa[48], sb[48];
    fmt(&a, sa, sizeof(sa));
    ESP_LOGW(TAG, "③ 目前時間 %s", sa);

    ESP_LOGI(TAG, "④ 等 5 秒確認真的在走…");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_ERROR_CHECK(rtc_get_time(&b));
    fmt(&b, sb, sizeof(sb));
    long d = diff_seconds(&b, &a);
    ESP_LOGW(TAG, "④ 5 秒後 %s，相差 %ld 秒 → %s",
             sb, d, (d >= 4 && d <= 7) ? "走時正常" : "異常");

    ESP_LOGW(TAG, "===== 第一段完成 =====");
    ESP_LOGW(TAG, "第二段需要人：按 BOOT 鍵記下基準並軟關機，"
                  "等十分鐘以上按 PWR 開機，會自動比對");
    ESP_LOGW(TAG, "注意：USB 供電時 AXP2101 切不掉電，要拔 USB 用電池才測得準");

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    while (1) {
        if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(BOARD_BOOT_GPIO) == 0) {
                while (gpio_get_level(BOARD_BOOT_GPIO) == 0) vTaskDelay(pdMS_TO_TICKS(50));
                arm_and_power_off(nvs);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

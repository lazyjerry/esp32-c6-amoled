// M0/S3：SPIFFS 建置整合、掛載時間與讀檔延遲。
//
// 驗證整條資料鏈路：data/*.json → gen-content.sh → spiffs_content/
// → spiffs_create_partition_image() → storage 分區 → 裝置讀出籤詩。
//
// 企劃 §8 S3 的通過條件：掛載 < 1s、讀一篇籤詩 < 100ms。
// 不通過的處置：改用 read-only FAT，或把語料放 app 分區 const。
//
// 燒錄：pio run -e verify-s3 -t upload
#include "verify_s3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MOUNT_POINT "/spiffs"
#define POEMS_PATH  MOUNT_POINT "/poems.json"

static const char *TAG = "s3";

// 回傳掛載耗時（微秒），失敗回 -1
static int64_t mount_spiffs(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 4,
        // 建置時就把 image 燒進去了，執行期不該格式化——真要格式化代表燒錯了，
        // 這時靜默重建一個空檔案系統只會讓問題更難查
        .format_if_mount_failed = false,
    };

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    int64_t us = esp_timer_get_time() - t0;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "掛載失敗（%s）", esp_err_to_name(err));
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "  找不到 storage 分區——檢查 partitions.csv");
        } else if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "  分區存在但掛不起來——image 可能沒燒進去，"
                          "確認 CMakeLists 的 spiffs_create_partition_image 有 FLASH_IN_PROJECT");
        }
        return -1;
    }

    size_t total = 0, used = 0;
    if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "分區容量 %u bytes，已用 %u bytes（%.1f%%）",
                 (unsigned)total, (unsigned)used, total ? used * 100.0 / total : 0);
    }
    return us;
}

// 讀整份 poems.json 並解析，回傳耗時（微秒）；失敗回 -1
static int64_t read_and_parse(cJSON **out_root, char **out_buf)
{
    int64_t t0 = esp_timer_get_time();

    FILE *f = fopen(POEMS_PATH, "rb");
    if (!f) {
        ESP_LOGE(TAG, "打不開 %s——gen-content.sh 跑過了嗎？", POEMS_PATH);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "配不到 %ld bytes——檔案太大，要改成串流解析", size);
        return -1;
    }
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    int64_t us = esp_timer_get_time() - t0;

    if (!root) {
        ESP_LOGE(TAG, "JSON 解析失敗：%s", cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
        free(buf);
        return -1;
    }
    ESP_LOGI(TAG, "讀檔 + 解析 %ld bytes 花 %lld ms", size, us / 1000);

    *out_root = root;
    *out_buf = buf;
    return us;
}

// 從已解析的 JSON 取第 no 籤，量單次查表耗時
static int64_t lookup_poem(cJSON *root, int no, const char **out_text)
{
    int64_t t0 = esp_timer_get_time();
    cJSON *poems = cJSON_GetObjectItem(root, "poems");
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, poems) {
        cJSON *n = cJSON_GetObjectItem(item, "n");
        if (cJSON_IsNumber(n) && n->valueint == no) {
            cJSON *t = cJSON_GetObjectItem(item, "t");
            *out_text = cJSON_IsString(t) ? t->valuestring : NULL;
            return esp_timer_get_time() - t0;
        }
    }
    *out_text = NULL;
    return -1;
}

void verify_s3_run(void)
{
    ESP_LOGI(TAG, "M0/S3 開始：SPIFFS 建置整合與讀取");

    int64_t mount_us = mount_spiffs();
    if (mount_us < 0) {
        ESP_LOGE(TAG, "S3 失敗在掛載這一步");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGW(TAG, "① 掛載耗時 %lld ms（通過條件 < 1000ms）→ %s",
             mount_us / 1000, mount_us < 1000000 ? "通過" : "不通過");

    cJSON *root = NULL;
    char *buf = NULL;
    int64_t read_us = read_and_parse(&root, &buf);
    if (read_us < 0) {
        ESP_LOGE(TAG, "S3 失敗在讀檔這一步");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    int count = cJSON_GetArraySize(cJSON_GetObjectItem(root, "poems"));
    ESP_LOGW(TAG, "② 語料共 %d 首（預期 63）→ %s", count, count == 63 ? "通過" : "不通過");

    // 抽幾首驗內容，順便量查表延遲。挑頭、中、尾與三支額外籤
    static const int probe[] = {1, 27, 47, 60, 61, 63};
    int64_t worst = 0;
    for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
        const char *text = NULL;
        int64_t us = lookup_poem(root, probe[i], &text);
        if (us < 0 || !text) {
            ESP_LOGE(TAG, "③ 第 %d 籤查不到", probe[i]);
            continue;
        }
        if (us > worst) worst = us;
        ESP_LOGI(TAG, "③ 第 %2d 籤（%lld us）%s", probe[i], us, text);
    }

    // 企劃的通過條件是「讀一篇籤詩 < 100ms」。整份讀進來之後查表是記憶體操作，
    // 真正的成本在第一次讀檔，所以兩者都要報
    int64_t worst_case_ms = (read_us + worst) / 1000;
    ESP_LOGW(TAG, "④ 冷讀一篇籤詩 = 讀檔解析 %lld ms + 查表 %lld us = %lld ms（通過條件 < 100ms）→ %s",
             read_us / 1000, worst, worst_case_ms, worst_case_ms < 100 ? "通過" : "不通過");

    cJSON_Delete(root);
    free(buf);

    ESP_LOGW(TAG, "===== S3 摘要 =====");
    ESP_LOGW(TAG, "掛載 %lld ms／讀檔解析 %lld ms／查表最差 %lld us",
             mount_us / 1000, read_us / 1000, worst);
    ESP_LOGW(TAG, "剩餘堆積 %u bytes", (unsigned)esp_get_free_heap_size());

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

#include "content.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

static const char *TAG = "content";

esp_err_t content_mount(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = CONTENT_MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 4,
        // 內容是建置時燒進去的，執行期格式化只會把問題藏起來：
        // 靜默生出一個空檔案系統，畫面照樣起得來但一首籤也沒有
        .format_if_mount_failed = false,
    };

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "掛載 storage 失敗（%s）", esp_err_to_name(err));
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "  找不到 storage 分區——檢查 partitions.csv");
        } else if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "  分區在但掛不起來——映像多半沒燒進去，跑 ./scripts/flash-content.sh");
        }
        return err;
    }

    // PlatformIO 不會產生也不會燒 SPIFFS 映像，所以「掛得起來、檔案卻不在」
    // 是這個專案最可能出現的狀態，一定要驗到檔案這一層
    FILE *f = fopen(CONTENT_POEMS_PATH, "rb");
    if (!f) {
        ESP_LOGE(TAG, "掛載成功但讀不到 %s——跑 ./scripts/gen-content.sh 再 flash-content.sh",
                 CONTENT_POEMS_PATH);
        esp_vfs_spiffs_unregister(conf.partition_label);
        return ESP_ERR_NOT_FOUND;
    }
    fclose(f);

    size_t total = 0, used = 0;
    esp_spiffs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "語料就緒，掛載 %lld ms，已用 %u / %u bytes",
             (esp_timer_get_time() - t0) / 1000, (unsigned)used, (unsigned)total);
    return ESP_OK;
}

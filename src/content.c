#include "content.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

static const char *TAG = "content";

static int s_count;

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
    s_count = 0;
    ESP_LOGI(TAG, "語料就緒，掛載 %lld ms，已用 %u / %u bytes",
             (esp_timer_get_time() - t0) / 1000, (unsigned)used, (unsigned)total);
    return ESP_OK;
}

// 讀整個檔進記憶體。呼叫端負責 free
static char *slurp(const char *path, long *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        ESP_LOGE(TAG, "配不到 %ld bytes", size);
        return NULL;
    }
    size_t got = fread(buf, 1, size, f);
    buf[got] = '\0';
    fclose(f);
    if (out_size) *out_size = (long)got;
    return buf;
}

static void copy_field(cJSON *item, const char *key, char *dst, size_t cap)
{
    cJSON *v = cJSON_GetObjectItem(item, key);
    if (cJSON_IsString(v) && v->valuestring) {
        strlcpy(dst, v->valuestring, cap);
    } else {
        dst[0] = '\0';
    }
}

esp_err_t content_get_poem(int no, content_poem_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    char *buf = slurp(CONTENT_POEMS_PATH, NULL);
    if (!buf) return ESP_ERR_NOT_FOUND;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "poems.json 解析失敗");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    cJSON *poems = cJSON_GetObjectItem(root, "poems");
    s_count = cJSON_GetArraySize(poems);

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, poems) {
        cJSON *n = cJSON_GetObjectItem(item, "n");
        if (!cJSON_IsNumber(n) || n->valueint != no) continue;

        // 欄位名是 gen-content.sh 縮短過的，不是 data/poems.json 的原名
        out->no = no;
        copy_field(item, "m", out->name, sizeof(out->name));
        copy_field(item, "g", out->ganzhi, sizeof(out->ganzhi));
        copy_field(item, "k", out->trigram, sizeof(out->trigram));
        copy_field(item, "a", out->attr, sizeof(out->attr));
        copy_field(item, "t", out->text, sizeof(out->text));
        // 本文留空的籤還沒核對完，不該出現在求籤結果裡
        err = out->text[0] ? ESP_OK : ESP_ERR_INVALID_STATE;
        break;
    }

    cJSON_Delete(root);
    if (err != ESP_OK) ESP_LOGW(TAG, "第 %d 籤取不到（%s）", no, esp_err_to_name(err));
    return err;
}

int content_poem_count(void)
{
    if (s_count > 0) return s_count;

    char *buf = slurp(CONTENT_POEMS_PATH, NULL);
    if (!buf) return 0;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return 0;
    s_count = cJSON_GetArraySize(cJSON_GetObjectItem(root, "poems"));
    cJSON_Delete(root);
    return s_count;
}

esp_err_t content_get_reading(int no, int cat, char *out, size_t cap)
{
    if (!out || cap == 0) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';

    char *buf = slurp(CONTENT_MOUNT_POINT "/readings.json", NULL);
    if (!buf) return ESP_ERR_NOT_FOUND;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return ESP_ERR_INVALID_STATE;

    esp_err_t err = ESP_ERR_NOT_FOUND;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, cJSON_GetObjectItem(root, "readings")) {
        cJSON *n = cJSON_GetObjectItem(item, "n");
        if (!cJSON_IsNumber(n) || n->valueint != no) continue;

        cJSON *t = cJSON_GetObjectItem(item, "t");
        cJSON *one = cJSON_GetArrayItem(t, cat);
        if (cJSON_IsString(one) && one->valuestring) {
            strlcpy(out, one->valuestring, cap);
            err = ESP_OK;
        }
        break;
    }

    cJSON_Delete(root);
    return err;
}

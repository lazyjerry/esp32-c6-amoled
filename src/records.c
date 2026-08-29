#include "records.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS      "shrine"
#define K_TOTAL "total"
#define K_LOG   "log"

static const char *TAG = "records";

static nvs_handle_t s_nvs;
static bool s_ready;
static uint32_t s_total;
// 環形緩衝：第 n 次參拜固定落在 (n-1) % RECORDS_MAX，不必搬動就能覆蓋最舊的
static record_t s_ring[RECORDS_MAX];

esp_err_t records_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要重建（%s），既有紀錄會消失", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &s_nvs), TAG, "nvs open");

    // 第一次開機時兩個 key 都還不存在，那是 0 筆，不是錯誤
    err = nvs_get_u32(s_nvs, K_TOTAL, &s_total);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_total = 0;
    } else {
        ESP_RETURN_ON_ERROR(err, TAG, "讀 total");
    }

    size_t len = sizeof(s_ring);
    err = nvs_get_blob(s_nvs, K_LOG, s_ring, &len);
    if (err != ESP_OK || len != sizeof(s_ring)) {
        // 長度對不上表示 RECORDS_MAX 改過。舊資料的環形位置不再成立，
        // 硬讀只會讓參拜簿顯示錯位的內容，不如從頭來過
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "紀錄讀不回來（%s），清空重來", esp_err_to_name(err));
        }
        memset(s_ring, 0, sizeof(s_ring));
    }

    s_ready = true;
    ESP_LOGI(TAG, "參拜紀錄就緒，累計 %" PRIu32 " 次", s_total);
    return ESP_OK;
}

uint32_t records_total(void) { return s_total; }

esp_err_t records_add(int cat, int poem, uint16_t *out_seq)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    // 序號存 u16。一天拜十次要十八年才會繞回去，不值得為它多存兩個 byte
    s_total++;
    record_t r = {.seq = (uint16_t)s_total, .cat = (uint8_t)cat, .poem = (uint8_t)poem};
    s_ring[(s_total - 1) % RECORDS_MAX] = r;

    // 兩個 key 不可能一起原子寫入。先寫內容再寫總數，中間斷電就是
    // 「多寫了一筆還看不到的紀錄」，比「序號跳號」好收拾
    esp_err_t err = nvs_set_blob(s_nvs, K_LOG, s_ring, sizeof(s_ring));
    if (err == ESP_OK) err = nvs_set_u32(s_nvs, K_TOTAL, s_total);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "寫入失敗（%s）", esp_err_to_name(err));
        s_total--;
        return err;
    }

    ESP_LOGI(TAG, "第 %u 次參拜：類別 %d、第 %d 籤", r.seq, cat, poem);
    if (out_seq) *out_seq = r.seq;
    return ESP_OK;
}

int records_recent(record_t *out, int max)
{
    if (!s_ready || !out || max <= 0) return 0;

    int n = (int)(s_total < RECORDS_MAX ? s_total : RECORDS_MAX);
    if (n > max) n = max;

    for (int i = 0; i < n; i++) {
        // s_total 是最新那筆的序號，往回數
        uint32_t seq = s_total - i;
        out[i] = s_ring[(seq - 1) % RECORDS_MAX];
    }
    return n;
}

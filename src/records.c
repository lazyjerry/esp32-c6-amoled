#include "records.h"

#include <inttypes.h>
#include <string.h>

#include "cast.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS      "shrine"
#define K_TOTAL "total"
#define K_LOG   "log"
#define K_STATS "stats"
#define K_POEMS "poems"

static const char *TAG = "records";

static nvs_handle_t s_nvs;
static bool s_ready;
static uint32_t s_total;
// 環形緩衝：第 n 次參拜固定落在 (n-1) % RECORDS_MAX，不必搬動就能覆蓋最舊的
static record_t s_ring[RECORDS_MAX];

static stats_t s_stats;
// 求中的籤：[類別][籤號] 的次數。384 bytes，直接用籤號當索引就不必找位置。
// u8 飽和在 255——同一支籤求中 255 次之後統計停在那裡，比多花一倍空間划算
static uint8_t s_poems[RITUAL_CAT_COUNT][RECORDS_POEM_MAX];
static bool s_dirty;

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

    // 統計與籤詩次數各自獨立：任一個讀不回來就只清那一個，不牽連其他
    len = sizeof(s_stats);
    err = nvs_get_blob(s_nvs, K_STATS, &s_stats, &len);
    if (err != ESP_OK || len != sizeof(s_stats)) memset(&s_stats, 0, sizeof(s_stats));

    len = sizeof(s_poems);
    err = nvs_get_blob(s_nvs, K_POEMS, s_poems, &len);
    if (err != ESP_OK || len != sizeof(s_poems)) memset(s_poems, 0, sizeof(s_poems));

    s_ready = true;
    ESP_LOGI(TAG, "參拜紀錄就緒，累計 %" PRIu32 " 次、擲筊 %" PRIu32 " 次",
             s_total, s_stats.casts);
    return ESP_OK;
}

const stats_t *records_stats(void) { return &s_stats; }

void records_count_cast(int result)
{
    if (!s_ready) return;
    s_stats.casts++;
    switch (result) {
    case CAST_SHENG: s_stats.sheng++; break;
    case CAST_XIAO:  s_stats.xiao++;  break;
    case CAST_YIN:   s_stats.yin++;   break;
    case CAST_LI:    s_stats.li++;    break;
    default: break;
    }
    s_dirty = true;
}

void records_count_tell(int cat)
{
    if (!s_ready || cat < 0 || cat >= RITUAL_CAT_COUNT) return;
    s_stats.tells[cat]++;
    s_dirty = true;
}

esp_err_t records_flush(void)
{
    if (!s_ready || !s_dirty) return ESP_OK;

    esp_err_t err = nvs_set_blob(s_nvs, K_STATS, &s_stats, sizeof(s_stats));
    if (err == ESP_OK) err = nvs_set_blob(s_nvs, K_POEMS, s_poems, sizeof(s_poems));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "統計寫入失敗（%s）", esp_err_to_name(err));
        return err;   // dirty 留著，下次再試
    }
    s_dirty = false;
    return ESP_OK;
}

// 最近求中的籤。順序直接沿用環形紀錄，不必另存「最後一次是第幾號」——
// 同一支籤再次求中時，新的那筆排在前面，舊的那次被視為重複跳過，
// 效果就是「重複的拉到最上方」
int records_recent_poems(poem_stat_t *out, int max)
{
    if (!s_ready || !out || max <= 0) return 0;

    record_t rec[RECORDS_MAX];
    int n = records_recent(rec, RECORDS_MAX);

    int m = 0;
    for (int i = 0; i < n && m < max; i++) {
        int cat = rec[i].cat, poem = rec[i].poem;
        if (cat < 0 || cat >= RITUAL_CAT_COUNT || poem <= 0 || poem >= RECORDS_POEM_MAX) continue;

        bool dup = false;
        for (int j = 0; j < m; j++) {
            if (out[j].cat == cat && out[j].poem == poem) { dup = true; break; }
        }
        if (dup) continue;   // 前面已經有更新的一次了

        out[m].cat = (uint8_t)cat;
        out[m].poem = (uint8_t)poem;
        out[m].count = s_poems[cat][poem];
        m++;
    }
    return m;
}

esp_err_t records_add(int cat, int poem, uint16_t *out_seq)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    // 序號存 u16。一天拜十次要十八年才會繞回去，不值得為它多存兩個 byte
    s_total++;
    record_t r = {.seq = (uint16_t)s_total, .cat = (uint8_t)cat, .poem = (uint8_t)poem};
    s_ring[(s_total - 1) % RECORDS_MAX] = r;

    s_stats.worships++;
    if (cat >= 0 && cat < RITUAL_CAT_COUNT && poem > 0 && poem < RECORDS_POEM_MAX) {
        if (s_poems[cat][poem] < 255) s_poems[cat][poem]++;
    }

    // 兩個 key 不可能一起原子寫入。先寫內容再寫總數，中間斷電就是
    // 「多寫了一筆還看不到的紀錄」，比「序號跳號」好收拾
    esp_err_t err = nvs_set_blob(s_nvs, K_LOG, s_ring, sizeof(s_ring));
    if (err == ESP_OK) err = nvs_set_u32(s_nvs, K_TOTAL, s_total);
    if (err == ESP_OK) err = nvs_set_blob(s_nvs, K_STATS, &s_stats, sizeof(s_stats));
    if (err == ESP_OK) err = nvs_set_blob(s_nvs, K_POEMS, s_poems, sizeof(s_poems));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "寫入失敗（%s）", esp_err_to_name(err));
        s_total--;
        s_stats.worships--;
        return err;
    }
    // 參拜完成這一刻順便把待寫的計數帶過去了
    s_dirty = false;

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

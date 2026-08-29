#include "settings.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS       "cfg"
#define K_VOL    "vol"
#define K_BRIGHT "bright"

static const char *TAG = "settings";

static nvs_handle_t s_nvs;
static bool s_ready;
static uint8_t s_vol = SETTINGS_VOL_DEFAULT;
static uint8_t s_bright = SETTINGS_BRIGHT_DEFAULT;

// 讀不到就用預設值。第一次開機兩個 key 都不存在，那不是錯誤
static void load_u8(const char *key, uint8_t *out)
{
    uint8_t v = 0;
    if (nvs_get_u8(s_nvs, key, &v) == ESP_OK) *out = v;
}

esp_err_t settings_init(void)
{
    // records_init() 通常已經初始化過，重複呼叫是安全的；
    // 兩支誰先誰後都不該影響另一支能不能用
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &s_nvs), TAG, "nvs open");

    load_u8(K_VOL, &s_vol);
    load_u8(K_BRIGHT, &s_bright);
    if (s_vol > 100) s_vol = SETTINGS_VOL_DEFAULT;
    if (s_bright < SETTINGS_BRIGHT_MIN) s_bright = SETTINGS_BRIGHT_MIN;

    s_ready = true;
    ESP_LOGI(TAG, "設定就緒，音量 %u、亮度 %u", s_vol, s_bright);
    return ESP_OK;
}

uint8_t settings_volume(void) { return s_vol; }
uint8_t settings_brightness(void) { return s_bright; }

static esp_err_t store_u8(const char *key, uint8_t v)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_u8(s_nvs, key, v);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "寫入 %s 失敗（%s）", key, esp_err_to_name(err));
    return err;
}

esp_err_t settings_set_volume(uint8_t percent)
{
    s_vol = percent > 100 ? 100 : percent;
    return store_u8(K_VOL, s_vol);
}

esp_err_t settings_set_brightness(uint8_t level)
{
    s_bright = level < SETTINGS_BRIGHT_MIN ? SETTINGS_BRIGHT_MIN : level;
    return store_u8(K_BRIGHT, s_bright);
}

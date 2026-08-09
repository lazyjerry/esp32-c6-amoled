#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

// 回應約 400 bytes，留四倍餘裕；超過就截斷讓 JSON 解析失敗，不會爆記憶體
#define RESP_MAX 1600

static const char *TAG = "weather";

static const char URL_FMT[] =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=%s&longitude=%s"
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
    "&timezone=%s";

static esp_err_t http_get(const char *url, char *buf, size_t buf_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) goto out;

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP %d", status);
        err = ESP_FAIL;
        goto out;
    }

    int total = 0;
    while (total < (int)buf_len - 1) {
        int n = esp_http_client_read(client, buf + total, buf_len - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    err = (total > 0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;

out:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t parse(const char *json, weather_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;

    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (!cJSON_IsObject(cur)) goto out;

    cJSON *t  = cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m");
    cJSON *ap = cJSON_GetObjectItemCaseSensitive(cur, "apparent_temperature");
    cJSON *rh = cJSON_GetObjectItemCaseSensitive(cur, "relative_humidity_2m");
    cJSON *ws = cJSON_GetObjectItemCaseSensitive(cur, "wind_speed_10m");
    cJSON *wc = cJSON_GetObjectItemCaseSensitive(cur, "weather_code");
    cJSON *tm = cJSON_GetObjectItemCaseSensitive(cur, "time");

    if (!cJSON_IsNumber(t) || !cJSON_IsNumber(wc)) goto out;

    out->temp_c   = (float)t->valuedouble;
    out->feels_c  = cJSON_IsNumber(ap) ? (float)ap->valuedouble : out->temp_c;
    out->humidity = cJSON_IsNumber(rh) ? rh->valueint : -1;
    out->wind_kmh = cJSON_IsNumber(ws) ? (float)ws->valuedouble : -1.0f;
    out->wmo_code = wc->valueint;
    if (cJSON_IsString(tm) && tm->valuestring) {
        snprintf(out->observed_at, sizeof(out->observed_at), "%s", tm->valuestring);
    } else {
        out->observed_at[0] = '\0';
    }
    err = ESP_OK;

out:
    cJSON_Delete(root);
    return err;
}

esp_err_t weather_fetch(weather_t *out)
{
    char url[320];
    snprintf(url, sizeof(url), URL_FMT,
             APP_WEATHER_LAT, APP_WEATHER_LON, APP_WEATHER_TZ_ENC);

    char *body = malloc(RESP_MAX);
    if (body == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = http_get(url, body, RESP_MAX);
    if (err == ESP_OK) {
        err = parse(body, out);
        if (err != ESP_OK) ESP_LOGE(TAG, "JSON 解析失敗：%.120s", body);
    }
    free(body);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s  %.1f°C 體感 %.1f 濕度 %d%% 風速 %.1f code=%d",
                 out->observed_at, out->temp_c, out->feels_c,
                 out->humidity, out->wind_kmh, out->wmo_code);
    }
    return err;
}

// WMO 4677 天氣代碼，Open-Meteo 用的子集
const char *weather_code_text(int wmo_code)
{
    switch (wmo_code) {
        case 0:  return "晴";
        case 1:  return "晴時多雲";
        case 2:  return "多雲";
        case 3:  return "陰";
        case 45: return "有霧";
        case 48: return "霧淞";
        case 51: return "毛毛雨";
        case 53: return "毛毛雨";
        case 55: return "毛毛雨";
        case 56: case 57: return "凍毛雨";
        case 61: return "小雨";
        case 63: return "雨";
        case 65: return "大雨";
        case 66: case 67: return "凍雨";
        case 71: return "小雪";
        case 73: return "雪";
        case 75: return "大雪";
        case 77: return "冰珠";
        case 80: return "陣雨";
        case 81: return "陣雨";
        case 82: return "強陣雨";
        case 85: return "陣雪";
        case 86: return "強陣雪";
        case 95: return "雷雨";
        case 96: case 99: return "雷雨冰雹";
        default: return "無資料";
    }
}

// 對應 src/fonts/font_icon_72.c 收的碼位；Arial Unicode 沒有閃電，雷雨用 U+2607
const char *weather_code_symbol(int wmo_code)
{
    switch (wmo_code) {
        case 0:  case 1:  return "☀";
        case 2:  case 3:  return "☁";
        case 45: case 48: return "≈";
        case 71: case 73: case 75: case 77:
        case 85: case 86: return "☃";
        case 95: case 96: case 99: return "☇";
        default: return "☂";
    }
}

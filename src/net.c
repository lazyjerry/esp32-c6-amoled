#include "net.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_softap.h"
#include "nvs_flash.h"

#define GOT_IP_BIT BIT0
#define FAILED_BIT BIT1
#define MAX_RETRY  5

static const char *TAG = "net";

static EventGroupHandle_t s_events;
static bool s_started;
static bool s_connected;
static bool s_provisioned;
static bool s_provisioning;
static int s_retry;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    // 配網期間 STA 的連線由 network_prov_mgr 主導（它要靠連線結果驗證使用者填的憑證），
    // 這裡再自己 connect／重連會和它的狀態機打架
    if (s_provisioning && base == WIFI_EVENT) return;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "斷線，第 %d 次重連", s_retry);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "取得 IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        s_connected = true;
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

// 用 MAC 後三碼組出每片板子固定、彼此不同的熱點名稱與 PoP
static void device_ids(char *ssid, size_t ssid_len, char *pop, size_t pop_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);   // 不需 wifi 已啟動
    snprintf(ssid, ssid_len, "WEATHER_%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(pop, pop_len, "%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t net_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();   // SoftAP 配網要用

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL), TAG, "wifi evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL), TAG, "ip evt");

    network_prov_mgr_config_t prov_cfg = {
        .scheme = network_prov_scheme_softap,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
    };
    ESP_RETURN_ON_ERROR(network_prov_mgr_init(prov_cfg), TAG, "prov init");
    ESP_RETURN_ON_ERROR(network_prov_mgr_is_wifi_provisioned(&s_provisioned), TAG, "prov query");
    ESP_LOGI(TAG, "WiFi 憑證：%s", s_provisioned ? "已存在" : "未設定，需配網");

    if (s_provisioned) {
        network_prov_mgr_deinit();
    }

    s_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_events, ESP_ERR_NO_MEM, TAG, "event group");
    return ESP_OK;
}

bool net_needs_provisioning(void) { return !s_provisioned; }

esp_err_t net_start_provisioning(net_prov_info_t *info)
{
    device_ids(info->ap_ssid, sizeof(info->ap_ssid), info->pop, sizeof(info->pop));

    // ESP SoftAP Prov App 掃的就是這串 JSON
    snprintf(info->qr_payload, sizeof(info->qr_payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"softap\"}",
             info->ap_ssid, info->pop);

    ESP_LOGI(TAG, "配網熱點 %s，PoP %s", info->ap_ssid, info->pop);
    s_provisioning = true;
    esp_err_t err = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, info->pop,
                                                        info->ap_ssid, NULL);
    if (err != ESP_OK) s_provisioning = false;
    return err;
}

void net_wait_provisioning(void)
{
    network_prov_mgr_wait();
    network_prov_mgr_deinit();
    s_provisioning = false;
    s_provisioned = true;
    // 配網流程自己會把 STA 連起來，之後 net_connect() 只是確認狀態
    s_started = true;
}

esp_err_t net_connect(uint32_t timeout_ms)
{
    if (s_connected) return ESP_OK;

    s_retry = 0;
    xEventGroupClearBits(s_events, GOT_IP_BIT | FAILED_BIT);

    if (!s_started) {
        // 不呼叫 esp_wifi_set_config：SSID 與密碼由配網流程寫進 NVS，驅動自己會讀
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
        s_started = true;
    } else {
        esp_wifi_connect();
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT | FAILED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & GOT_IP_BIT) return ESP_OK;
    ESP_LOGE(TAG, "連線 %s", (bits & FAILED_BIT) ? "失敗" : "逾時");
    return (bits & FAILED_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

void net_disconnect(void)
{
    if (!s_started) return;
    // 用 stop 而非 disconnect：light sleep 期間留著連線會被 AP 踢掉，醒來還要等逾時
    s_retry = MAX_RETRY;          // 擋掉 stop 觸發的 DISCONNECTED 事件自動重連
    esp_wifi_stop();
    s_started = false;
    s_connected = false;
}

bool net_is_connected(void) { return s_connected; }

void net_erase_credentials(void)
{
    ESP_LOGW(TAG, "清除 WiFi 憑證");
    esp_wifi_restore();
}

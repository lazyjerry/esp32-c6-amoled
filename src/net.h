#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    char ap_ssid[33];    // 配網期間板子開出來的熱點名稱
    char pop[16];        // proof of possession，手機 App 要輸入或由 QR 帶入
    char qr_payload[128];
} net_prov_info_t;

esp_err_t net_init(void);

// NVS 裡沒有可用憑證時為 true；net_init() 之後才有意義
bool net_needs_provisioning(void);

// 開啟 SoftAP 配網服務並填回 QR 內容，之後呼叫 net_wait_provisioning() 等使用者設定完
esp_err_t net_start_provisioning(net_prov_info_t *info);
void net_wait_provisioning(void);

// 連上為止或逾時；重複呼叫安全（已連線直接回 ESP_OK）
esp_err_t net_connect(uint32_t timeout_ms);

// 進休眠前關掉射頻；net_connect() 會自己重新拉起來
void net_disconnect(void);

bool net_is_connected(void);

// 清掉已存的 WiFi 憑證，下次開機會回到配網模式
void net_erase_credentials(void);

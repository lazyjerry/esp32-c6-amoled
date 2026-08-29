// 語料：storage 分區的 SPIFFS。內容在建置時燒進去，執行期只讀不寫。
#pragma once

#include "esp_err.h"

#define CONTENT_MOUNT_POINT "/spiffs"
#define CONTENT_POEMS_PATH  CONTENT_MOUNT_POINT "/poems.json"

// 掛載並確認籤詩檔讀得到。掛得起來但檔案不在也算失敗——
// 空的檔案系統一樣掛得起來，只驗掛載等於沒驗
esp_err_t content_mount(void);

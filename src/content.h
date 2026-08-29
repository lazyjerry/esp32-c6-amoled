// 語料：storage 分區的 SPIFFS。內容在建置時燒進去，執行期只讀不寫。
#pragma once

#include "esp_err.h"

#define CONTENT_MOUNT_POINT "/spiffs"
#define CONTENT_POEMS_PATH  CONTENT_MOUNT_POINT "/poems.json"

// 掛載並確認籤詩檔讀得到。掛得起來但檔案不在也算失敗——
// 空的檔案系統一樣掛得起來，只驗掛載等於沒驗
esp_err_t content_mount(void);

// 一首籤。字串長度取自實際語料再留餘裕：本文最長 4 句共 23 個中文字（69 bytes）
typedef struct {
    int no;
    char name[24];
    char ganzhi[16];
    char trigram[40];
    char attr[56];
    char text[160];
} content_poem_t;

// 籤數。掛載後才有效
int content_poem_count(void);

// 取第 no 籤（1-based）。每次都重新讀檔解析——實測 10ms，
// 換掉「整棵 cJSON 樹常駐吃掉數十 KB」的代價，求籤一輪只會呼叫一次
esp_err_t content_get_poem(int no, content_poem_t *out);

// 白話解讀。cat 是 ritual_cat_t 的序號（0~5），順序由 gen-content.sh 硬性比對過。
// 這首還沒寫解讀時回 ESP_ERR_NOT_FOUND——解讀是分批補的，缺是常態不是錯誤
#define CONTENT_READING_MAX 200
esp_err_t content_get_reading(int no, int cat, char *out, size_t cap);

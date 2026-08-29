// 【已歸檔】S4 已通過（64 列一段 32ms、31fps）。測試圖 testcard.c 有 330KB，
// 留在 src/ 只會拖慢每次編譯，結論已寫進 docs/notes/sh8601-qspi-fullscreen-throughput.md。
// 要複現就把這四個檔放回 src/ 並在 platformio.ini 恢復 verify-s4 環境。
// M0/S4：全螢幕背景圖從 flash const 繪製的實測速度。
//
// 企劃 §8 S4 的通過條件：靜態畫面切換 < 100ms。
// 不通過的處置：背景改用純色 + 少量向量元素。
//
// 只量裸繪（esp_lcd_panel_draw_bitmap），不走 LVGL。兩個理由：
//   1. 企劃要的是硬體上限，LVGL 路徑會混入圖層合成與色彩轉換的成本
//   2. 測試圖為了裸繪已經預先 byte-swap，而 LVGL 那條路設了 swap_bytes=true 會再 swap 一次，
//      兩者互斥；要同時量就得備兩份 330KB 的圖，不值得
//
// 沒有 PSRAM，368x448x2 = 330KB 的 frame buffer 放不進 512KB SRAM，所以必須分段送。
// 這裡用與 LVGL 繪圖緩衝相同的 64 列高度，數字才和實際運作可比。
//
// 燒錄：pio run -e verify-s4 -t upload
#include "verify_s4.h"

#include "board.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "testcard.h"

#define ROUNDS 5

static const char *TAG = "s4";

static int64_t blit_fullscreen(int rows_per_chunk)
{
    esp_lcd_panel_handle_t panel = board_panel();
    int64_t t0 = esp_timer_get_time();
    for (int y = 0; y < TESTCARD_H; y += rows_per_chunk) {
        int h = (y + rows_per_chunk <= TESTCARD_H) ? rows_per_chunk : (TESTCARD_H - y);
        esp_lcd_panel_draw_bitmap(panel, 0, y, TESTCARD_W, y + h,
                                  &testcard_368x448[y * TESTCARD_W]);
    }
    return esp_timer_get_time() - t0;
}

// 分段高度會影響傳輸效率（每段都有指令與位址開銷），順便量出最佳值
static void measure(int rows)
{
    int64_t total = 0, best = 0, worst = 0;
    for (int i = 0; i < ROUNDS; i++) {
        int64_t us = blit_fullscreen(rows);
        total += us;
        if (i == 0 || us < best) best = us;
        if (us > worst) worst = us;
    }
    int64_t avg = total / ROUNDS;
    double mbps = (double)(TESTCARD_W * TESTCARD_H * 2) * 1000000.0 / avg / (1024 * 1024);
    ESP_LOGW(TAG, "  %3d 列一段：平均 %lld ms（%lld~%lld）→ %.1f fps，%.2f MB/s",
             rows, avg / 1000, best / 1000, worst / 1000, 1000000.0 / avg, mbps);
}

void verify_s4_run(void)
{
    ESP_LOGI(TAG, "M0/S4 開始：全螢幕背景圖繪製速度");
    ESP_ERROR_CHECK(board_init());
    ESP_LOGI(TAG, "測試圖 %dx%d = %d bytes（在 flash，已 byte-swap）",
             TESTCARD_W, TESTCARD_H, TESTCARD_W * TESTCARD_H * 2);

    ESP_LOGW(TAG, "裸繪 esp_lcd_panel_draw_bitmap，各分段高度各跑 %d 次：", ROUNDS);
    measure(16);
    measure(32);
    measure(64);
    measure(112);   // 448 / 4，整除

    ESP_LOGW(TAG, "===== S4 摘要 =====");
    ESP_LOGW(TAG, "通過條件：靜態畫面切換 < 100ms");
    ESP_LOGW(TAG, "剩餘堆積 %u bytes", (unsigned)esp_get_free_heap_size());

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

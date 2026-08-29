// 【已歸檔】S1 驗證程式，結論：① 板載卡座不可用、③ SPI2 拆裝可行。
// 依賴當時 board.c 的診斷函式（board_exio_dump/drive/release、board_pmic_read），
// 那些仍留在 board.c。注意：暴力掃描腳位時不可把 GPIO0~5 放進候選——
// 那六支接著面板，驅動上萬次會讓板子失聯。
// M0/S1：SD 卡 sdspi 掛載，以及與面板分時共用 SPI2_HOST 的可行性驗證。
//
// C6 只有一個通用 SPI（SPI2），面板與記憶卡的腳位組不同，不可能同時開著。v3 企劃把記憶卡
// 定位成「開機時的內容匯入通道」，整條路線押在「拆得掉、裝得回來」上。這支程式驗四件事：
//
//   ① 卡片能不能以 sdspi 掛載並讀檔                     → 看 log
//   ② 拆掉 SPI 之後 AMOLED 是否還顯示最後一幀（面板自帶 GRAM）→ 看螢幕
//   ③ 切回面板能不能重建，有沒有殘影／錯色／偏移           → 看螢幕
//   ④ 讀取吞吐量，外推 11MB 內容包要多久                  → 看 log
//
// ②③只能靠人眼，程式會在該看螢幕的時候停下來並用 LOGW 提示。
// 燒錄：pio run -e verify-s1 -t upload
#include "verify_s1.h"

#include <dirent.h>
#include <stdio.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdcard.h"

#define DRAW_BUF_ROWS 64
#define BULK_PATH     SDCARD_MOUNT_POINT "/temple/bulk.bin"
#define TEMPLE_DIR    SDCARD_MOUNT_POINT "/temple"
#define READ_CHUNK    4096
// 內容包上限＝storage 分區大小，用來把實測吞吐外推成匯入耗時
#define CONTENT_BYTES (11 * 1024 * 1024)

static const char *TAG = "s1";

static lv_display_t *s_disp;

// SH8601 要求繪圖區起點偶數、終點奇數，否則整片畫面會歪掉。cast_ui.c 有孿生體
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static esp_err_t attach_display(void)
{
    const lvgl_port_display_cfg_t cfg = {
        .io_handle = board_panel_io(),
        .panel_handle = board_panel(),
        .buffer_size = BOARD_LCD_H_RES * DRAW_BUF_ROWS,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            // 面板吃的是 byte-swap 過的 RGB565（紅是 0x00F8 不是 0xF800）
            .swap_bytes = true,
        },
    };
    s_disp = lvgl_port_add_disp(&cfg);
    if (!s_disp) return ESP_FAIL;
    lv_display_add_event_cb(s_disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    return ESP_OK;
}

// 上下兩塊純色加一條白帶與文字。色塊純度高，錯色與偏移一眼就看得出來
static void paint(lv_color_t top, lv_color_t bottom, const char *text)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    lv_obj_t *a = lv_obj_create(scr);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, BOARD_LCD_H_RES, BOARD_LCD_V_RES / 2);
    lv_obj_set_pos(a, 0, 0);
    lv_obj_set_style_bg_color(a, top, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_COVER, 0);

    lv_obj_t *b = lv_obj_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, BOARD_LCD_H_RES, BOARD_LCD_V_RES / 2);
    lv_obj_set_pos(b, 0, BOARD_LCD_V_RES / 2);
    lv_obj_set_style_bg_color(b, bottom, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);

    lv_obj_t *band = lv_obj_create(scr);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, BOARD_LCD_H_RES, 60);
    lv_obj_set_pos(band, 0, BOARD_LCD_V_RES / 2 - 30);
    lv_obj_set_style_bg_color(band, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);

    // 刻意用 ASCII：中文子集字型漏字會出現豆腐方塊，那會干擾這次要判定的東西
    lv_obj_t *label = lv_label_create(band);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

    lv_screen_load(scr);
    lvgl_port_unlock();

    // 讓 LVGL 的 timer task 真的把這一幀送進面板，之後才可以拆 SPI
    vTaskDelay(pdMS_TO_TICKS(300));
}

static void look_at_screen(const char *what, int seconds)
{
    ESP_LOGW(TAG, "===== 請看螢幕：%s（%d 秒）=====", what, seconds);
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
}

static void list_temple_dir(void)
{
    DIR *dir = opendir(TEMPLE_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "① 打不開 %s——卡上沒有這個目錄，先跑 scripts/one-time/prep-sdcard.sh", TEMPLE_DIR);
        return;
    }
    int n = 0;
    for (struct dirent *e = readdir(dir); e; e = readdir(dir)) {
        ESP_LOGI(TAG, "①   %s", e->d_name);
        n++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "① %s 共 %d 個項目", TEMPLE_DIR, n);
}

// 回傳實測吞吐（bytes/sec），失敗回 0
static double measure_read(void)
{
    FILE *f = fopen(BULK_PATH, "rb");
    if (!f) {
        ESP_LOGW(TAG, "④ 打不開 %s，跳過吞吐量測", BULK_PATH);
        return 0;
    }

    static uint8_t buf[READ_CHUNK];
    size_t total = 0;
    int64_t t0 = esp_timer_get_time();
    for (size_t got = fread(buf, 1, sizeof(buf), f); got > 0; got = fread(buf, 1, sizeof(buf), f)) {
        total += got;
    }
    int64_t us = esp_timer_get_time() - t0;
    fclose(f);

    if (us <= 0 || total == 0) {
        ESP_LOGW(TAG, "④ 讀到 0 bytes，量測無效");
        return 0;
    }
    double bps = (double)total * 1000000.0 / (double)us;
    ESP_LOGI(TAG, "④ 讀 %u bytes 花 %lld ms → %.2f MB/s", (unsigned)total, us / 1000, bps / (1024 * 1024));
    ESP_LOGI(TAG, "④ 外推 11MB 內容包約需 %.1f 秒（通過條件 < 10 秒）",
             (double)CONTENT_BYTES / bps);
    return bps;
}

// 第 2 輪把 TCA9554 的 bit0~3 與 bit6 從輸入改成了輸出並拉高，用來試 SD 電源是不是掛在
// 那顆晶片上（結論：不是）。TCA9554 的狀態不隨 CPU reset 清除，所以那次的驅動還留在板子上，
// 每次開機都要先收乾淨——bit3 若是 card detect，推挽輸出會一直和外部訊號源打架。
#define EXIO_PROBE_LEFTOVER 0x4F   // bit0,1,2,3,6

static void clear_probe_leftover(void)
{
    uint8_t cfg = 0, out = 0, in = 0;
    if (board_exio_dump(&cfg, &out, &in) == ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 開機現況：config=0x%02X output=0x%02X input=0x%02X", cfg, out, in);
    }
    esp_err_t err = board_exio_release(EXIO_PROBE_LEFTOVER);
    ESP_LOGW(TAG, "收掉第 2 輪的探測殘留（bit0~3,6 設回輸入）：%s", esp_err_to_name(err));

    if (board_exio_dump(&cfg, &out, &in) == ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 收乾淨後：config=0x%02X output=0x%02X input=0x%02X", cfg, out, in);
        ESP_LOGW(TAG, "  input bit3=%d（若拔卡後變 1，bit3 就是 card detect）", (in >> 3) & 1);
    }
}

// AXP2101 電軌現況，唯讀。MISO 被外部拉低最像「卡插著但沒供電」，而本板的 PMIC
// 從來沒有配置過任何電軌（board.c 的 pmic_init() 只碰電源鍵）。先把現況攤開來看。
//
// 刻意不寫入：開一路用途不明的電軌，若預設電壓與接在上面的元件不符就是燒零件，
// 重燒韌體救不回來。要開哪一路由人決定。
static void dump_pmic_rails(void)
{
    static const struct {
        uint8_t reg;
        const char *name;
    } enables[] = {
        {0x80, "DCDC enable  "},
        {0x90, "LDO enable 0 "},
        {0x91, "LDO enable 1 "},
    };
    static const struct {
        uint8_t reg;
        const char *name;
    } volts[] = {
        {0x92, "ALDO1"}, {0x93, "ALDO2"}, {0x94, "ALDO3"}, {0x95, "ALDO4"},
        {0x96, "BLDO1"}, {0x97, "BLDO2"}, {0x98, "DLDO1"}, {0x99, "DLDO2"},
    };

    ESP_LOGW(TAG, "AXP2101 電軌現況（唯讀）：");
    uint8_t ldo_en0 = 0;
    for (size_t i = 0; i < sizeof(enables) / sizeof(enables[0]); i++) {
        uint8_t v = 0;
        if (board_pmic_read(enables[i].reg, &v) != ESP_OK) {
            ESP_LOGE(TAG, "  0x%02X %s 讀取失敗", enables[i].reg, enables[i].name);
            continue;
        }
        if (enables[i].reg == 0x90) ldo_en0 = v;
        ESP_LOGW(TAG, "  0x%02X %s = 0x%02X", enables[i].reg, enables[i].name, v);
    }

    // ALDO/BLDO 的電壓碼是 0.5V + N x 100mV
    for (size_t i = 0; i < sizeof(volts) / sizeof(volts[0]); i++) {
        uint8_t v = 0;
        if (board_pmic_read(volts[i].reg, &v) != ESP_OK) continue;
        int mv = 500 + (v & 0x1F) * 100;
        bool on = (i < 6) && ((ldo_en0 >> i) & 1);   // en0 的 bit0~5 對到 ALDO1~4、BLDO1~2
        ESP_LOGW(TAG, "  0x%02X %s = 0x%02X → %d mV，enable0 bit%d = %s",
                 volts[i].reg, volts[i].name, v, mv, (int)i, i < 6 ? (on ? "開" : "關") : "?");
    }
    ESP_LOGW(TAG, "  以上皆未寫入。要開哪一路請由人決定——開錯會燒零件");
}

// 判斷腳位上到底有沒有東西：分別帶內部上拉與下拉各讀一次。
// 兩次都跟著拉的方向跑 = 浮動，沒有東西接在上面；下拉時仍讀到高 = 有外部源在驅動或上拉。
static void dump_sd_pins(void)
{
    static const struct {
        int gpio;
        const char *name;
    } pins[] = {
        {SDCARD_PIN_CS, "CS"},
        {SDCARD_PIN_CLK, "CLK"},
        {SDCARD_PIN_MOSI, "MOSI"},
        {SDCARD_PIN_MISO, "MISO"},
    };

    ESP_LOGW(TAG, "① SD 腳位靜態電位（上拉／下拉各讀一次）：");
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_config_t up = {
            .pin_bit_mask = 1ULL << pins[i].gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&up);
        vTaskDelay(pdMS_TO_TICKS(20));
        int hi = gpio_get_level(pins[i].gpio);

        gpio_config_t down = {
            .pin_bit_mask = 1ULL << pins[i].gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        gpio_config(&down);
        vTaskDelay(pdMS_TO_TICKS(20));
        int lo = gpio_get_level(pins[i].gpio);

        const char *verdict = (hi == 1 && lo == 0) ? "浮動（沒東西接）"
                            : (hi == 1 && lo == 1) ? "被外部拉高／有卡在驅動"
                            : (hi == 0 && lo == 0) ? "被外部拉低"
                                                   : "不合常理";
        ESP_LOGW(TAG, "①   %-4s (GPIO%2d)  上拉讀=%d 下拉讀=%d  → %s",
                 pins[i].name, pins[i].gpio, hi, lo, verdict);

        // 收乾淨，免得殘留的上下拉干擾等一下的 SPI
        gpio_reset_pin(pins[i].gpio);
    }
}

// 插拔卡片會改變哪幾支腳的電位，那幾支就是卡座——不需要任何外部資料就能定位。
// 拔卡與插卡各掃一次，比對輸出即可。
//
// 避開三組會當場出事的腳位：GPIO12/13 是 USB D-/D+（動了序列埠斷線，log 也沒了）、
// GPIO24~30 是 SPI flash（動了直接當機）、GPIO7/8 是 I2C（掃完要立刻還原）。
static bool gpio_is_scannable(int gpio)
{
    if (gpio == 7 || gpio == 8) return false;     // I2C
    if (gpio == 12 || gpio == 13) return false;   // USB Serial/JTAG
    if (gpio >= 24) return false;                 // SPI flash
    return true;
}

static void scan_all_gpio(const char *label)
{
    ESP_LOGW(TAG, "① ===== 全 GPIO 掃描（%s）=====", label);
    for (int g = 0; g <= 23; g++) {
        if (!gpio_is_scannable(g)) continue;

        gpio_config_t up = {
            .pin_bit_mask = 1ULL << g,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        if (gpio_config(&up) != ESP_OK) continue;
        vTaskDelay(pdMS_TO_TICKS(5));
        int hi = gpio_get_level(g);

        gpio_config_t down = {
            .pin_bit_mask = 1ULL << g,
            .mode = GPIO_MODE_INPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        if (gpio_config(&down) != ESP_OK) continue;
        vTaskDelay(pdMS_TO_TICKS(5));
        int lo = gpio_get_level(g);

        const char *verdict = (hi == 1 && lo == 0) ? "浮動"
                            : (hi == 1 && lo == 1) ? "外部拉高"
                            : (hi == 0 && lo == 0) ? "外部拉低"
                                                   : "?";
        ESP_LOGW(TAG, "①   GPIO%-2d  up=%d dn=%d  %s", g, hi, lo, verdict);
        gpio_reset_pin(g);
    }
    ESP_LOGW(TAG, "① ===== 掃描結束（%s）=====", label);
}

// 候選腳位。第一版只放了 7 支「沒被其他周邊佔用」的（6、10、11、14、16、17、18），
// 840 組全滅。那次的錯誤是把 GPIO0~5 當成 LCD 專用而排除——但 SD 與 LCD 本來就共用 SPI2，
// 板廠讓兩者共用同一組資料線、只用不同 CS 區分是常見設計，SD CS 標在 GPIO6、
// 緊接著 LCD CS=5，正是這種佈線的特徵。掃描時面板已經拆掉，那六支腳是自由的。
//
// 仍然排除的：7/8（I2C，掃描期間還要用）、9（BOOT）、12/13（USB，動了序列埠就斷）、
// 15（TP_INT）、19~23（I2S）、24 以上（SPI flash）。
// 13 取 4 的排列共 17,160 組，實測每組約 45ms，全掃約 13 分鐘。
static esp_err_t brute_force_pins(void)
{
    static const int cand[] = {0, 1, 2, 3, 4, 5, 6, 10, 11, 14, 16, 17, 18};
    const int n = sizeof(cand) / sizeof(cand[0]);

    // 840 組失敗訊息會把序列埠淹掉，掃描期間關掉底層 log
    esp_log_level_set("sdmmc_sd", ESP_LOG_NONE);
    esp_log_level_set("sdmmc_common", ESP_LOG_NONE);
    esp_log_level_set("sdmmc_init", ESP_LOG_NONE);
    esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_NONE);
    esp_log_level_set("sdspi_host", ESP_LOG_NONE);
    esp_log_level_set("sdcard", ESP_LOG_NONE);

    ESP_LOGW(TAG, "① 開始腳位暴力掃描：13 取 4 共 17160 組，約 13 分鐘");
    int tried = 0;
    esp_err_t found = ESP_FAIL;

    for (int a = 0; a < n && found != ESP_OK; a++) {
        for (int b = 0; b < n && found != ESP_OK; b++) {
            if (b == a) continue;
            for (int c = 0; c < n && found != ESP_OK; c++) {
                if (c == a || c == b) continue;
                for (int d = 0; d < n; d++) {
                    if (d == a || d == b || d == c) continue;

                    esp_err_t err = sdcard_mount_pins(cand[a], cand[b], cand[c], cand[d]);
                    tried++;
                    if (err == ESP_OK) {
                        ESP_LOGW(TAG, "① ★★★ 掛載成功！CS=%d CLK=%d MOSI=%d MISO=%d",
                                 cand[a], cand[b], cand[c], cand[d]);
                        found = ESP_OK;
                        break;
                    }
                    if (tried % 1000 == 0) ESP_LOGW(TAG, "① 已試 %d/17160 組", tried);
                }
            }
        }
    }

    esp_log_level_set("sdcard", ESP_LOG_INFO);
    if (found != ESP_OK) ESP_LOGE(TAG, "① %d 組全部試完，沒有一組掛得起來", tried);
    return found;
}

// BSP 的命名在本專案有前科：I2S 的 DOUT/DSIN 是以 ESP 為視角而非 codec 視角
// （docs/notes/bsp-i2s-dout-is-esp-side.md）。SD 的 CMD/DATA 也可能對調，試一次成本很低。

void verify_s1_run(void)
{
    ESP_LOGI(TAG, "M0/S1 開始：SD 卡 sdspi + SPI2 分時共用");
    ESP_ERROR_CHECK(board_init());
    clear_probe_leftover();

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));
    ESP_ERROR_CHECK(attach_display());

    paint(lv_palette_main(LV_PALETTE_RED), lv_palette_main(LV_PALETTE_BLUE), "S1 STEP 1  LCD OK");
    look_at_screen("上紅下藍，白帶寫 STEP 1。顏色不對就是 swap_bytes 出問題", 6);

    // ── 拆掉面板，把 SPI2 讓給記憶卡 ──────────────────────────
    // 先停 LVGL timer，不然它會在面板已經被刪掉之後繼續 flush
    lvgl_port_stop();
    ESP_ERROR_CHECK(lvgl_port_remove_disp(s_disp));
    s_disp = NULL;

    esp_err_t err = board_panel_deinit();
    ESP_LOGI(TAG, "② board_panel_deinit()：%s", esp_err_to_name(err));
    look_at_screen("SPI 已拆。畫面若仍停在 STEP 1 → ② 通過；黑掉 → ② 失敗", 20);

    // ── ① 掛載記憶卡 ────────────────────────────────────────
    bool sd_ok = false;
    err = sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "① 首次掛載失敗（%s），進入接線診斷", esp_err_to_name(err));
        dump_sd_pins();
        dump_pmic_rails();
        scan_all_gpio("目前卡片狀態");
        err = brute_force_pins();
    }
    if (err == ESP_OK) {
        sd_ok = true;
        ESP_LOGI(TAG, "① 掛載成功");
        sdmmc_card_print_info(stdout, sdcard_handle());
        list_temple_dir();
        measure_read();
        err = sdcard_unmount();
        ESP_LOGI(TAG, "① 卸載：%s", esp_err_to_name(err));
    } else {
        ESP_LOGE(TAG, "① 掛載失敗（%s）", esp_err_to_name(err));
    }

    // ── ③ 把面板裝回來 ──────────────────────────────────────
    err = board_panel_init();
    ESP_LOGI(TAG, "③ board_panel_init()：%s", esp_err_to_name(err));
    if (err != ESP_OK) {
        // 企劃 §8 S1 子項③的既定備案：重建不了就靠軟重開機收場
        ESP_LOGE(TAG, "③ 面板重建失敗 → 匯入流程要改成「匯完直接 esp_restart()」");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    ESP_ERROR_CHECK(attach_display());
    lvgl_port_resume();
    paint(lv_palette_main(LV_PALETTE_GREEN), lv_palette_main(LV_PALETTE_YELLOW), "S1 STEP 2  REBUILT");
    look_at_screen("上綠下黃，白帶寫 STEP 2。乾淨無殘影無偏移 → ③ 通過", 15);

    ESP_LOGW(TAG, "===== S1 摘要 =====");
    ESP_LOGW(TAG, "① 掛載讀檔：%s", sd_ok ? "通過" : "失敗");
    ESP_LOGW(TAG, "② 拆 SPI 後畫面保持：看螢幕判定");
    ESP_LOGW(TAG, "③ 面板重建：程式面通過，殘影看螢幕判定");
    ESP_LOGW(TAG, "④ 吞吐量：見上方外推值");
    ESP_LOGW(TAG, "剩餘堆積 %u bytes", (unsigned)esp_get_free_heap_size());

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}

// ESP32-C6-Touch-AMOLED-1.8 (V1) 顯示測試
// 腳位與初始化序列取自官方 BSP：Waveshare-ESP32-components/bsp/esp32_c6_touch_amoled_1_8
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_SDA         8
#define I2C_SCL         7

#define TCA9554_ADDR    0x20
#define TCA9554_OUTPUT  0x01
#define TCA9554_CONFIG  0x03
#define EXIO_LCD_RST    (1 << 4)
#define EXIO_TOUCH_RST  (1 << 5)

#define TOUCH_CST820    0x15   // V2
#define TOUCH_FT5X06    0x38   // V1

#define LCD_HOST        SPI2_HOST
#define LCD_PCLK        0
#define LCD_DATA0       1
#define LCD_DATA1       2
#define LCD_DATA2       3
#define LCD_DATA3       4
#define LCD_CS          5
#define LCD_H_RES       368
#define LCD_V_RES       448
#define LCD_BPP         16

#define TP_REG_TD_STATUS  0x02   // 觸控點數，後接 0x03~0x06 為第一點 XH/XL/YH/YL
#define TP_REG_VENDOR_ID  0xA3

#define BAND_ROWS       32     // 一次送 32 列，避免配置整張 frame buffer
#define CURSOR          24     // 觸控游標邊長

static const char *TAG = "amoled";

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, 200);
}

static esp_err_t reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), 200);
}

static esp_err_t touch_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, out, len, 200);
}

// LCD 與觸控的 reset 都掛在 TCA9554，冷開機時未被驅動，不放開螢幕不會亮
static esp_err_t release_resets(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9554_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK) return err;

    uint8_t cfg = 0, out = 0;
    if ((err = reg_read(dev, TCA9554_CONFIG, &cfg)) != ESP_OK) return err;
    if ((err = reg_read(dev, TCA9554_OUTPUT, &out)) != ESP_OK) return err;

    // 只動 bit4/bit5；bit7 是喇叭電源，整片覆寫會誤動
    const uint8_t mask = EXIO_LCD_RST | EXIO_TOUCH_RST;
    if ((err = reg_write(dev, TCA9554_CONFIG, cfg & ~mask)) != ESP_OK) return err;
    if ((err = reg_write(dev, TCA9554_OUTPUT, out & ~mask)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    if ((err = reg_write(dev, TCA9554_OUTPUT, out | mask)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static void fill_screen(esp_lcd_panel_handle_t panel, uint16_t *band, uint16_t color)
{
    for (int i = 0; i < LCD_H_RES * BAND_ROWS; i++) {
        band[i] = color;
    }
    for (int y = 0; y < LCD_V_RES; y += BAND_ROWS) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y + BAND_ROWS, band));
    }
}

// SH8601 要求繪圖區域起點為偶數、終點為奇數，否則畫面會歪掉
static void draw_cursor(esp_lcd_panel_handle_t panel, uint16_t *buf, int cx, int cy, uint16_t color)
{
    int x1 = cx - CURSOR / 2, y1 = cy - CURSOR / 2;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x1 + CURSOR > LCD_H_RES) x1 = LCD_H_RES - CURSOR;
    if (y1 + CURSOR > LCD_V_RES) y1 = LCD_V_RES - CURSOR;
    x1 &= ~1;
    y1 &= ~1;

    for (int i = 0; i < CURSOR * CURSOR; i++) {
        buf[i] = color;
    }
    esp_lcd_panel_draw_bitmap(panel, x1, y1, x1 + CURSOR, y1 + CURSOR, buf);
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    ESP_ERROR_CHECK(release_resets(i2c_bus));

    if (i2c_master_probe(i2c_bus, TOUCH_FT5X06, 100) == ESP_OK) {
        ESP_LOGI(TAG, "板子版本：V1（觸控 FT5x06 @0x38，顯示 SH8601）");
    } else if (i2c_master_probe(i2c_bus, TOUCH_CST820, 100) == ESP_OK) {
        ESP_LOGE(TAG, "偵測到 V2 觸控，但本程式寫死 SH8601（V1）驅動，畫面不會正確");
    } else {
        ESP_LOGW(TAG, "找不到觸控 IC，仍繼續嘗試 SH8601");
    }

    const spi_bus_config_t spi_cfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        LCD_PCLK, LCD_DATA0, LCD_DATA1, LCD_DATA2, LCD_DATA3,
        LCD_H_RES * BAND_ROWS * (LCD_BPP / 8));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    sh8601_vendor_config_t vendor_cfg = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,   // reset 走 TCA9554，不是 GPIO
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BPP,
        .vendor_config = &vendor_cfg,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_LOGI(TAG, "SH8601 初始化完成，面板 %dx%d", LCD_H_RES, LCD_V_RES);

    uint16_t *band = heap_caps_malloc(LCD_H_RES * BAND_ROWS * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_ERROR_CHECK(band ? ESP_OK : ESP_ERR_NO_MEM);

    // RGB565，位元組序需 byte-swap 後送出
    const uint16_t COLOR_RED   = 0x00F8;
    const uint16_t COLOR_GREEN = 0xE007;
    const uint16_t COLOR_BLUE  = 0x1F00;
    const uint16_t COLOR_BLACK = 0x0000;

    // 開機自檢：確認顯示鏈路正常
    const struct { const char *name; uint16_t rgb565; } colors[] = {
        {"紅", COLOR_RED}, {"綠", COLOR_GREEN}, {"藍", COLOR_BLUE},
    };
    for (int i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        ESP_LOGI(TAG, "自檢填色：%s", colors[i].name);
        fill_screen(panel, band, colors[i].rgb565);
        vTaskDelay(pdMS_TO_TICKS(600));
    }
    fill_screen(panel, band, COLOR_BLACK);

    // 觸控 @0x38，與 PMIC 等共用同一條 I2C
    i2c_device_config_t tp_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_FT5X06,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t tp = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &tp_dev_cfg, &tp));
    uint16_t *cursor = heap_caps_malloc(CURSOR * CURSOR * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_ERROR_CHECK(cursor ? ESP_OK : ESP_ERR_NO_MEM);

    uint8_t vendor = 0;
    ESP_ERROR_CHECK(touch_read(tp, TP_REG_VENDOR_ID, &vendor, 1));
    ESP_LOGI(TAG, "觸控 vendor id=0x%02X，請觸碰螢幕", vendor);

    int tick = 0;
    while (1) {
        uint8_t buf[5] = {0};   // 0x02 TD_STATUS + 0x03~0x06 第一點座標
        esp_err_t err = touch_read(tp, TP_REG_TD_STATUS, buf, sizeof(buf));
        if (err == ESP_OK && (buf[0] & 0x0F) > 0) {
            int tx = ((buf[1] & 0x0F) << 8) | buf[2];
            int ty = ((buf[3] & 0x0F) << 8) | buf[4];
            ESP_LOGI(TAG, "觸控 x=%d y=%d（%d 點）", tx, ty, buf[0] & 0x0F);
            draw_cursor(panel, cursor, tx, ty, COLOR_GREEN);
        }
        // 每 2 秒回報一次，用來分辨「讀取失敗」與「沒碰到」
        if (++tick % 100 == 0) {
            ESP_LOGI(TAG, "等待觸控中：讀取=%s TD_STATUS=0x%02X", esp_err_to_name(err), buf[0]);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

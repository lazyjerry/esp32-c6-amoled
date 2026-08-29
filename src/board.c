// 腳位與初始化序列取自官方 BSP：Waveshare-ESP32-components/bsp/esp32_c6_touch_amoled_1_8
#include "board.h"

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_SDA 8
#define I2C_SCL 7

#define TCA9554_ADDR   0x20
#define TCA9554_INPUT  0x00
#define TCA9554_OUTPUT 0x01
#define TCA9554_CONFIG 0x03
#define EXIO_LCD_RST   (1 << 4)
#define EXIO_TOUCH_RST (1 << 5)
#define EXIO_SPK_PWR   (1 << 7)   // BSP_POWER_AMP_IO，高電位開喇叭

#define AXP2101_ADDR    0x34
#define AXP2101_COMMON  0x10
#define AXP2101_PRESS_TIME 0x27
#define AXP2101_BAT_PCT 0xA4          // gauge 算出的電量百分比。實測開機即有效，不必自己開 ADC
#define AXP2101_INTEN2  0x41
#define AXP2101_INTSTS2 0x49
#define AXP2101_PKEY_SHORT (1 << 3)   // INTSTS2 bit3 = POWERON short press
#define AXP2101_SOFF       (1 << 0)   // COMMON_CONFIG bit0 = 軟關機
#define AXP2101_PWRON_OFF  (1 << 2)   // COMMON_CONFIG bit2 = 長按 PWRON 關掉 PMIC

#define LCD_HOST  SPI2_HOST
#define LCD_PCLK  0
#define LCD_DATA0 1
#define LCD_DATA1 2
#define LCD_DATA2 3
#define LCD_DATA3 4
#define LCD_CS    5
#define LCD_BPP   16

// 送給面板的最大單次傳輸列數，同時是 LVGL 繪圖緩衝的高度
#define LCD_MAX_ROWS 32

static const char *TAG = "board";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_exio;
static i2c_master_dev_handle_t s_axp;
static uint8_t s_common_at_boot;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;

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

static esp_err_t add_dev(uint16_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

// LCD 與觸控的 reset 都掛在 TCA9554，冷開機時未被驅動，不放開螢幕不會亮
static esp_err_t release_resets(void)
{
    esp_err_t err = add_dev(TCA9554_ADDR, &s_exio);
    if (err != ESP_OK) return err;

    uint8_t cfg = 0, out = 0;
    if ((err = reg_read(s_exio, TCA9554_CONFIG, &cfg)) != ESP_OK) return err;
    if ((err = reg_read(s_exio, TCA9554_OUTPUT, &out)) != ESP_OK) return err;

    // 只動 bit4/bit5；bit7 是喇叭電源，整片覆寫會誤動
    const uint8_t mask = EXIO_LCD_RST | EXIO_TOUCH_RST;
    if ((err = reg_write(s_exio, TCA9554_CONFIG, cfg & ~mask)) != ESP_OK) return err;
    if ((err = reg_write(s_exio, TCA9554_OUTPUT, out & ~mask)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    if ((err = reg_write(s_exio, TCA9554_OUTPUT, out | mask)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t board_exio_dump(uint8_t *config, uint8_t *output, uint8_t *input)
{
    if (s_exio == NULL) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(reg_read(s_exio, TCA9554_CONFIG, config), TAG, "exio cfg");
    ESP_RETURN_ON_ERROR(reg_read(s_exio, TCA9554_OUTPUT, output), TAG, "exio out");
    return reg_read(s_exio, TCA9554_INPUT, input);
}

esp_err_t board_exio_drive(uint8_t mask, bool high)
{
    if (s_exio == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t cfg = 0, out = 0;
    ESP_RETURN_ON_ERROR(reg_read(s_exio, TCA9554_CONFIG, &cfg), TAG, "exio cfg");
    ESP_RETURN_ON_ERROR(reg_read(s_exio, TCA9554_OUTPUT, &out), TAG, "exio out");

    // 先擺好輸出值再切成輸出，免得切換瞬間送出前一次的殘值
    out = high ? (out | mask) : (out & ~mask);
    ESP_RETURN_ON_ERROR(reg_write(s_exio, TCA9554_OUTPUT, out), TAG, "exio out");
    return reg_write(s_exio, TCA9554_CONFIG, cfg & ~mask);
}

esp_err_t board_pmic_read(uint8_t reg, uint8_t *val)
{
    if (s_axp == NULL) return ESP_ERR_INVALID_STATE;
    return reg_read(s_axp, reg, val);
}

esp_err_t board_exio_release(uint8_t mask)
{
    if (s_exio == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t cfg = 0;
    ESP_RETURN_ON_ERROR(reg_read(s_exio, TCA9554_CONFIG, &cfg), TAG, "exio cfg");
    return reg_write(s_exio, TCA9554_CONFIG, cfg | mask);   // config 的 1 是輸入
}

esp_err_t board_speaker_power(bool on)
{
    if (s_exio == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t cfg = 0, out = 0;
    ESP_RETURN_ON_ERROR(reg_read(s_exio, TCA9554_CONFIG, &cfg), TAG, "exio cfg");
    ESP_RETURN_ON_ERROR(reg_read(s_exio, TCA9554_OUTPUT, &out), TAG, "exio out");

    ESP_RETURN_ON_ERROR(reg_write(s_exio, TCA9554_CONFIG, cfg & ~EXIO_SPK_PWR), TAG, "exio cfg");
    out = on ? (out | EXIO_SPK_PWR) : (out & ~EXIO_SPK_PWR);
    return reg_write(s_exio, TCA9554_OUTPUT, out);
}

static esp_err_t pmic_init(void)
{
    esp_err_t err = add_dev(AXP2101_ADDR, &s_axp);
    if (err != ESP_OK) return err;

    uint8_t inten = 0;
    if ((err = reg_read(s_axp, AXP2101_INTEN2, &inten)) != ESP_OK) return err;
    if ((err = reg_write(s_axp, AXP2101_INTEN2, inten | AXP2101_PKEY_SHORT)) != ESP_OK) return err;

    // 長按沒反應多半是 bit2 沒開。PMIC 不跟著 CPU 重置，原值只有真正斷電後的第一次開機才看得到
    if ((err = reg_read(s_axp, AXP2101_COMMON, &s_common_at_boot)) != ESP_OK) return err;
    if (!(s_common_at_boot & AXP2101_PWRON_OFF)) {
        err = reg_write(s_axp, AXP2101_COMMON, s_common_at_boot | AXP2101_PWRON_OFF);
        if (err != ESP_OK) return err;
    }

    // 開機過程本身會留下旗標，先清掉免得一啟動就當成一次按鍵
    return reg_write(s_axp, AXP2101_INTSTS2, AXP2101_PKEY_SHORT);
}

esp_err_t board_panel_init(void)
{
    const spi_bus_config_t spi_cfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        LCD_PCLK, LCD_DATA0, LCD_DATA1, LCD_DATA2, LCD_DATA3,
        BOARD_LCD_H_RES * LCD_MAX_ROWS * (LCD_BPP / 8));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &spi_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

esp_err_t board_panel_deinit(void)
{
    // 拆的順序與建的相反：面板 → panel io → bus。少拆一層，spi_bus_free 會回 ESP_ERR_INVALID_STATE
    if (s_panel) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_del(s_panel), TAG, "panel del");
        s_panel = NULL;
    }
    if (s_io) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(s_io), TAG, "panel io del");
        s_io = NULL;
    }
    return spi_bus_free(LCD_HOST);
}

esp_err_t board_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");
    ESP_RETURN_ON_ERROR(release_resets(), TAG, "tca9554");

    esp_err_t err = pmic_init();
    if (err != ESP_OK) {
        // PWR 鍵刷新會失效，但天氣與 BOOT 鍵照常，不值得讓整台開不起來
        ESP_LOGW(TAG, "AXP2101 初始化失敗（%s），PWR 鍵刷新停用", esp_err_to_name(err));
        s_axp = NULL;
    }

    ESP_RETURN_ON_ERROR(board_panel_init(), TAG, "panel");
    ESP_LOGI(TAG, "板級初始化完成，面板 %dx%d", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void) { return s_bus; }

esp_lcd_panel_handle_t board_panel(void) { return s_panel; }
esp_lcd_panel_io_handle_t board_panel_io(void) { return s_io; }

esp_err_t board_display_on(bool on)
{
    return esp_lcd_panel_disp_on_off(s_panel, on);
}

// QSPI 的一次寫入是 32 bit：opcode(0x02) << 24 | 指令 << 8 | 位址。
// 指令要往左挪 8 位，不是擺在最低的那個 byte——擺錯位置和完全不送是一樣的結果：
// SPI 傳輸照樣成功、函式照樣回 ESP_OK，面板卻收不到有效指令。
// 這是 esp_lcd_sh8601 元件內部 tx_param() 與官方 BSP 的同一段算式
#define LCD_CMD_WRITE(cmd) ((((uint32_t)(cmd) & 0xFF) << 8) | (0x02UL << 24))

esp_err_t board_display_brightness(uint8_t level)
{
    return esp_lcd_panel_io_tx_param(s_io, LCD_CMD_WRITE(0x51), &level, 1);
}

void board_display_fade(uint8_t from, uint8_t to, uint32_t ms)
{
    const uint32_t step_ms = 16;
    const uint32_t steps = ms / step_ms ? ms / step_ms : 1;
    const int delta = (int)to - (int)from;

    for (uint32_t i = 1; i <= steps; i++) {
        esp_err_t err = board_display_brightness((uint8_t)((int)from + delta * (int)i / (int)steps));
        if (err != ESP_OK) ESP_LOGW(TAG, "亮度寫入失敗：%s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
}

bool board_pwrkey_short_pressed(void)
{
    if (s_axp == NULL) return false;

    uint8_t sts = 0;
    if (reg_read(s_axp, AXP2101_INTSTS2, &sts) != ESP_OK) return false;
    if (!(sts & AXP2101_PKEY_SHORT)) return false;

    reg_write(s_axp, AXP2101_INTSTS2, AXP2101_PKEY_SHORT);   // 寫 1 清除
    return true;
}

esp_err_t board_battery_percent(uint8_t *percent)
{
    if (s_axp == NULL || percent == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(reg_read(s_axp, AXP2101_BAT_PCT, &v), TAG, "讀電量");
    // gauge 沒開或沒接電池時這個暫存器不會是合法的百分比
    if (v > 100) return ESP_ERR_NOT_SUPPORTED;
    *percent = v;
    return ESP_OK;
}

void board_pmic_log(void)
{
    if (s_axp == NULL) {
        ESP_LOGW(TAG, "AXP2101 未就緒");
        return;
    }

    // gauge 實測預設就是開的（ADC_EN=0x03），本專案因此完全不必寫 AXP2101 的任何暫存器
    uint8_t st2 = 0, pct = 0;
    reg_read(s_axp, 0x01, &st2);
    reg_read(s_axp, AXP2101_BAT_PCT, &pct);
    // STATUS2 bit[2:0]：010 定流、011 定壓、100 充飽、101 沒在充
    ESP_LOGI(TAG, "AXP2101 電量 %u%%，充電狀態 0x%02X", pct, st2 & 0x07);

    uint8_t common = 0, press = 0;
    reg_read(s_axp, AXP2101_COMMON, &common);
    reg_read(s_axp, AXP2101_PRESS_TIME, &press);
    // COMMON bit2 = 長按斷電使能；PRESS bit[1:0] 開機門檻、bit[3:2] 關機門檻（4/6/8/10 秒）
    ESP_LOGI(TAG, "AXP2101 COMMON 開機時=0x%02X 現在=0x%02X，PRESS=0x%02X",
             s_common_at_boot, common, press);
}

esp_err_t board_power_off(void)
{
    if (s_axp == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t common = 0;
    ESP_RETURN_ON_ERROR(reg_read(s_axp, AXP2101_COMMON, &common), TAG, "axp common");
    return reg_write(s_axp, AXP2101_COMMON, common | AXP2101_SOFF);
}

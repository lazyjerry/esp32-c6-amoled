// 板級硬體：I2C、TCA9554 reset、SH8601 面板、AXP2101 電源鍵
#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#define BOARD_LCD_H_RES 368
#define BOARD_LCD_V_RES 448

// BOOT 鍵。C6 的 GPIO9 不是 LP IO，只能喚醒 light sleep，喚不醒 deep sleep
#define BOARD_BOOT_GPIO 9

esp_err_t board_init(void);

// 板上 I2C 匯流排；IMU 與音訊 codec 都掛在這條上，不另開
i2c_master_bus_handle_t board_i2c_bus(void);

// 喇叭功放電源，掛在 TCA9554 的 bit7（不是 GPIO）
esp_err_t board_speaker_power(bool on);

// TCA9554 I/O 擴充（I2C 0x20）的直接存取。已知用途：bit4 LCD reset、bit5 觸控 reset、
// bit7 喇叭功放電源；bit0~3 與 bit6 用途不明。診斷未知周邊時才需要動這兩支。
esp_err_t board_exio_dump(uint8_t *config, uint8_t *output, uint8_t *input);
esp_err_t board_exio_drive(uint8_t mask, bool high);

// AXP2101 的唯讀存取。本專案目前只用它的電源鍵功能，電軌從未配置過；
// 診斷未知周邊的供電時用得上。刻意不提供寫入——開錯電軌會燒零件，不是重燒韌體能救的
esp_err_t board_pmic_read(uint8_t reg, uint8_t *val);

// 把 bit 設回輸入（高阻）。TCA9554 是獨立晶片，狀態不隨 CPU reset 清除，
// 診斷時驅動過的腳一定要自己收乾淨，否則會一直和外部訊號源打架
esp_err_t board_exio_release(uint8_t mask);

esp_lcd_panel_handle_t board_panel(void);
esp_lcd_panel_io_handle_t board_panel_io(void);

// 面板佔用 SPI2_HOST，而 C6 只有這一個通用 SPI（SOC_SPI_PERIPH_NUM=2，SPI1 屬 flash）。
// 記憶卡腳位與面板不同，同一條 bus 一次只能綁一組，所以要用 SD 卡就得先把面板整組拆掉。
// board_init() 內部已呼叫過 init，這兩支是給「拆掉換 SD、用完裝回來」的流程用的。
esp_err_t board_panel_init(void);
esp_err_t board_panel_deinit(void);

esp_err_t board_display_on(bool on);

// SH8601 的 0x51 亮度暫存器，跟 LVGL 無關
esp_err_t board_display_brightness(uint8_t level);
void board_display_fade(uint8_t from, uint8_t to, uint32_t ms);

// 電池電量百分比（0~100）。AXP2101 的 gauge 沒開或沒接電池時回 ESP_ERR_NOT_SUPPORTED
esp_err_t board_battery_percent(uint8_t *percent);

// AXP2101 的 PWR 鍵短按會在 INTSTS2 留下旗標；讀到就順手清掉。
// 長按關機是 PMIC 硬體行為，韌體攔不到也擋不掉。
bool board_pwrkey_short_pressed(void);

// 印出 AXP2101 的電源鍵設定。板子重置時原生 USB 要重新列舉，board_init() 期間的 log 抓不到，
// 要診斷得等主程式就緒後再叫。
void board_pmic_log(void);

// 軟關機：AXP2101 切掉所有電軌，耗電降到 μA 等級，按 PWR 鍵才會再開機。
// 正常情況這個呼叫不會返回；返回就表示 PMIC 沒斷電（例如 USB 仍在供電）。
esp_err_t board_power_off(void);

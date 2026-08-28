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

esp_lcd_panel_handle_t board_panel(void);
esp_lcd_panel_io_handle_t board_panel_io(void);

esp_err_t board_display_on(bool on);

// SH8601 的 0x51 亮度暫存器，跟 LVGL 無關
esp_err_t board_display_brightness(uint8_t level);
void board_display_fade(uint8_t from, uint8_t to, uint32_t ms);

// AXP2101 的 PWR 鍵短按會在 INTSTS2 留下旗標；讀到就順手清掉。
// 長按關機是 PMIC 硬體行為，韌體攔不到也擋不掉。
bool board_pwrkey_short_pressed(void);

// 印出 AXP2101 的電源鍵設定。板子重置時原生 USB 要重新列舉，board_init() 期間的 log 抓不到，
// 要診斷得等主程式就緒後再叫。
void board_pmic_log(void);

// 軟關機：AXP2101 切掉所有電軌，耗電降到 μA 等級，按 PWR 鍵才會再開機。
// 正常情況這個呼叫不會返回；返回就表示 PMIC 沒斷電（例如 USB 仍在供電）。
esp_err_t board_power_off(void);

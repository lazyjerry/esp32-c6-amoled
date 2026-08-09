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

// AXP2101 的 PWR 鍵短按會在 INTSTS2 留下旗標；讀到就順手清掉。
// 長按關機是 PMIC 硬體行為，韌體攔不到也擋不掉。
bool board_pwrkey_short_pressed(void);

// 記憶卡：C6 沒有 SDMMC host（IDF soc_caps 只有 esp32/s3/p4 有），只能走 sdspi。
//
// 卡片與面板共用 SPI2_HOST 且腳位組不同，兩者不可能同時開著。呼叫 sdcard_mount() 之前
// 必須先 board_panel_deinit()，用完 sdcard_unmount() 再 board_panel_init() 裝回去。
#pragma once

#include "esp_err.h"
#include "sdmmc_cmd.h"

#define SDCARD_MOUNT_POINT "/sdcard"

// 腳位取自官方 BSP，以 SDMMC 命名（CLK/CMD/DATA）；走 SPI 時 CMD 是 MOSI、DATA 是 MISO
#define SDCARD_PIN_CS   6
#define SDCARD_PIN_CLK  11
#define SDCARD_PIN_MOSI 10
#define SDCARD_PIN_MISO 18

// 綁 SPI2 到記憶卡腳位並掛載 FAT。不會格式化卡片：掛不起來就回錯誤，不動使用者的資料
esp_err_t sdcard_mount(void);

// 指定腳位的版本，診斷接線用。正常情況用 sdcard_mount()
esp_err_t sdcard_mount_pins(int cs, int clk, int mosi, int miso);

// 卸載並放掉 SPI2，讓面板可以重新綁回去
esp_err_t sdcard_unmount(void);

// 卡片資訊（型號、容量、速度）。未掛載時回 NULL
sdmmc_card_t *sdcard_handle(void);

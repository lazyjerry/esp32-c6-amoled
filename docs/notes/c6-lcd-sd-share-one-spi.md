---
規格: note/v1
標題: 本板無可用的 SD 卡座；面板佔著唯一的 SPI2，但可安全拆裝
分類: hardware
觸發時機: 想在 ESP32-C6-Touch-AMOLED-1.8 上用 SD 卡，或要加任何需要 SPI 的周邊時
摘要: 板載 microSD 實測掛不起來，BSP 標的腳位加上所有未佔用腳共 17160 組排列全滅，原廠韌體也沒有掛 SD 的程式碼。另一方面 C6 可用的通用 SPI 只有 SPI2_HOST 一個、被面板佔著，但實測可以安全地拆掉再裝回來，要外接 SPI 周邊時用得上。
狀態: 生效
建立日期: 2026-08-28
---

## 結論
**別在這片板子上規劃板載 SD 卡。** 實測 `esp_vfs_fat_sdspi_mount()` 掛不起來，
一路停在 `send_if_cond` 回 `0x108`（CMD8 無應答）。排除過的原因：MISO 缺上拉、
電源掛在 TCA9554、電源掛在 AXP2101（六路 LDO 實測全開）、CMD/DATA 命名顛倒。
最後把 BSP 標的腳位與所有未被其他周邊佔用的腳做窮舉，13 取 4 共 17,160 組全滅。
原廠韌體 dump 裡也只有 `bsp_spiffs_mount`，沒有任何 `bsp_sdcard_*`。
要外部儲存就外接模組，內容量不大時直接放 `storage` 分區的 SPIFFS。

**但面板可以安全地拆掉再裝回來**，這條結論獨立成立且有用：
`esp_lcd_panel_del` → `esp_lcd_panel_io_del` → `spi_bus_free(SPI2_HOST)`，
之後可用別的腳位重新 `spi_bus_initialize`，用完反向做回去，實測多輪皆 `ESP_OK`
且畫面正常重建。少拆一層，`spi_bus_free` 會回 `ESP_ERR_INVALID_STATE`。
拆之前要先 `lvgl_port_stop()` 與 `lvgl_port_remove_disp()`，否則 LVGL 的 timer task
會對著已刪掉的 panel handle 繼續 flush。`board.c` 已把
`board_panel_init()` / `board_panel_deinit()` 開出來。

**GPIO0~5 不要拿去試別的用途。** 那六支是面板的 QSPI，即使 deinit 之後面板仍然接電；
拿它們當 SPI 腳位反覆驅動，實測會讓板子連 ROM bootloader 都連不上，
要拔 USB、按住 BOOT 再上電才救得回來。

## 為什麼
- ESP-IDF 的 `soc/esp32c6/soc_caps.h` 沒有 `SOC_SDMMC_HOST_SUPPORTED`（全 IDF 只有
  esp32／esp32s3／esp32p4 有），所以 SD 只能走 sdspi
- 同一份標頭的 `SOC_SPI_PERIPH_NUM = 2`，而 `spi_types.h` 把 `SPI3_HOST` 編在 `> 2` 的條件裡；
  `SPI1_HOST` 屬 flash，使用者可用的通用 SPI 只剩 `SPI2_HOST`
- BSP 標頭寫的 `SD CLK=11 CMD=10 DATA=18 CS=6` 涵蓋整個產品線，不代表手上這片的實際配置

## 驗證
```bash
IDF=~/.platformio/packages/framework-espidf
grep -c SOC_SDMMC_HOST_SUPPORTED $IDF/components/soc/esp32c6/include/soc/soc_caps.h   # 0
grep SOC_SPI_PERIPH_NUM $IDF/components/soc/esp32c6/include/soc/soc_caps.h            # 2
strings backup/factory-16MB.bin | grep -oE "bsp_[a-z0-9_]+" | sort -u                 # 無 bsp_sdcard_*
```

完整的六輪排除過程見 `docs/knowledge-skill/M0-S1_SD卡與SPI2分時共用驗證-001/notes.md`。

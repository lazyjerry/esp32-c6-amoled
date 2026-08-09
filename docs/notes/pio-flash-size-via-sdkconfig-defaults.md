---
規格: note/v1
標題: PlatformIO 的 board_upload.flash_size 不會進 sdkconfig，要用 sdkconfig.defaults
分類: build
觸發時機: 在 PlatformIO ESP-IDF 專案要改 flash 大小、console 走 USB 等 sdkconfig 選項，或改完出現 esptool「SHA256 digest offset 不是全零」錯誤時
摘要: board_upload.flash_size 只影響上傳，不會寫進 CONFIG_ESPTOOLPY_FLASHSIZE，開機仍會警告 image header 與實體 flash 不符。要改 sdkconfig 選項就寫 sdkconfig.defaults，刪掉 sdkconfig.<env> 讓它重生；重生後第一次編譯若報 SHA256 digest 錯誤，跑一次 pio run -t clean 即可。
狀態: 生效
建立日期: 2026-08-06
---

## 結論
要改 sdkconfig 選項就寫 `sdkconfig.defaults`，然後刪掉 `sdkconfig.<env>` 讓 PlatformIO 重生。`platformio.ini` 的 `board_upload.flash_size` 只管上傳，改它不會動到 `CONFIG_ESPTOOLPY_FLASHSIZE`。重生後第一次編譯若出現 esptool `Contents of segment at SHA256 digest offset 0xb0 are not all zero`，跑一次 `pio run -t clean` 就好，不是設定寫錯。

## 為什麼
- `board_upload.flash_size` 與 sdkconfig 是兩條獨立路徑；只改前者，image header 仍用舊值，開機時 `spi_flash` 會警告 detected size 大於 header size，且分區表算出來的可用空間是錯的。
- SHA256 digest 錯誤是舊 ELF 與新設定混用的產物，屬於殘留而非設定問題。

## 驗證
```bash
rm -f sdkconfig.esp32-c6-devkitc-1
~/.platformio/penv/bin/pio run -t clean && ~/.platformio/penv/bin/pio run
grep -E '^CONFIG_ESPTOOLPY_FLASHSIZE' sdkconfig.esp32-c6-devkitc-1
```
生效後開機 log 應為 `SPI Flash Size : 16MB` 且不再出現 detected size 警告。本機實測 app 分區從 1MB 變成 4MB。

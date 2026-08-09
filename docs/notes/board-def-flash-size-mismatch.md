---
規格: note/v1
標題: board 定義的 flash 大小未必等於實際板子，需以 esptool flash_id 為準
分類: build
觸發時機: 用通用 devkit board 定義（如 esp32-c6-devkitc-1）開發模組板，或編譯報告的 Flash 容量看起來偏小時
摘要: PlatformIO board 定義寫的是參考板規格，模組板實際 flash 常更大；以 esptool flash_id 讀到的值為準，在 platformio.ini 補 board_upload.flash_size 與對應 partition 表，否則多出的容量完全用不到。
狀態: 生效
建立日期: 2026-08-06
---

## 結論
用通用 devkit board 定義跑模組板時，flash 大小以 `esptool flash_id` 讀到的為準，在 `platformio.ini` 補 `board_upload.flash_size` 與對應 partition 表。不補的話多出來的容量完全用不到，等到整合 LVGL／圖片資源才爆掉會更難查。

## 為什麼
- board 定義描述的是參考板規格，不是你手上那片；`esp32-c6-devkitc-1` 預設 4MB。
- 編譯報告的 Flash 百分比是拿 partition 表算的，不是拿實體 flash 算的，所以看起來「還很空」不代表沒問題。

## 驗證
```bash
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --port /dev/cu.usbmodem1101 flash_id
```
本機實測 `Detected flash size: 16MB`（Manufacturer `20`, Device `4018`），但 `pio run` 報告 app 分區僅 1048576 bytes。

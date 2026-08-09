---
規格: note/v1
標題: PlatformIO 自帶 cmake/ninja，ESP-IDF 專案不需另外 brew install
分類: toolchain
觸發時機: 看到教學要求 brew install cmake ninja dfu-util，準備裝系統套件前
摘要: PlatformIO Core 的 packages 目錄已含 tool-cmake 與 tool-ninja，pio run 走自帶版本；ESP32-C6 以 esptool 經 USB Serial/JTAG 燒錄不走 DFU，dfu-util 亦免。先跑一次 pio run 驗證，成功就別裝。
狀態: 生效
建立日期: 2026-08-06
---

## 結論
看到教學叫你 `brew install cmake ninja dfu-util` 時，先跑一次 `pio run`。編譯成功就別裝——PlatformIO 用的是自己 packages 目錄下的版本，裝系統套件既無效果也可能與自帶版本版號打架。

## 為什麼
- `~/.platformio/packages/` 已含 `tool-cmake`、`tool-ninja`，ESP-IDF builder 走絕對路徑呼叫自帶版本，不吃 PATH。
- ESP32-C6 用 esptool 經 USB Serial/JTAG 燒錄，不走 DFU，`dfu-util` 用不到。
- 那些教學多半針對「手動裝 ESP-IDF」路線，PlatformIO 路線不適用。

## 驗證
```bash
ls ~/.platformio/packages | grep -E 'tool-(cmake|ninja)'
~/.platformio/penv/bin/pio run
```
本機實測：cmake/ninja/dfu-util 皆未由 brew 安裝，`pio run` 仍 SUCCESS。

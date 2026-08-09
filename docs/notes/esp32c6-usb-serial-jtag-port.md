---
規格: note/v1
標題: ESP32-C6 走原生 USB，序列埠是 usbmodem 而非 usbserial，且不需裝橋接晶片驅動
分類: hardware
觸發時機: macOS 上找不到 ESP32-C6 板子的序列埠，或準備安裝 CP2102/CH340 驅動前
摘要: ESP32-C6 內建 USB Serial/JTAG，USB-C 直連 SoC，macOS 以原生 CDC-ACM 列舉為 /dev/cu.usbmodemXXXX；找不到裝置是沒接好或線材只供電，不是缺驅動。
狀態: 生效
建立日期: 2026-08-06
---

## 結論
找 ESP32-C6 的序列埠時找 `/dev/cu.usbmodem*`，不是 `/dev/cu.usbserial*`。找不到裝置**不要**去裝 CP2102/CH340 驅動，先換一條有資料線的 USB-C（很多線只供電）或換 port。板子在本機實測為 `/dev/cu.usbmodem1101`。

## 為什麼
- ESP32-C6 內建 USB Serial/JTAG 週邊，USB-C 直連 SoC，板上沒有 USB-UART 橋接晶片。
- macOS 以原生 CDC-ACM 列舉，不需第三方 kext；`usbserial` 這個命名只出現在有橋接晶片的板子。
- VID `303A` 是 Espressif，看到它就代表是原生 USB 而非橋接。

## 驗證
```bash
ls /dev/cu.*
~/.platformio/penv/bin/pio device list
```
認到時 Hardware ID 會是 `USB VID:PID=303A:1001`，Description 為 `USB JTAG/serial debug unit`。

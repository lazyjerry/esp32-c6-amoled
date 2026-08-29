---
規格: note/v1
標題: C6 韌體當機後 esptool 連不上：default_reset 沒用，usb_reset 也救不完全
分類: flash
觸發時機: ESP32-C6 燒了會當機的韌體之後 esptool 回 No serial data received，或想從當機狀態救回板子時
摘要: C6 走原生 USB 沒有實體 DTR/RTS，default_reset 觸發不了重置。--before usb_reset 進得了下載模式但 USB 會重新列舉、連線隨即失效（serial TX path seems to be down），軟體繞不過這個競態。可靠做法是拔 USB、按住 BOOT 再上電，讓板子從一開始就以 bootloader 列舉。
狀態: 生效
建立日期: 2026-08-29
---

## 結論
**燒進去的韌體會當機時，唯一可靠的救援是實體操作**：拔掉 USB → 按住 BOOT → 插回 USB → 放開 BOOT。
板子在上電當下就進下載模式，USB 從一開始列舉的就是 bootloader 的 CDC，沒有競態。
本板沒有 RESET 鍵，另一條路是 PWR 長按（AXP2101 硬體斷電，韌體攔不到）。

軟體方式各自的極限：

| `--before` | 結果 |
|---|---|
| `default_reset`（預設，也是 `pio run -t upload` 用的） | `No serial data received`。C6 走原生 USB，沒有實體 DTR/RTS 可以拉 |
| `usb_reset` | **進得了下載模式**，唯讀指令（`chip_id`）可以成功；但接著 `write_flash` 會得到 `Download mode successfully detected, but getting no sync reply: The serial TX path seems to be down` |
| `no_reset` / `no_reset_no_sync` | 板子已經在跑當機的程式，同步不到 |

`usb_reset` 失敗的原因是**進入下載模式時 USB 會重新列舉**，esptool 手上的檔案描述子隨即失效。
在 macOS 上加等待、重試、先停在下載模式再分兩段燒，全部無效——這是競態，軟體繞不過。

**預防勝於救援**：驗證性質的韌體，凡是會動到面板腳位、關中斷、或有可能無限迴圈的，
燒之前先確認救援路徑（人在旁邊、能拔線）。本專案踩過兩次，
一次是暴力掃描腳位把 `GPIO0~5` 放進候選，一次是 LVGL image 的 `stride` 沒填導致越界。

## 為什麼
- ESP32-C6 的 USB Serial/JTAG 是 SoC 內建外設，USB-C 直連 SoC（見
  `esp32c6-usb-serial-jtag-port.md`），`/dev/cu.usbmodem*` 是它列舉出來的 CDC，
  不是 CP2102/CH340 那種橋接晶片——所以沒有可以拉的 DTR/RTS 實體線
- 應用程式當機（尤其關了中斷或卡在不可搶佔的迴圈）時，處理重置請求的那段韌體邏輯也停了
- BOOT 鍵在 GPIO9，是**上電時取樣**的 strapping 腳，所以「按住再上電」有效，
  「跑起來之後才按」無效

## 驗證
```bash
ET=~/.platformio/packages/tool-esptoolpy
PYTHONPATH="$ET/_contrib:$ET" ~/.platformio/penv/bin/python -m esptool \
    --chip esp32c6 --port /dev/cu.usbmodem101 --before usb_reset chip_id
```
讀得到 MAC 表示能進下載模式；此時若 `write_flash` 仍失敗於 TX path，就只能實體操作。

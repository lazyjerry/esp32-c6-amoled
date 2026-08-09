# 韌體改 MicroPython — 可行性報告

日期：2026-08-09　｜　板子：Waveshare ESP32-C6-Touch-AMOLED-1.8（V1）

---

## 結論

**不建議做，因為目標與手段互相矛盾：要讓 MicroPython 點亮這片螢幕，必須先自己多寫一份 C。**

MicroPython 本身沒問題——ESP32-C6 是官方正式支援的 port（v1.28.0，2026-04-06）。卡點在顯示層：

- 現存唯一的 MicroPython SH8601 驅動（`dobodu/Lilygo-Waveshare_Amoled-Micropython`）**只支援 ESP32-S3**，明確不含 C6；而且它要客製韌體、**不相容 LVGL**
- LVGL 的 MicroPython 綁定 `lvgl_micropython` 內建約 30 種顯示驅動，**沒有 sh8601 也沒有 co5300**；匯流排正式支援 SPI / I80 / RGB，**QSPI 沒有列入**，只出現在社群 PR
- `machine.SPI` 沒有 quad 模式，純 Python 做不出 QSPI

所以唯一的路是把 `esp_lcd_sh8601` + QSPI panel 包成 MicroPython 的 C module 編進客製韌體——正是 dobodu 對 S3 做的事，要再移植到 C6。**為了寫 Python 而先寫 C，然後 Python 只剩上層應用邏輯。**

---

## 逐層盤點

| 層 | 現況（C） | MicroPython | 說明 |
|----|-----------|-------------|------|
| 執行環境 | ESP-IDF | ✅ | ESP32-C6 官方支援 |
| IMU QMI8658 | `imu.c` | ✅ 純 Python | 只是 I2C 讀暫存器 |
| TCA9554 / AXP2101 | `board.c` | ✅ 純 Python | 純 I2C 寫暫存器 |
| 觸控 FT5x06 | 目前未用 | ✅ 純 Python | 直接讀 `0x38` 的 `0x02~0x06` |
| 音訊 ES8311 | `esp_codec_dev` | ⚠️ 要實測 | codec 暫存器可用 I2C 設；`machine.I2S` 官方標為 Technical Preview，且沒有 C6 的能力矩陣 |
| **顯示 SH8601 QSPI** | `esp_lcd_sh8601` | ❌ **卡點** | 無 C6 可用驅動，純 Python 做不到 QSPI |
| **LVGL 9** | `esp_lvgl_port` | ❌ 連帶卡住 | 綁定沒有 sh8601，QSPI 未正式支援 |
| 字型／圖／音效 | 產生的 `.c` | ⚠️ 要重做 | `lv_font_conv` 產出是 C 結構，四支產生腳本要跟著改 |

週邊層（I2C 那些）其實很適合 Python。問題是為了它們換掉整個框架不划算，而真正吃重的顯示層又剛好是做不到的那層。

---

## 三個附帶代價

**記憶體撐得很緊。** ESP32-C6 沒有外部 PSRAM 介面（`sdkconfig` 也確認無任何 `CONFIG_SPIRAM*`），可用的就是 512KB SRAM，沒有擴充空間。全畫面單緩衝 368×448×2 = 322KB，已佔六成。現行 C 韌體用 368×64 雙緩衝 ≈ 94KB 是取捨後的結果；疊上 MicroPython runtime 與 Python heap，LVGL 緩衝只會更小。

**效能會退步。** 擲筊動畫目前 21 fps（2140ms / 46 幀），CLAUDE.md 已經把「要更順」列為待處理。改 Python 後每幀的 sprite 搬移與座標計算走直譯器，只會更慢——而這支應用的價值幾乎全在那段動畫。

**既有資產作廢一半。** `docs/notes/` 裡關於 IDF 元件管理、flash size、SH8601 area rounding、codec 聲道的結論都是 ESP-IDF 情境；`src/fonts/*.c`、`src/sprites/*.c`、`src/sounds/*.c` 與四支產生腳本要重做。

---

## 三條路

**A. 混合式（建議）** — 韌體維持 C，只把工具鏈改純 Python：`flash.py` 取代手打 `pio run -t upload`、`monitor.py` 拆出 `monitor.sh` 的 heredoc、產生器拿掉 shell 包裝層。上一個任務已評估過，風險最低，而且該補的腳本本來就該補。

**B. 先做 spike 再決定** — 真的想走 MicroPython，第一里程碑不是移植應用，而是「編出含 SH8601 C module 的 C6 客製韌體並點亮螢幕」。這步過不了後面都不用談；過得了再談 LVGL 與動畫效能。要動板子，會抹掉現有擲筊韌體。

**C. 局部 Python 化** — 只把週邊層（IMU / PMIC / 觸控）當 MicroPython 練習品，顯示不碰。技術上成立，但和現有應用接不起來。

---

## 補充：專案裡沒有你要取代的 C++

`src/` 全是 `.c`（main / board / cast / cast_ui / imu / audio，另有未使用的 net / weather / ui）。唯一的 `.cpp` 在 `managed_components/lvgl__lvgl/` 的 ThorVG，是 LVGL 第三方相依，不歸本專案改，而且目前的擲筊應用沒用到向量繪圖那條路徑。

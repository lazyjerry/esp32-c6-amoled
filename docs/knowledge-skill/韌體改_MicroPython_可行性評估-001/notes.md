# 筆記：韌體改 MicroPython

---  2026-08-09 19:52  第 1 次更新筆記 ---
## 任務摘要
評估把本板韌體從 ESP-IDF C 換成 MicroPython 是否成立。範圍涵蓋顯示（SH8601 QSPI AMOLED 368×448）、LVGL 9、音訊（ES8311）、IMU（QMI8658）與電源（AXP2101 / TCA9554）。結論導向「哪一層真的能用 Python、哪一層不行、卡點在哪」。

## 來源

### 來源 1：MicroPython 官方（ESP32-C6 支援）
- https://micropython.org/download/ESP32_GENERIC_C6/
- https://github.com/micropython/micropython/blob/master/ports/esp32/README.md
- 重點：ESP32-C6 是官方正式支援的 port，最新穩定版 v1.28.0（2026-04-06），另有 v1.29.0 preview。**這一層沒有問題。**

### 來源 2：唯一現成的 MicroPython SH8601 驅動
- https://github.com/dobodu/Lilygo-Waveshare_Amoled-Micropython
- 重點：
  - 支援板子清單只有 **ESP32-S3**：LILYGO T4 S3 / T-Display S3 AMOLED、WAVESHARE ESP32-S3 1.8" / 2.41" AMOLED
  - **明確不支援 ESP32-C6**
  - 需要**自行編譯客製 MicroPython 韌體**（ESP-IDF v5.5.2 + MicroPython 1.28，`MODULE_AMOLED_ENABLED=1`）
  - QSPI 是用 ESP-IDF 的 `esp_lcd` API 加自製 `QSPIPanel` C class 實作，**編進韌體**，不是純 Python 模組
  - 專案自述「與 LVGL 是完全不同的專案」，**不相容 LVGL**

### 來源 3：LVGL 的 MicroPython 綁定
- https://github.com/lvgl-micropython/lvgl_micropython
- https://peter.hkprog.org/2025/04/tutorial-micropython-lvgl-for-esp32-c6-lcd-1-47/
- 重點：
  - 能為 ESP32-C6 建置：`python3 make.py esp32 clean --flash-size=4 --enable-jtag-repl=y BOARD=ESP32_GENERIC_C6 DISPLAY=st7789`
  - 內建約 30 種顯示驅動（axs15231b、gc9a01、ili93xx、st77xx、rm67162、spd2010…），**沒有 sh8601、沒有 co5300**
  - 匯流排支援列的是 SPI / I80 / RGB / SDL2，**QSPI 未列為正式支援**；QSPI 只出現在社群討論與 PR（AXS15231B 的 SPIBusFast）
  - C6 教學回報的 quirk：程式第一次跑成功，第二次要先按 reset

### 來源 4：SH8601 + LVGL 9 的實況
- https://forum.lvgl.io/t/buggy-homebrew-display-firmware-help-me-fix-the-co5300-sh8601-driver-lvgl9-setup/21242
- 重點：那串是 **ESP32-S3 直接寫 C（ESP-IDF）**，不是 MicroPython；即使在 C 環境，SH8601/CO5300 配 LVGL 9 的 buffer 設定仍讓人踩到顏色錯亂與 watchdog。與 MicroPython 無關，但說明這顆面板本身不是「隨插即用」等級。

### 來源 5：本機驗證（記憶體上限）
- `sdkconfig.esp32-c6-devkitc-1` 沒有任何 `CONFIG_SPIRAM*` 條目；`IDF_TARGET="esp32c6"`
- ESP32-C6 本身沒有外部 PSRAM 介面 → **可用 RAM 就是 512KB HP SRAM，沒有擴充空間**
- 現行 C 韌體的 LVGL 緩衝是 368×64 雙緩衝 ≈ 94KB（CLAUDE.md 記載）
- 全畫面單緩衝 368×448×2 = 329,728 B ≈ 322KB，已佔掉 512KB 的六成

### 來源 6：現行韌體的組成（本地盤點）
- `src/*.c`：main / board / cast / cast_ui / imu / audio（+ 未使用的 net / weather / ui）
- 相依 IDF 元件（`src/idf_component.yml`）：`esp_lcd_sh8601`、`esp_lvgl_port`、`lvgl`、`esp_codec_dev`、`cjson`、`network_provisioning`
- 產物型資源：`src/sprites/*.c`、`src/sounds/*.c`、`src/fonts/*.c`，全部由腳本產生並編進韌體

## 綜合發現

### 逐層判定

| 層 | 現況（C） | MicroPython 可行性 | 說明 |
|----|-----------|--------------------|------|
| 執行環境 | ESP-IDF | ✅ 可 | ESP32-C6 官方支援，v1.28.0 |
| IMU QMI8658 | `imu.c` | ✅ 可，純 Python | 只是 I2C 讀暫存器，`machine.I2C` 就夠 |
| TCA9554 / AXP2101 | `board.c` | ✅ 可，純 Python | 同上，純 I2C 寫暫存器 |
| 觸控 FT5x06 | 目前未用 | ✅ 可，純 Python | 直接讀 `0x38` 的 `0x02~0x06` |
| 音訊 ES8311 | `audio.c` 走 `esp_codec_dev` | ⚠️ 部分 | codec 暫存器可用 I2C 設；`machine.I2S` 官方標為 Technical Preview 且沒有 C6 的能力矩陣，要實測 |
| **顯示 SH8601 QSPI** | `esp_lcd_sh8601` | ❌ **卡點** | 無 C6 可用驅動；`machine.SPI` 沒有 quad 模式，純 Python 做不到 |
| **LVGL 9** | `esp_lvgl_port` | ❌ 連帶卡住 | `lvgl_micropython` 沒有 sh8601、QSPI 未正式支援 |
| 字型／圖／音效資源 | 產生的 `.c` | ⚠️ 要重做 | `lv_font_conv` 產出的是 C 結構，改 MicroPython 要重新產出對應格式 |

### 核心結論：目標與手段互相矛盾
要在 MicroPython 下點亮這片面板，**只有一條路**：把 `esp_lcd_sh8601` + QSPI panel 包成 MicroPython 的 C module，編進客製韌體（就是 dobodu 那個專案對 S3 做的事，但要移植到 C6）。

也就是說，「用 Python 取代 C」在最關鍵的顯示層**做不到**——反而要先多寫一份 C，才能讓 Python 有東西可呼叫。換來的是 Python 只能在上層寫應用邏輯。

### 附帶代價
1. **記憶體**：512KB SRAM 無 PSRAM。C 韌體用 94KB 緩衝已經是取捨後的結果；再加上 MicroPython runtime 與 Python heap，LVGL 緩衝只會更小。
2. **效能**：擲筊動畫現在是 21 fps（2140ms / 46 幀），且 CLAUDE.md 已列「還要更順」為待處理項。改 Python 後每幀的 sprite 搬移與座標計算走直譯器，只會更慢。
3. **資源檔**：`src/fonts/*.c`（`lv_font_conv` 產出）、`src/sprites/*.c`、`src/sounds/*.c` 都要重新產成 MicroPython 能吃的形式，四支產生腳本要跟著改。
4. **既有筆記作廢**：`docs/notes/` 有一半是 ESP-IDF / PlatformIO 情境的結論（元件管理、flash size、SH8601 area rounding、codec 聲道），換掉框架後不再適用。

### 可行的替代路徑
- **A. 混合式**：保留 C 韌體與顯示層，只把工具鏈（燒錄、產生器、監看）改純 Python。這是上一個任務已評估過、風險最低的路。
- **B. 先做 spike 再決定**：真的想走 MicroPython，第一里程碑不是移植應用，而是「編出含 SH8601 C module 的 C6 客製韌體並點亮螢幕」。這一步過不了，後面都不用談。
- **C. 局部 Python 化**：週邊層（IMU / PMIC / 觸控）本來就適合 Python，但為了它們換掉整個框架不划算。

---

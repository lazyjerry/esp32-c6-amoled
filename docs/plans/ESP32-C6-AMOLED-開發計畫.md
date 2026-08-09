# ESP32-C6-Touch-AMOLED-1.8 開發計畫（Mac + PlatformIO）

## 目標
在 Mac 上用 PlatformIO + VS Code,成功點亮並操作 Waveshare ESP32-C6-Touch-AMOLED-1.8 這片板子（AMOLED 螢幕、觸控、IMU、RTC、音訊、SD 卡等周邊）。

---

## 階段 0：硬體確認（開發前必做）

- [x] 確認板子版本是 **V1** 還是 **V2** → **實測為 V1**
  - V1：SH8601 顯示驅動、觸控 I2C 位址 `0x38`（官方 BSP 命名 FT5x06）
  - V2：CO5300 顯示驅動、觸控 I2C 位址 `0x15`（CST820 / FT3168）
  - 實際確認方式：燒 I2C 掃描程式,`0x38` 有應答、`0x15` 無應答 → V1
- [x] 找出你的板子在 Mac 上對應的序列埠裝置名稱（`/dev/cu.usbserial-XXXX` 或類似）
  - 實際為 **`/dev/cu.usbmodem1101`**（非 `usbserial`,因為走原生 USB）
- [x] 若序列埠找不到裝置 → 先安裝 USB 轉序列驅動（CP2102 或 CH340,依板子實際晶片而定）
  - 判定為**不需要**：ESP32-C6 內建 USB Serial/JTAG,USB-C 直連 SoC,macOS 以原生 CDC-ACM 辨識為 `/dev/cu.usbmodemXXXX`,不經 CP2102/CH340

**參考資料：**
- 官方 Wiki：https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8

**階段 0 結論（2026-08-06 實機判定完成）：**

| 項目 | 狀態 | 實測值 |
|------|------|--------|
| 板子版本 V1/V2 | ✅ **V1** | I2C 掃描：`0x38` 應答、`0x15` 無應答 → SH8601 + FT5x06 |
| 序列埠裝置名稱 | ✅ | `/dev/cu.usbmodem1101` |
| USB 轉序列驅動 | ✅ 不需安裝 | 已證實無 USB-UART 橋接晶片 |

I2C 匯流排（SDA=GPIO8 / SCL=GPIO7）掃到 6 個裝置：`0x18` ES8311、`0x20` TCA9554、`0x34` AXP2101、`0x38` 觸控 FT5x06、`0x51` PCF85063、`0x6B` QMI8658。

**踩到的坑：** 冷開機時掃不到觸控——它的 reset 掛在 TCA9554（`0x20`）bit5,預設沒被驅動。要先把 bit4/bit5 設輸出、拉低再拉高才掃得到。官方 BSP 的 `bsp_board_detect()` 同樣是先 reset 再 probe。詳見 [../notes/tca9554-holds-touch-reset.md](../notes/tca9554-holds-touch-reset.md)。

esptool 實測晶片資訊：

| 項目 | 值 |
|------|-----|
| USB VID:PID | `303A:1001`（Espressif USB JTAG/serial debug unit） |
| 晶片 | ESP32-C6 (QFN40) revision **v0.2** |
| USB 模式 | USB-Serial/JTAG |
| Base MAC | `98:A3:16:A7:9E:1C` |
| 晶振 | 40MHz |
| Flash | **16MB**（Manufacturer `20`, Device `4018`） |

**待處理：** `platformio.ini` 目前用 `board = esp32-c6-devkitc-1`,該 board 定義預設 4MB flash（編譯報告 app 分區僅 1MB）,與實際 16MB 不符。整合螢幕/LVGL 前需補上 flash size 設定,否則會浪費 15MB 空間。

---

## 階段 1：開發環境建置

- [x] 安裝 VS Code
- [x] 安裝 PlatformIO IDE 擴充功能
- [x] 確認 PlatformIO 可以正常偵測到 ESP32-C6 開發板（Devices 清單有出現序列埠）
- [x] 安裝相關依賴工具（若走 ESP-IDF 路線）：
  ```bash
  brew install cmake ninja dfu-util
  ```
  - 判定為**不需要執行**,理由見下方結論

**階段 1 結論（2026-08-06 檢查）：**

| 項目 | 狀態 | 實測值 |
|------|------|--------|
| VS Code | ✅ | 1.132.0 (arm64),`code` CLI 已在 `/usr/local/bin/code` |
| PlatformIO IDE 擴充 | ✅ | `platformio.platformio-ide` 已安裝 |
| PlatformIO Core | ✅ | 6.1.19,位於 `~/.platformio/penv/bin/pio` |
| 偵測開發板 | ✅ | `pio device list` 認到 `/dev/cu.usbmodem1101`,Hardware ID `USB VID:PID=303A:1001` |
| cmake / ninja / dfu-util | ✅ 免裝 | PlatformIO 已自帶 `tool-cmake`、`tool-ninja`;ESP32-C6 用 esptool 經 USB Serial/JTAG 燒錄,不走 DFU |

**已驗證：** 用現有工具鏈實際編譯本專案（`framework = espidf`）成功,證明環境完整、不缺 brew 套件。

```
RAM:   3.3% (10964 / 327680 bytes)
Flash: 14.8% (155504 / 1048576 bytes)
========================= [SUCCESS] Took 27.92 seconds =========================
```

**需要注意：** `pio` 沒有進 shell PATH,終端機直接打 `pio` 會 command not found（VS Code 擴充內部走絕對路徑,不受影響）。要在終端機用,把這行加進 `~/.zshrc`：

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

---

## 階段 2：取得官方/社群範例程式碼

- [x] Clone 官方範例 repo（優先參考,含 ESP-IDF / Arduino 兩種範例）：
  ```bash
  git clone https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8.git
  ```
- [x] Clone Waveshare 官方元件庫（顯示驅動等）：
  ```bash
  git clone https://github.com/waveshareteam/Waveshare-ESP32-components.git
  ```
- [ ] （選用）參考社群整理的第三方 ESP-IDF 專案集,內含詳細腳位對照與踩雷筆記：
  ```bash
  git clone https://github.com/chayuto/ESP32-C6-Touch-AMOLED-1.8.git
  ```
- [x] 閱讀官方 repo README,確認範例是否有區分 V1 / V2 版本的程式碼分支或資料夾

**階段 2 結論：**

兩個官方 repo 已 clone 並讀過,但**沒有放進專案**——它們是查腳位與初始化序列用的參考,不是相依。實際相依改走 ESP-IDF component manager（`src/idf_component.yml`）,由建置流程自動取得,不用把 4000 個檔案塞進版控。

版本區分方式：
- **範例程式**依資料夾分：V1 用 `examples/arduino/`,V2 用 `examples/arduino-v2/`,兩者的 `libraries/Mylibrary/pin_config.h` 腳位完全相同
- **ESP-IDF 路線**不分資料夾,由 BSP 的 `bsp_board_detect()` 執行期偵測觸控位址自動選驅動
- **最完整的腳位來源**是 BSP 標頭檔 `bsp/esp32_c6_touch_amoled_1_8/include/bsp/esp32_c6_touch_amoled_1_8.h`,比官方 Wiki 詳細（Wiki 根本沒列 GPIO）

---

## 階段 3：先跑通「最小可行測試」

目標：不追求完整功能,先確認燒錄流程與基本硬體都正常。

- [x] 建立一個空的 PlatformIO 專案,board 先選 `esp32-c6-devkitc-1`
- [x] 上傳一個簡單的 Blink / Serial print 測試程式,確認：
  - 燒錄流程正常（BOOT 鍵操作、序列埠正確）
  - Serial Monitor 能收到訊息
- [x] 確認開發板通電、螢幕背光有反應（即使還沒顯示畫面）— 已於階段 4 一併確認

**階段 3 結論：**

燒錄鏈路完全正常,而且**不需要按 BOOT 鍵**——ESP32-C6 的原生 USB Serial/JTAG 支援軟體觸發下載模式,`pio run -t upload` 直接燒。實測燒錄約 4 秒、驗證 hash 通過、自動 reset 重開。

Serial 輸出走 USB Serial/JTAG。已在 `sdkconfig.defaults` 設 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`——預設 console 是 UART0（GPIO16/17）,那兩隻腳在本板沒接出來,不改就看不到輸出。

順帶修掉的設定錯誤：

| 項目 | 修正前 | 修正後 |
|------|--------|--------|
| Flash 大小 | image header 8MB（實體 16MB,每次開機警告） | 16MB,無警告 |
| app 分區 | 1MB | 4MB |
| storage 分區 | 無 | 11MB（SPIFFS） |

作法見 [../notes/pio-flash-size-via-sdkconfig-defaults.md](../notes/pio-flash-size-via-sdkconfig-defaults.md)。

背光那項待你目視確認。

---

## 階段 4：整合螢幕顯示（AMOLED + QSPI）

- [x] 根據版本（V1 用 SH8601 / V2 用 CO5300）選擇對應的顯示驅動元件
- [x] 將官方元件庫中的顯示驅動整合進 PlatformIO 專案（`lib/` 或透過 `lib_deps`）
- [x] 跑通官方範例中的「顯示測試」（例如顯示色塊、文字或圖片）— 已目視確認,紅綠藍白黑依序正確
- [x] （進階）整合 LVGL,做出第一個簡單 UI 畫面 — 已於天氣看板應用完成（LVGL 9.3 + esp_lvgl_port，見 [CLAUDE.md](../../CLAUDE.md) 的「應用：天氣看板」）

**階段 4 進度：**

驅動走 ESP-IDF component manager 而非 `lib_deps`（ESP-IDF 路線不吃 `lib_deps`）。`src/idf_component.yml` 只宣告一個相依：

```yaml
dependencies:
  waveshare/esp_lcd_sh8601: "^2.0.0"
```

沒有整包拉官方 BSP,因為 BSP 會連帶拉進 LVGL、esp_lvgl_port、esp_codec_dev 等一串相依,現階段只要點亮螢幕用不到。初始化序列（9 條指令）從 BSP 原始碼抄進 [../../src/main.c](../../src/main.c)。

| 項目 | 值 |
|------|-----|
| 面板 | 368 × 448,RGB565（16bpp） |
| QSPI | SPI2_HOST,PCLK=0、CS=5、D0~D3=1,2,3,4 |
| Reset | 走 TCA9554 bit4,不是 GPIO（`reset_gpio_num = GPIO_NUM_NC`） |
| 送圖方式 | 每次 32 列（23KB DMA buffer）,不配置整張 frame buffer |

**已確認：** SH8601 初始化無錯誤,螢幕正常亮起,紅綠藍白黑依序顯示且顏色正確。RGB565 需手動 byte-swap（程式中的色值 `0x00F8` 才是紅色,不是 `0xF800`）。

新增相依後若編譯說找不到標頭檔,見 [../notes/pio-component-manager-needs-clean.md](../notes/pio-component-manager-needs-clean.md)。

---

## 階段 5：整合觸控功能

- [x] 確認觸控 IC（V1: FT6146 / V2: CST820 or FT3168）與對應 I2C 位址
- [x] 跑通觸控測試範例,確認觸控座標讀取正常
- [ ] 將觸控事件與 LVGL（若有使用）整合,實現互動 UI

**階段 5 結論：**

觸控可讀,但**不能用官方觸控元件**。

這顆在 `0x38` 應答、vendor id（`0xA3`）回 `0x64`,可是暫存器語意與 FT5x06 不同。`espressif/esp_lcd_touch_ft5x06` 的 init 會寫入 8 個 FT5x06 電源管理暫存器,寫完晶片就停止回應 I2C。改成直接讀 `0x02`~`0x06` 五個 byte,不需要任何觸控元件。

順帶澄清命名：官方 Wiki 寫 FT6146、官方 BSP 叫它 FT5X06,兩邊都只是憑位址對應,實際是另一顆。

實測結果：

| 項目 | 值 |
|------|-----|
| vendor id (`0xA3`) | `0x64` |
| 觸碰時 `TD_STATUS` | `0x01`,放開為 `0x00` |
| 座標 | 隨手指連續變化（x=128 y=86 → x=176 y=143） |
| 75 秒取樣 | 546 筆事件,無讀取錯誤 |
| 輪詢方式 | 20ms 輪詢,不需接 INT（GPIO15） |

詳見 [../notes/touch-not-real-ft5x06.md](../notes/touch-not-real-ft5x06.md)。

---

## 階段 6：整合其他周邊（依需求選做）

- [ ] **PMIC（AXP2101）**：電源管理、電池電量讀取
- [ ] **IMU（QMI8658）**：六軸感測資料讀取
- [ ] **RTC（PCF85063）**：時間讀寫測試
- [ ] **音訊 Codec（ES8311）+ 喇叭/麥克風**：錄音/播放測試
- [ ] **Micro SD**：檔案讀寫測試

---

## 階段 7：專案應用開發

依你的實際專案需求,在前面驗證過的基礎模組上,開始開發你的實際應用邏輯（例如：資訊看板、穿戴裝置、語音助理原型等）。

---

## 待確認事項（需要你回頭補充）

1. ~~板子版本是 V1 還是 V2？~~ → 已實測確認為 **V1**
2. ~~你偏好用 Arduino 框架還是 ESP-IDF 框架？~~ → `platformio.ini` 已設 `framework = espidf`,目前走 ESP-IDF 6.0.1（要改 Arduino 現在成本最低）
3. 你的最終專案目標是什麼？（會影響階段 6 要優先做哪些周邊整合）

---

## 參考連結彙整

| 資源 | 連結 |
|------|------|
| 官方 Wiki | https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8 |
| 官方範例 Repo | https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 |
| 官方元件庫 | https://github.com/waveshareteam/Waveshare-ESP32-components |
| 社群 ESP-IDF 專案集 | https://github.com/chayuto/ESP32-C6-Touch-AMOLED-1.8 |

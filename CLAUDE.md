# my-first-esp32

Waveshare ESP32-C6-Touch-AMOLED-1.8 開發專案（macOS + PlatformIO + ESP-IDF）。
開發計畫與進度：[docs/plans/ESP32-C6-AMOLED-開發計畫.md](docs/plans/ESP32-C6-AMOLED-開發計畫.md)

## 硬體實測事實（2026-08-06 實機讀取，非推測）

| 項目 | 值 |
|------|-----|
| **板子版本** | **V1** — 觸控 FT5x06 應答 `0x38`，`0x15` 無回應 |
| 顯示驅動 | **SH8601**（V1 對應；QSPI） |
| 觸控 IC | **FT5x06 @ `0x38`**（官方 BSP 命名為 FT5X06，非 Wiki 早期寫的 FT6146） |
| 序列埠 | `/dev/cu.usbmodem1101`（原生 USB Serial/JTAG，VID:PID `303A:1001`） |
| 晶片 | ESP32-C6 (QFN40) rev v0.2，Base MAC `98:A3:16:A7:9E:1C` |
| 實體 Flash | 16MB（build 產出的 image header 目前是 8MB，未修正） |

### I2C 匯流排（SDA=GPIO8、SCL=GPIO7）

實測掃描到 6 個裝置：

| 位址 | 裝置 |
|------|------|
| `0x18` | 音訊 Codec ES8311 |
| `0x20` | I/O 擴充 TCA9554 |
| `0x34` | PMIC AXP2101 |
| `0x38` | 觸控 FT5x06 ← **版本判定依據** |
| `0x51` | RTC PCF85063 |
| `0x6B` | IMU QMI8658 |

**冷開機時掃不到 `0x38`** — 觸控 reset 掛在 TCA9554 bit5，需先放開。見 [docs/notes/tca9554-holds-touch-reset.md](docs/notes/tca9554-holds-touch-reset.md)。

其他腳位（官方 BSP）：`TP_INT=15`、LCD QSPI `SCLK=0 / CS=5 / D0~D3=1,2,3,4`、面板 368×448、I2S `MCK=19 BCK=20 WS=22 DO=21 DI=23`、SD `CLK=11 CMD=10 DATA=18 CS=6`。

## 操作筆記制度

`docs/notes/` 存跨任務可重用的**結論**，不是工作日誌。索引由腳本產生，用來在動手前查已知的坑。

### 強制流程

1. **動手前先查**：`./scripts/note.sh find <關鍵字>`。命中就照筆記做，不要重新踩。
2. **操作完成後才建筆記**，且只在「下次遇到同情境會用到」時建。純過程紀錄不建。
3. 建立骨架 → 填正文 → `./scripts/note.sh sync`。
4. **`docs/notes/index.md` 由腳本重建，任何情況都不手動編輯。**
5. 更新既有筆記直接改該檔再 `sync`；結論被推翻時把 `狀態` 改成 `已取代`，不刪檔。

### 規格

筆記與索引都在檔案內標明規格版本，腳本比對不符即報錯，避免混入不同格式的檔案。

- 筆記規格 `note/v1`，索引規格 `note-index/v1`
- frontmatter 必要欄位：`規格` `標題` `分類` `觸發時機` `摘要` `狀態` `建立日期`
- `狀態` 只能是 `生效` 或 `已取代`
- 正文必須有 `## 結論`；建議再加 `## 為什麼`、`## 驗證`
- 超過 60 行會警告 —— 那通常表示寫成流水帳了
- 分類：`hardware` / `toolchain` / `build` / `flash` / `driver` / `lvgl` / `misc`

### 指令

```bash
./scripts/note.sh find <關鍵字>                                  # 查（動手前）
./scripts/note.sh new <slug> -c 分類 -t 標題 -w 觸發時機 -s 摘要   # 建骨架
./scripts/note.sh sync                                          # 檢查 + 重建索引
./scripts/note.sh check                                         # 只檢查
```

### 與全域知識庫的分界

- **只在這個專案／這片板子成立** → `docs/notes/`
- **跨專案可重用**（shell 陷阱、工具鏈通則等）→ 全域知識庫 `~/.knowledge-skill/knowledge/`，走 knowledge-record-skill，不放這裡

## 建置

`pio` 不在 shell PATH，用絕對路徑或先 `export PATH="$HOME/.platformio/penv/bin:$PATH"`。

```bash
~/.platformio/penv/bin/pio run                                   # 編譯
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem1101
~/.platformio/penv/bin/pio device monitor -p /dev/cu.usbmodem1101
```

cmake / ninja / dfu-util **不需要** brew 安裝，PlatformIO 自帶，理由見 [docs/notes/platformio-bundles-cmake-ninja.md](docs/notes/platformio-bundles-cmake-ninja.md)。

## 建置設定

| 設定 | 值 | 位置 |
|------|-----|------|
| Flash | 16MB | [sdkconfig.defaults](sdkconfig.defaults)（不是 `board_upload.flash_size`） |
| 分區 | app 4MB + storage 11MB | [partitions.csv](partitions.csv) |
| Console | USB Serial/JTAG | `sdkconfig.defaults`（UART0 在本板沒接出來） |
| 外部元件 | `esp_lcd_sh8601` / `esp_lvgl_port` / `lvgl` / `cjson` / `network_provisioning` | [src/idf_component.yml](src/idf_component.yml)，走 IDF component manager 而非 `lib_deps` |
| 元件相依 | 手動列在 `REQUIRES` | [src/CMakeLists.txt](src/CMakeLists.txt)——這個元件不叫 `main`，IDF 不會自動掛上全部元件 |

改了 `sdkconfig.defaults` 要刪掉 `sdkconfig.esp32-c6-devkitc-1` 重生；改了 `idf_component.yml` 要 `rm -rf .pio/build`。兩者都有筆記。

`lvgl` 釘在 `~9.3.0`：下限是 `esp_lvgl_port` 2.9 要用的 `LV_COLOR_FORMAT_RGB565_SWAPPED`，上限是沒驗過更新版和 `lv_font_conv` 產出的字型結構。

IDF 6.0 有兩個搬家要注意：cJSON 不再內建（改吃 `espressif/cjson`）、`wifi_provisioning` 改名 `espressif/network_provisioning`（API 前綴 `network_prov_*`）。另外 protocomm 預設只開 security v2，PoP 流程要的 v1 得自己在 `sdkconfig.defaults` 打開。

## 應用：天氣看板

[src/main.c](src/main.c) 是實際應用，硬體與周邊拆成 [board.c](src/board.c)／[net.c](src/net.c)／[weather.c](src/weather.c)／[ui.c](src/ui.c)。

| 功能 | 作法 |
|------|------|
| 天氣來源 | Open-Meteo，HTTPS + esp-tls 內建根憑證組，免 API key |
| 更新頻率 | 每小時；失敗時 5 分鐘後重試 |
| WiFi 設定 | SoftAP 配網，螢幕畫 QR 給 ESP SoftAP Prov App 掃，憑證存 NVS |
| **BOOT 鍵短按** | 切 light sleep（螢幕關／喚醒）。喚醒源只能是它，見筆記 |
| **BOOT 鍵長按 3 秒** | 清除 WiFi 憑證並重開，回到配網畫面 |
| **PWR 鍵短按** | 手動刷新。走 AXP2101 `INTSTS2` bit3 輪詢，不佔 GPIO |
| 文字 | LVGL 9 + 自產中文子集字型（`src/fonts/`），內建 Montserrat 只有 ASCII |

熱點名稱與 PoP 由 STA MAC 後幾碼推出來，每片板子固定：`WEATHER_xxxxxx` / 8 碼十六進位。

設定與字型都由腳本產生，不要手改：

```bash
./scripts/config.sh --name 台北 --lat 25.0330 --lon 121.5654   # → src/app_config.h（只有地點，無密碼）
./scripts/gen-font.sh --extra "額外中文字"                       # → src/fonts/*.c
./scripts/monitor.sh 20                                        # 非互動式讀序列埠（pio device monitor 需要 TTY）
```

要回到「未配網」狀態測配網流程，長按 BOOT，或直接抹掉 NVS：

```bash
~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32c6 -p /dev/cu.usbmodem1101 erase_region 0x9000 0x6000
```

## 已驗證可用

- **顯示**：SH8601 QSPI，368×448 RGB565。裸繪時色值需手動 byte-swap（紅是 `0x00F8`）；走 LVGL 則由 `flags.swap_bytes` 處理
- **觸控**：直接讀 `0x38` 的 `0x02`~`0x06`，20ms 輪詢，不用觸控元件也不用接 INT。天氣看板沒用到觸控，程式保留在 [docs/examples/display-touch-verify.c](docs/examples/display-touch-verify.c)
- **LVGL 9**：esp_lvgl_port，368×32 雙緩衝

## 待處理

1. 其他周邊：QMI8658 / PCF85063 / ES8311 / SD（AXP2101 目前只用到電源鍵 IRQ）
2. 電池電量顯示（AXP2101 已初始化，讀 gauge 即可）

## 官方參考來源

| 用途 | 位置 |
|------|------|
| 範例 repo | `waveshareteam/ESP32-C6-Touch-AMOLED-1.8`；V1 走 `examples/arduino/`，V2 走 `examples/arduino-v2/` |
| BSP 元件 | `waveshareteam/Waveshare-ESP32-components`，路徑 `bsp/esp32_c6_touch_amoled_1_8` |
| 腳位定義 | BSP 的 `include/bsp/esp32_c6_touch_amoled_1_8.h`（比 Wiki 完整） |
| 原廠韌體備份 | [backup/factory-16MB.bin](backup/factory-16MB.bin)（16MB 全片 dump，2026-08-06） |

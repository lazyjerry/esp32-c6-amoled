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

其他腳位（官方 BSP）：`TP_INT=15`、LCD QSPI `SCLK=0 / CS=5 / D0~D3=1,2,3,4`、面板 368×448、I2S `MCK=19 BCK=20 WS=22`、SD `CLK=11 CMD=10 DATA=18 CS=6`。

I2S 資料線與喇叭電源見 [docs/notes/bsp-i2s-dout-is-esp-side.md](docs/notes/bsp-i2s-dout-is-esp-side.md)：**ESP 端輸出是 `GPIO23`、輸入是 `GPIO21`**（BSP 的 `DOUT`/`DSIN` 以 ESP 為視角，不是 codec 視角），功放電源在 TCA9554 bit7。

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
- **研究過程與完整報告** → `docs/knowledge-skill/<任務名稱>-<流水號>/`，走 knowledge-skill。這裡收的是查證過程與結論全文，`docs/notes/` 只收壓縮過的結論

## Git 提交

**每次任務結束後執行 commit**，走 git-atomic-commit skill：依目的拆成原子提交，只收本次任務的變更，不夾帶既有或無關的修改。任務中途不提交，等結束再一次整理。

**研究任務結束後，`docs/knowledge-skill/` 的研究資料要一併 commit 並 push。** 這批資料進版控，理由是查證成本高、結論會被後續任務引用；留在本機等於下次重查。報告與程式碼變更拆成不同提交。

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
| 分區 | app 6MB + storage 9MB | [partitions.csv](partitions.csv)——2026-08-29 由 4M/11M 調整，壓力在 app 不在 storage |
| Console | USB Serial/JTAG | `sdkconfig.defaults`（UART0 在本板沒接出來） |
| 外部元件 | `esp_lcd_sh8601` / `esp_lvgl_port` / `lvgl` / `cjson` / `network_provisioning` / `esp_codec_dev` | [src/idf_component.yml](src/idf_component.yml)，走 IDF component manager 而非 `lib_deps` |
| 元件相依 | 手動列在 `REQUIRES` | [src/CMakeLists.txt](src/CMakeLists.txt)——這個元件不叫 `main`，IDF 不會自動掛上全部元件 |

改了 `sdkconfig.defaults` 要刪掉 `sdkconfig.esp32-c6-devkitc-1` 重生；改了 `idf_component.yml` 要 `rm -rf .pio/build`。兩者都有筆記。

`lvgl` 釘在 `~9.3.0`：下限是 `esp_lvgl_port` 2.9 要用的 `LV_COLOR_FORMAT_RGB565_SWAPPED`，上限是沒驗過更新版和 `lv_font_conv` 產出的字型結構。

IDF 6.0 有兩個搬家要注意：cJSON 不再內建（改吃 `espressif/cjson`）、`wifi_provisioning` 改名 `espressif/network_provisioning`（API 前綴 `network_prov_*`）。另外 protocomm 預設只開 security v2，PoP 流程要的 v1 得自己在 `sdkconfig.defaults` 打開。

## 應用：掌上宮廟

開機進**正殿**：點擊走完整儀式（三拜 → 稟告 → 求籤 → 擲筊確認 → 解籤閣），
左右滑動去兩個側翼（參拜簿／設定，目前是空殼），BOOT 短按直接進**擲筊**（跳過儀式，測試用）。
**BOOT 長按（900ms）在任何畫面都回正殿**，短按只做該畫面的主要動作
（三拜是算一拜、求籤是抽一支、擲筊是擲一次）。
畫面由 [src/screen_mgr.c](src/screen_mgr.c) 管，每個畫面實作 `enter / exit / tick / on_event`
（介面在 [src/screen.h](src/screen.h)）；輸入統一收在 `main.c` 的主迴圈轉成事件派發。

**畫面切換一律在主迴圈做**，LVGL 回呼只設旗標；畫面物件建一次就快取，不在 `exit()` 刪除。
理由見 [筆記](docs/notes/lvgl-screen-switch-from-main-loop.md)。

| 檔案 | 職責 |
|------|------|
| [display.c](src/display.c) | 起 LVGL 與面板，與任何單一畫面無關 |
| [content.c](src/content.c) | 掛 storage 分區的 SPIFFS，並確認 `poems.json` 讀得到 |
| [shrine_screen.c](src/shrine_screen.c) | 正殿。**神像與神龕是 LVGL 幾何佔位造型，不是美術**；香煙是 120×160 局部動畫 |
| [stub_screen.c](src/stub_screen.c) | 參拜簿與設定的空殼，共用一份實作 |
| [error_screen.c](src/error_screen.c) | 語料掛不起來時的死路畫面。刻意不退回擲筊 |
| [cast_screen.c](src/cast_screen.c) | 擲筊，包住既有的 `cast_ui.c`。求籤後進來是**確認模式**，依筊象決定去向 |
| [bow_screen.c](src/bow_screen.c) | 三拜。拜是姿勢變化不是甩動，看重力向量轉了幾度，基準在進畫面時取 |
| [tell_screen.c](src/tell_screen.c) | 稟告。6 類別 2×3，點一格記進 `ritual.c` 並進求籤 |
| [draw_screen.c](src/draw_screen.c) | 求籤。籤號在動畫開始時就抽好，動畫只是演出來 |
| [reading_screen.c](src/reading_screen.c) | 解籤閣。籤詩一句一行，白話解讀來自 `readings.json` |
| [ritual.c](src/ritual.c) | 一次參拜的過程狀態（稟告類別、籤號），畫面之間因此不必互相認識 |

### 擲筊

快速上下甩板子擲一次筊，畫面是**由上往下看的第一人稱**。兩片紅木筊被拋出、落地彈跳，鏡頭再拉近看筊象，同時播木頭撞擊聲。結果會一直停在畫面上，收掉之後才能再擲。

| 功能 | 作法 |
|------|------|
| 手勢 | QMI8658 加速度計，[imu.c](src/imu.c)。先低通估出重力方向，沿重力軸的動態分量是「上下甩」（擲筊），垂直方向的是「左右晃」（收結果）。板子怎麼拿都算得出來 |
| 筊象 | [cast.c](src/cast.c)。聖筊 50%、笑筊 25%、陰筊 25%，立筊十萬分之一（0.001%），走 `esp_random` |
| 動畫 | [cast_ui.c](src/cast_ui.c)。俯視沒有地平線，景深靠「高度→放大＋上移」＋「影子留在地面」。**整段翻滾的角度是從結果反推的**，停在哪個角度就是哪一面（`[0,180)` 平面、`[180,360)` 凸面），所以動畫和結果不可能接不起來 |
| 音效 | [audio.c](src/audio.c)。ES8311 走 `esp_codec_dev`，落地與兩次彈跳各一聲，音量遞減 |
| 收掉結果 | BOOT 鍵、左右晃動，或**停留 3 秒自動收掉**。確認模式下聖筊與陰筊直接換頁，不經過預備畫面；笑筊才回到預備狀態重擲。結果剛出現的 0.8 秒內只認按鍵——擲完手還在動，不然筊象會來不及看就被收掉 |
| 冷卻 | 收掉後 3 秒才能再擲，時間到才會出現「搖一搖　擲筊」提示 |
| **BOOT 鍵短按** | 功能鍵。平時直接擲一次（不用搖，測試方便）；結果停著時收掉結果 |
| **BOOT 鍵長按** | 900ms，任何畫面都回正殿。短按＝這個畫面的主要動作、長按＝離開，是跨畫面唯一的一組規則。動畫跑到一半不放行 |
| **PWR 鍵短按** | 電源鍵，任何狀態下都軟關機——寫 AXP2101 `COMMON_CONFIG`(`0x10`) bit0 切掉所有電軌，μA 等級，再按 PWR 開機。長按是 PMIC 硬體斷電，韌體攔不到。見筆記 |
| 文字 | LVGL 9 + 自產中文子集字型（`src/fonts/`），內建 Montserrat 只有 ASCII。結果畫面刻意沒有任何文字，只有放大的筊 |

圖、音效、字型都由腳本產生，**不要手改產物**：

```bash
./scripts/gen-sprites.sh [--color 8A4028]      # → src/sprites/*.c（紅木半月，平面版與凸面版）
./scripts/gen-sound.sh [--preview out.wav]     # → src/sounds/*（合成的木頭撞擊聲，可先在 Mac 上試聽）
./scripts/gen-font.sh --extra "額外中文字"       # → src/fonts/*.c
./scripts/gen-content.sh                       # data/*.json → spiffs_content/ + .pio/storage.bin
./scripts/flash-content.sh                     # 只燒 storage 分區，不動韌體
./scripts/monitor.sh 20 --reset                # 非互動式讀序列埠（pio device monitor 需要 TTY）
```

**語料在 [data/](data/)（進版控、人工編輯），產物在 `spiffs_content/`（不進版控、不手改）。**
`gen-content.sh` 有字元白名單守門，混入非預期字元會直接中止——AI 產的語料實際出現過
西里爾字母污染。籤詩 63 首已核對完成（`verified: true`），來源見
[docs/knowledge-skill/M0-S1_SD卡與SPI2分時共用驗證-001/notes.md](docs/knowledge-skill/M0-S1_SD卡與SPI2分時共用驗證-001/notes.md) 的附錄。

**PlatformIO 不會產生也不會燒 SPIFFS image**，`spiffs_create_partition_image()` 寫了沒用，
見 [docs/notes/pio-does-not-build-or-flash-spiffs.md](docs/notes/pio-does-not-build-or-flash-spiffs.md)。

**畫面上出現豆腐方塊 = 字型子集漏字。** 全形空白 `U+3000`、全形逗號 `U+FF0C` 這種標點也要收，改字串就要回頭改 `gen-font.sh` 的字集再重產。

天氣看板的 [net.c](src/net.c)／[weather.c](src/weather.c)／[ui.c](src/ui.c) 與 [config.sh](scripts/config.sh) 都保留著但已不進入，`main.c` 沒有引用它們。

## 已驗證可用

- **顯示**：SH8601 QSPI，368×448 RGB565。裸繪時色值需手動 byte-swap（紅是 `0x00F8`）；走 LVGL 則由 `flags.swap_bytes` 處理
- **觸控**：直接讀 `0x38` 的 `0x02`~`0x06`，20ms 輪詢，不用觸控元件也不用接 INT。[src/touch.c](src/touch.c) 把它接成 LVGL 指標裝置並自寫左右滑動判定；座標與面板 368×448 為 1:1，不需縮放或鏡射。**滑動判定成立時要 `lv_indev_reset()` 取消該次觸碰**，否則滑過按鈕等於按下去，見 [筆記](docs/notes/touch-swipe-must-cancel-lvgl-press.md)。擲筊本身沒用到觸控
- **LVGL 9**：esp_lvgl_port，368×64 雙緩衝（擲筊沒有網路，緩衝可以比天氣看板大一倍）
- **IMU**：QMI8658 加速度計，`±8g` 檔位 4096 LSB/g，靜置合力實測 1.00~1.07 g。見 [docs/notes/qmi8658-accel-minimal-bringup.md](docs/notes/qmi8658-accel-minimal-bringup.md)
- **RTC**：PCF85063 @ `0x51`，[src/rtc.c](src/rtc.c)。讀寫與走時實測正常；**軟關機保時未驗證**，產品刻意不依賴
- **SPIFFS 語料**：`storage` 分區 9MB，掛載 296ms、讀 7.6KB JSON 加解析 10ms。內容走 [scripts/gen-content.sh](scripts/gen-content.sh) → [scripts/flash-content.sh](scripts/flash-content.sh)
- **音訊**：ES8311 走 `espressif/esp_codec_dev`，16 kHz／16-bit。單聲道音源要自己攤成兩個 slot，見 [docs/notes/esp-codec-dev-needs-even-channels.md](docs/notes/esp-codec-dev-needs-even-channels.md)

## 待處理

1. 其他周邊：SD **板上無可用卡座，已確認不做**（見 [筆記](docs/notes/c6-lcd-sd-share-one-spi.md)）；PCF85063 驅動已寫（[src/rtc.c](src/rtc.c)）但**產品不依賴**——軟關機保時未驗證且決定不驗，參拜紀錄改用遞增序號
2. 電池電量顯示（AXP2101 已初始化，讀 gauge 即可）
3. 拋擲動畫實測 21 fps（2140ms / 46 幀）。**S4 已證實瓶頸在 LVGL 算圖不在傳輸**——全螢幕傳輸上限 31 fps，而動畫只畫兩片 132×140 sprite。所以要做的是 sprite 改 RGB565A8、影子改預算好的圖，**不是**加大繪圖緩衝（見 [筆記](docs/notes/sh8601-qspi-fullscreen-throughput.md)）
4. 搖動門檻（`imu.c` 的 `SWING_G` / `SWING_HALVES`）只在桌上驗過，手持手感要實際調

## 官方參考來源

| 用途 | 位置 |
|------|------|
| 範例 repo | `waveshareteam/ESP32-C6-Touch-AMOLED-1.8`；V1 走 `examples/arduino/`，V2 走 `examples/arduino-v2/` |
| BSP 元件 | `waveshareteam/Waveshare-ESP32-components`，路徑 `bsp/esp32_c6_touch_amoled_1_8` |
| 腳位定義 | BSP 的 `include/bsp/esp32_c6_touch_amoled_1_8.h`（比 Wiki 完整） |
| 原廠韌體備份 | [backup/factory-16MB.bin](backup/factory-16MB.bin)（16MB 全片 dump，2026-08-06） |

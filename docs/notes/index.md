# 操作筆記索引

<!-- 規格：note-index/v1 -->

| 規格 | 值 |
|------|------|
| 筆記規格 | `note/v1` |
| 索引規格 | `note-index/v1` |

> 本檔由 `scripts/note.sh index` 自動重建，**請勿手動編輯**。
> 動手前先查：`scripts/note.sh find <關鍵字>`。

## build

| 筆記 | 觸發時機 | 摘要 | 狀態 |
|------|----------|------|------|
| [board 定義的 flash 大小未必等於實際板子，需以 esptool flash_id 為準](board-def-flash-size-mismatch.md) | 用通用 devkit board 定義（如 esp32-c6-devkitc-1）開發模組板，或編譯報告的 Flash 容量看起來偏小時 | PlatformIO board 定義寫的是參考板規格，模組板實際 flash 常更大；以 esptool flash_id 讀到的值為準，在 platformio.ini 補 board_upload.flash_size 與對應 partition 表，否則多出的容量完全用不到。 | 生效 |
| [PlatformIO 加了 idf_component.yml 後要清掉 .pio/build，否則元件不會下載](pio-component-manager-needs-clean.md) | 在 PlatformIO 的 ESP-IDF 專案新增或修改 src/idf_component.yml，編譯卻回報找不到該元件的標頭檔時 | PlatformIO 只在 CMake 重新設定時才跑 ESP-IDF component manager；既有 .pio/build 存在時不會重跑，新增的相依會被無聲忽略。pio run -t clean 不夠，要 rm -rf .pio/build 才會觸發，成功後專案根目錄會出現 managed_components/。 | 生效 |
| [PlatformIO 的 ESP-IDF 專案不會產生也不會燒 SPIFFS image，要自己來](pio-does-not-build-or-flash-spiffs.md) | 在 PlatformIO + ESP-IDF 專案要用 SPIFFS 分區放資料，或發現 spiffs_create_partition_image() 沒作用時 | spiffs_create_partition_image() 是 IDF 的 custom target，PlatformIO 的建置不執行它、upload 也只燒 bootloader/partitions/firmware，image 不會產生更不會進 flash。要自己用 IDF 內附的 spiffsgen.py 打包、再用 esptool 燒到分區 offset，且打包參數必須與 sdkconfig 一致。 | 生效 |
| [PlatformIO 的 board_upload.flash_size 不會進 sdkconfig，要用 sdkconfig.defaults](pio-flash-size-via-sdkconfig-defaults.md) | 在 PlatformIO ESP-IDF 專案要改 flash 大小、console 走 USB 等 sdkconfig 選項，或改完出現 esptool「SHA256 digest offset 不是全零」錯誤時 | board_upload.flash_size 只影響上傳，不會寫進 CONFIG_ESPTOOLPY_FLASHSIZE，開機仍會警告 image header 與實體 flash 不符。要改 sdkconfig 選項就寫 sdkconfig.defaults，刪掉 sdkconfig.<env> 讓它重生；重生後第一次編譯若報 SHA256 digest 錯誤，跑一次 pio run -t clean 即可。 | 生效 |

## driver

| 筆記 | 觸發時機 | 摘要 | 狀態 |
|------|----------|------|------|
| [esp_codec_dev 的 I2S 資料層只收偶數聲道，單聲道音源要自己攤成兩個 slot](esp-codec-dev-needs-even-channels.md) | 用 espressif/esp_codec_dev 播單聲道 PCM，esp_codec_dev_open 直接回失敗時 | sample_info 的 channel 必須是偶數，填 1 會被擋掉。另外 audio_codec_i2c_cfg_t 的 addr 是 8 位元寫入位址（ES8311 填 0x30，匯流排上看到的是 0x18），且在 IDF 5.3 以上要填 bus_handle 才會走新的 i2c_master 驅動。 | 生效 |
| [QMI8658 只用加速度計的最小啟動：CTRL1 沒開位址自動遞增就讀不到連續六個位元組](qmi8658-accel-minimal-bringup.md) | 要在本板讀 QMI8658 三軸加速度，或連讀 0x35 起六個 byte 拿到重複／錯位的值時 | 三行暫存器就能跑：CTRL1=0x40 開 ADDR_AI、CTRL2=0x26 設 ±8g/125Hz、CTRL7=0x01 開加速度計。±8g 是 4096 LSB/g，小端。驗收看靜置合力是不是 1g，不對就是量程或位元組序設錯。 | 生效 |
| [SH8601 全螢幕傳輸實測：分段越大越快，64 列是 RAM 與速度的平衡點](sh8601-qspi-fullscreen-throughput.md) | 要估算 SH8601／QSPI AMOLED 的畫面更新速度，或在調 LVGL 繪圖緩衝大小、找動畫掉幀原因時 | 368x448 RGB565（329,728 bytes）裸繪實測：16 列一段 44ms、32 列 41ms、64 列 32ms、112 列 28ms——每段都有固定的指令與位址開銷，分段越大越省。64 列雙緩衝 94KB 換到 31fps，再往上收益遞減。全螢幕理論上限 31fps 而擲筊動畫只有 21fps，代表瓶頸在 LVGL 算圖不在傳輸。 | 生效 |
| [本板觸控只是位址相同，不是真 FT5x06；套官方元件會把晶片寫到不回應 I2C](touch-not-real-ft5x06.md) | 要整合本板觸控、或看到 esp_lcd_touch_ft5x06 初始化後出現 I2C hardware timeout detected 時 | 觸控在 0x38 應答、vendor id(0xA3)=0x64，但暫存器語意與 FT5x06 不同。espressif/esp_lcd_touch_ft5x06 的 init 會寫入 8 個 FT5x06 電源管理暫存器（含 2 秒進 Monitor 模式），寫完晶片就停止回應 I2C。改成直接讀 0x02~0x06 五個 byte 即可，不需任何觸控元件。 | 生效 |
| [自寫觸控 swipe 要用 lv_indev_reset 取消該次觸碰，否則滑過按鈕就等於按下去](touch-swipe-must-cancel-lvgl-press.md) | 要在本板做觸控滑動換頁、或滑動時發現底下的按鈕被誤觸時 | 本板觸控只能裸讀單點座標，滑動要自己判。判定成立的當下必須呼叫 lv_indev_reset(indev, NULL)，否則手指抬起時 LVGL 仍會補一次 CLICKED。座標與面板 368x448 為 1:1，不需縮放也不需鏡射。 | 生效 |

## flash

| 筆記 | 觸發時機 | 摘要 | 狀態 |
|------|----------|------|------|
| [C6 韌體當機後 esptool 連不上：default_reset 沒用，usb_reset 也救不完全](c6-usb-recover-from-hung-firmware.md) | ESP32-C6 燒了會當機的韌體之後 esptool 回 No serial data received，或想從當機狀態救回板子時 | C6 走原生 USB 沒有實體 DTR/RTS，default_reset 觸發不了重置。--before usb_reset 進得了下載模式但 USB 會重新列舉、連線隨即失效（serial TX path seems to be down），軟體繞不過這個競態。可靠做法是拔 USB、按住 BOOT 再上電，讓板子從一開始就以 bootloader 列舉。 | 生效 |

## hardware

| 筆記 | 觸發時機 | 摘要 | 狀態 |
|------|----------|------|------|
| [BSP 的 I2S DOUT/DSIN 以 ESP 為視角，喇叭功放在 TCA9554 bit7 不是 GPIO](bsp-i2s-dout-is-esp-side.md) | 要在 ESP32-C6-Touch-AMOLED-1.8 上出聲，或 I2S 設定看起來都對卻完全沒聲音時 | ESP 端資料輸出是 GPIO23、輸入是 GPIO21，反接就是全靜音。功放電源掛在 TCA9554 bit7，es8311_codec_cfg_t 的 pa_pin 要填 -1 並自己去操作擴充晶片。 | 生效 |
| [本板無可用的 SD 卡座；面板佔著唯一的 SPI2，但可安全拆裝](c6-lcd-sd-share-one-spi.md) | 想在 ESP32-C6-Touch-AMOLED-1.8 上用 SD 卡，或要加任何需要 SPI 的周邊時 | 板載 microSD 實測掛不起來，BSP 標的腳位加上所有未佔用腳共 17160 組排列全滅，原廠韌體也沒有掛 SD 的程式碼。另一方面 C6 可用的通用 SPI 只有 SPI2_HOST 一個、被面板佔著，但實測可以安全地拆掉再裝回來，要外接 SPI 周邊時用得上。 | 生效 |
| [本板兩顆按鍵：BOOT 在 GPIO9 喚不醒 deep sleep，PWR 直通 AXP2101](esp32c6-boot-key-cannot-wake-deep-sleep.md) | 要在 ESP32-C6-Touch-AMOLED-1.8 上做按鍵休眠／喚醒，或規劃低功耗模式時 | 板上只有 BOOT 與 PWR 兩顆鍵（無 RESET）。C6 的 LP IO 只有 GPIO0~7 且被 LCD QSPI／SD／I2C 佔滿，BOOT 在 GPIO9 只能喚醒 light sleep；PWR 直通 AXP2101 PWRKEY，長按硬體斷電攔不到，短按可由 I2C 讀 INTSTS2 bit3 得知，並可寫 COMMON_CONFIG bit0 軟關機，比 light sleep 省得多。 | 生效 |
| [ESP32-C6 走原生 USB，序列埠是 usbmodem 而非 usbserial，且不需裝橋接晶片驅動](esp32c6-usb-serial-jtag-port.md) | macOS 上找不到 ESP32-C6 板子的序列埠，或準備安裝 CP2102/CH340 驅動前 | ESP32-C6 內建 USB Serial/JTAG，USB-C 直連 SoC，macOS 以原生 CDC-ACM 列舉為 /dev/cu.usbmodemXXXX；找不到裝置是沒接好或線材只供電，不是缺驅動。 | 生效 |
| [觸控 IC 掃不到不代表沒有：reset 掛在 TCA9554，冷開機時被拉住](tca9554-holds-touch-reset.md) | 在 ESP32-C6-Touch-AMOLED-1.8 上 I2C 掃描找不到觸控位址（0x38／0x15），或要判定板子 V1/V2 版本時 | 本板觸控與 LCD 的 reset 接在 TCA9554（I2C 0x20）的 bit5／bit4，冷開機時擴充晶片全腳為輸入、觸控被拉在 reset，掃描掃不到；要先把該兩位元設為輸出並拉低再拉高，觸控才會出現在匯流排上。TCA9554 是獨立晶片，狀態不隨 CPU reset 清除，因此後續重開機看得到觸控，必須整片斷電才會回到冷開機狀態。 | 生效 |

## lvgl

| 筆記 | 觸發時機 | 摘要 | 狀態 |
|------|----------|------|------|
| [LVGL 回呼裡不要切畫面，也不要在 exit() 刪掉正在顯示的畫面](lvgl-screen-switch-from-main-loop.md) | 要用 screen_mgr 做多畫面切換、或畫面切換後當機／畫面全黑時 | 畫面切換一律在主迴圈做：LVGL 事件回呼只設旗標，由該畫面的 tick() 執行 screen_mgr_goto()。畫面物件建一次就快取，不要在 exit() 刪除——exit() 早於下一個畫面的 enter()，刪掉的正是當下顯示中的那一個。 | 生效 |
| [SH8601 接 LVGL 9：偶奇對齊要掛 INVALIDATE_AREA 事件，v8 的 rounder_cb 已移除](sh8601-lvgl9-area-rounding.md) | 在 SH8601／QSPI AMOLED 上整合 LVGL 9，畫面出現斜切、撕裂或位移時 | SH8601 要求繪圖區起點偶數、終點奇數，LVGL 預設不會對齊。LVGL 9 拿掉了 v8 的 rounder_cb，改用 lv_display_add_event_cb 監聽 LV_EVENT_INVALIDATE_AREA 修改 lv_area_t。另需 esp_lvgl_port 的 flags.swap_bytes 才有正確顏色。 | 生效 |

## toolchain

| 筆記 | 觸發時機 | 摘要 | 狀態 |
|------|----------|------|------|
| [PlatformIO 自帶 cmake/ninja，ESP-IDF 專案不需另外 brew install](platformio-bundles-cmake-ninja.md) | 看到教學要求 brew install cmake ninja dfu-util，準備裝系統套件前 | PlatformIO Core 的 packages 目錄已含 tool-cmake 與 tool-ninja，pio run 走自帶版本；ESP32-C6 以 esptool 經 USB Serial/JTAG 燒錄不走 DFU，dfu-util 亦免。先跑一次 pio run 驗證，成功就別裝。 | 生效 |

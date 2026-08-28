# 筆記：以 Python 改寫燒錄流程

---  2026-08-09 19:37:11  第 1 次更新筆記 ---
## 任務摘要
釐清本專案目前「燒錄」實際由什麼程式執行，判斷是否能改用 Python 腳本取代 `pio run -t upload`。範圍限於本機 macOS + PlatformIO + ESP-IDF、ESP32-C6 走原生 USB Serial/JTAG 的情境。結論導向「可行與否 + 若做要怎麼做」，本次不落地實作。

## 來源

### 來源 1：專案設定檔
- `platformio.ini`：`upload_port = /dev/cu.usbmodem1101`、`board_upload.flash_size = 16MB`、`board_build.partitions = partitions.csv`、`monitor_speed = 115200`
- `partitions.csv`：`factory` app 分區 offset `0x10000`，另有 `storage` spiffs 11M；**沒有 ota_data 分區**
- `sdkconfig.esp32-c6-devkitc-1`：`CONFIG_BOOTLOADER_OFFSET_IN_FLASH=0x0`、`FLASHMODE="dio"`、`FLASHFREQ="80m"`、`FLASHSIZE="16MB"`

### 來源 2：PlatformIO 平台 builder（燒錄指令真身）
- `~/.platformio/platforms/espressif32/builder/main.py` 的 `upload_protocol == "esptool"` 區塊：
  ```
  UPLOADER  = <tool-esptoolpy>/esptool.py
  UPLOADCMD = "$PYTHONEXE" "$UPLOADER" $UPLOADERFLAGS $ESP32_APP_OFFSET $SOURCE
  UPLOADERFLAGS = --chip esp32c6 --port <port> --baud <speed>
                  --before default_reset --after hard_reset
                  write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB
  ```
  再把 `FLASH_EXTRA_IMAGES` 逐對接在後面。
- `builder/frameworks/espidf.py`：`FLASH_EXTRA_IMAGES` 於本專案 = bootloader@`0x0` + partition table@`0x8000`（無 OTA 分區所以不加 ota_data_initial）；`ESP32_APP_OFFSET` 由 `partitions.csv` 推導 = `0x10000`。
- **關鍵事實：PlatformIO 的 upload 本身就是在跑一支 Python 程式（`esptool.py`），沒有任何 C/原生 uploader 參與。**

### 來源 3：esptool 套件實況（實機驗證）
- 位置 `~/.platformio/packages/tool-esptoolpy`，package.json 版本 `2.41100.0`，實際 `esptool.__version__ == 4.11.0`
- **不在 penv 的 site-packages 裡**：`~/.platformio/penv/bin/python -c "import esptool"` → `ModuleNotFoundError`
- 相依（cryptography / ecdsa / bitstring / reedsolo / intelhex）由 `package-postinstall.py` 裝進該套件的 `_contrib/` 子目錄，不是裝進 penv
- 因此以函式庫方式使用，`sys.path` 要**同時**加入 `<pkg>` 與 `<pkg>/_contrib`，缺 `_contrib` 會在 `import esptool` 就炸 `No module named 'intelhex'`
- 實測成功（板子接著跑真連線）：
  ```python
  sys.path.insert(0, ET + "/_contrib"); sys.path.insert(0, ET)
  import esptool; esptool.main(["--chip","esp32c6","--port","/dev/cu.usbmodem1101","flash_id"])
  ```
  輸出：Chip is ESP32-C6 rev v0.2 / USB mode: USB-Serial/JTAG / BASE MAC 98:a3:16:a7:9e:1c / Detected flash size: 16MB → 與 CLAUDE.md 記載的硬體事實一致

### 來源 4：`.pio/build/esp32-c6-devkitc-1/flasher_args.json`
- 內容給的是 **ESP-IDF 視角**的參數：`--flash-mode` / `--flash-size` / `--flash-freq`（連字號）、`before: default-reset` / `after: hard-reset`（連字號）
- 檔案路徑也是 IDF 視角：`bootloader/bootloader.bin`、`partition_table/partition-table.bin`、`my-first-esp32.bin`
- **但 PlatformIO 的實際產物在 build 根目錄**：`bootloader.bin`、`partitions.bin`、`firmware.bin`；`partition_table/` 目錄根本不存在（glob 無命中）
- **且 bundled esptool 是 4.11**，`write_flash --help` 只認 `--flash_mode` / `--flash_freq` / `--flash_size`（底線），連字號寫法是 esptool 5.x 才有
- ⇒ 直接把 `flasher_args.json` 的 `write_flash_args` 餵給 bundled esptool 會失敗，路徑也對不上。這是本題最容易踩的坑。

### 來源 5：既有腳本慣例
- `scripts/gen-sound.sh` → `python3 scripts/gen_sound.py`：shell 只做參數解析與路徑組裝，重活在 Python
- `scripts/gen-sprites.sh` → `gen_sprites.py`：同上
- `scripts/monitor.sh`：shell 內用 heredoc 直接餵 Python 給 `$HOME/.platformio/penv/bin/python`，因為要用 penv 內的 `pyserial`
- `scripts/monitor.sh` 已記錄 USB Serial/JTAG 的 reset 序列（DTR/RTS 組合，順序同 esptool 的 usb_jtag_serial reset）
- 專案目前**沒有** flash/upload 腳本；CLAUDE.md 的燒錄指令是要人手打 `pio run -t upload --upload-port ...`
- 全域規則「固定流程一律腳本化」與此有落差 → 燒錄正是該補腳本的位置

### 來源 6：既有筆記（`./scripts/note.sh find`）
- `platformio-bundles-cmake-ninja.md`：ESP32-C6 以 esptool 經 USB Serial/JTAG 燒錄，不走 DFU
- `pio-flash-size-via-sdkconfig-defaults.md`：flash size 真正生效處在 `sdkconfig.defaults`，`board_upload.flash_size` 只影響上傳
- `board-def-flash-size-mismatch.md`：以 `esptool flash_id` 讀到的值為準
- `esp32c6-usb-serial-jtag-port.md`：埠是 `/dev/cu.usbmodem*`

## 綜合發現

### 可行性
- **可行，而且不是「改寫」而是「拿掉中間層」**：燒錄本來就是 Python（esptool.py）。`pio run -t upload` 只是 SCons 幫忙算好參數再呼叫它。
- 用 Python 直接驅動 esptool 完全成立，兩種呼叫法都驗過方向：`subprocess` 叫 `esptool.py`，或 `import esptool; esptool.main(argv)`（已實機驗證後者）。

### 邊界：Python 取代得了燒錄，取代不了編譯
- 編譯是 SCons + CMake + ninja + IDF component manager，Python 腳本自己重寫沒有意義。
- 合理分工：`pio run` 負責 build，Python 腳本負責 flash。腳本要嘛先 `subprocess` 呼叫 `pio run`，要嘛檢查產物時間戳避免燒到舊 binary。

### 改寫真正能換到的好處
1. 不必為了換埠／只燒 app／只燒 bootloader 去記 PlatformIO 的 target 名稱
2. 可以在燒錄前後接自己的動作（燒完直接接 `monitor.sh` 的讀取邏輯、燒錄前 `flash_id` 核對 16MB）
3. 非互動友善——與 `monitor.sh` 面對的是同一個問題（`pio device monitor` 需要 TTY）
4. 埠自動偵測（`/dev/cu.usbmodem*` 只有一個就直接用），少打一段參數

### 改寫的成本／風險
1. **參數要自己維持正確**：offset（0x0 / 0x8000 / 0x10000）、flash_mode/freq/size 若與 sdkconfig 不同步，燒進去會開不了機。解法是從 `sdkconfig.<env>` 與 `partitions.csv` 讀，不要寫死。
2. **esptool 版本語法漂移**：bundled 4.11 用底線參數，IDF 產的 `flasher_args.json` 是 5.x 連字號語法。跨版本抄參數會炸。
3. **`_contrib` 相依路徑**：以函式庫方式 import 必須手動補 `sys.path`，PlatformIO 升級套件後路徑版本可能變（目前 `tool-esptoolpy` 無版本後綴，風險低）。
4. **繞過 PlatformIO 後，SCons 那邊的隱含步驟也一併沒了**（如 OTA data 空映像產生）。本專案 partitions.csv 無 ota_data，暫時無影響，但加了 OTA 分區就要自己補。

### 建議形狀（符合既有慣例）
- `scripts/flash.sh`：入口，解析 `[--port] [--no-build] [--erase] [--app-only] [--monitor N]`，其餘轉給 Python
- `scripts/flash.py`：讀 `sdkconfig.esp32-c6-devkitc-1` 與 `partitions.csv` 組 argv、`sys.path` 補 `_contrib`、`import esptool` 呼叫
- 走 `gen-sound.sh` 那種「shell 入口 + 獨立 .py」而非 `monitor.sh` 的 heredoc，因為燒錄邏輯量比 monitor 大，heredoc 會難維護
- Python 直譯器用 `$HOME/.platformio/penv/bin/python`（與 `monitor.sh` 一致；系統 `python3` 沒有 pyserial）

---

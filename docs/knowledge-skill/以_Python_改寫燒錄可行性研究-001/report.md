# 以 Python 改寫燒錄流程 — 可行性報告

日期：2026-08-09　｜　板子：Waveshare ESP32-C6-Touch-AMOLED-1.8（V1）　｜　驗證方式：實機唯讀連線

---

## 結論

**可行，而且嚴格說不算「改寫」，是把 PlatformIO 這層中間人拿掉。**

本專案的燒錄從一開始就是 Python 在跑：`pio run -t upload` 最終執行的是
`~/.platformio/packages/tool-esptoolpy/esptool.py`，PlatformIO 只負責把 offset、flash 參數、產物路徑算好再呼叫它。

自己寫一支 Python 腳本直接驅動 esptool，等於接手 PlatformIO 那段「組參數」的工作。已在真板驗證這條路走得通。

一個邊界：**Python 取代得了燒錄，取代不了編譯。** 編譯是 SCons + CMake + ninja + IDF component manager，重寫沒有效益。合理分工是 `pio run` 負責 build、Python 腳本負責 flash。

---

## 目前的燒錄鏈路

`~/.platformio/platforms/espressif32/builder/main.py` 的 `upload_protocol == "esptool"` 區塊組出的指令，實質等同：

```bash
~/.platformio/penv/bin/python \
  ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32c6 --port /dev/cu.usbmodem1101 --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x10000 .pio/build/esp32-c6-devkitc-1/firmware.bin \
  0x0     .pio/build/esp32-c6-devkitc-1/bootloader.bin \
  0x8000  .pio/build/esp32-c6-devkitc-1/partitions.bin
```

參數各自的來源：

| 參數 | 值 | 來源 |
|------|-----|------|
| app offset | `0x10000` | `partitions.csv` 的 `factory` 分區，由 `espidf.py` 的 `get_app_partition_offset()` 推導 |
| bootloader offset | `0x0` | `CONFIG_BOOTLOADER_OFFSET_IN_FLASH`（C6 是 0x0，不是 ESP32 的 0x1000） |
| partition table offset | `0x8000` | IDF 預設 |
| flash_mode / freq / size | `dio` / `80m` / `16MB` | `sdkconfig.esp32-c6-devkitc-1` |
| before / after reset | `default_reset` / `hard_reset` | board 定義；C6 走原生 USB 時 esptool 內部會轉成 usb_jtag_serial 序列 |

本專案 `partitions.csv` 沒有 ota_data 分區，所以 PlatformIO 不會額外產生 `ota_data_initial.bin`，燒錄清單就是上面三塊。

---

## 實機驗證

用 penv 的 Python 直接把 esptool 當函式庫呼叫，對板子跑唯讀的 `flash_id`：

```python
import sys
ET = "/Users/lazyjerry/.platformio/packages/tool-esptoolpy"
sys.path.insert(0, ET + "/_contrib")   # 相依裝在這，不在 penv
sys.path.insert(0, ET)
import esptool
esptool.main(["--chip", "esp32c6", "--port", "/dev/cu.usbmodem1101", "flash_id"])
```

輸出：

```
esptool.py v4.11.0
Chip is ESP32-C6 (QFN40) (revision v0.2)
USB mode: USB-Serial/JTAG
BASE MAC: 98:a3:16:a7:9e:1c
Detected flash size: 16MB
```

與 CLAUDE.md 記錄的硬體事實一致。**函式庫呼叫路徑成立。**

---

## 四個必須注意的坑

### 1. esptool 不在 penv 裡，相依也不在

```
~/.platformio/penv/bin/python -c "import esptool"   → ModuleNotFoundError
```

esptool 是 PlatformIO 的 **package**（`~/.platformio/packages/tool-esptoolpy`），其相依（cryptography、ecdsa、bitstring、reedsolo、intelhex）由該套件的 `package-postinstall.py` 裝進自己的 `_contrib/` 子目錄，不進 penv。

以函式庫方式使用，`sys.path` 必須**同時**加入套件目錄與 `_contrib`；只加套件目錄會在 `import esptool` 當下就炸 `No module named 'intelhex'`。

用 `subprocess` 呼叫 `esptool.py` 則沒這問題（PlatformIO 的 postinstall 已處理），代價是要多開一個行程、拿不到結構化錯誤。

### 2. `flasher_args.json` 不能直接拿來用 ⚠️ 最容易踩

`.pio/build/esp32-c6-devkitc-1/flasher_args.json` 看起來就是為此而生，但它是 **ESP-IDF 視角**的產物，兩處對不上：

| 項目 | flasher_args.json（IDF 6.0 / esptool 5.x 語法） | 實際可用 |
|------|--------------------------------|----------|
| 參數命名 | `--flash-mode` `--flash-size` `--flash-freq`、`before: default-reset` | bundled esptool **4.11 只認底線**：`--flash_mode` `--flash_freq` `--flash_size` `default_reset` |
| 產物路徑 | `bootloader/bootloader.bin`、`partition_table/partition-table.bin`、`my-first-esp32.bin` | PlatformIO 放在 build 根目錄：`bootloader.bin`、`partitions.bin`、`firmware.bin`；`partition_table/` 目錄不存在 |

照抄會同時吃到語法錯誤與檔案不存在。

### 3. 參數寫死會變成開不了機

flash_mode / freq / size 與 sdkconfig 不同步時，燒進去的 image header 與實體對不上就開不了機——這與 `docs/notes/pio-flash-size-via-sdkconfig-defaults.md` 記錄的是同一類問題。腳本應該從 `sdkconfig.esp32-c6-devkitc-1` 與 `partitions.csv` 讀，不要硬編。

### 4. 繞過 PlatformIO 也繞過了它的隱含步驟

SCons 在 upload 前會做的事（產生 OTA data 空映像、build 相依檢查）一併沒了。本專案目前沒有 ota_data 分區所以無影響，但**日後改 `partitions.csv` 加 OTA 分區時要記得自己補**。同理，跳過 build 直接燒，可能燒到舊的 `firmware.bin`。

---

## 改寫能換到什麼

值得做的理由不是「Python 比較好」，而是這四項：

1. **非互動友善** — 與 `monitor.sh` 面對的是同一個問題（`pio device monitor` 需要 TTY），燒錄同樣可以在腳本／CI 裡跑
2. **燒完直接接讀取** — 現在要手動接 `monitor.sh`，一支腳本可以燒完就開始讀開機日誌
3. **埠自動偵測** — `/dev/cu.usbmodem*` 通常只有一個，不必每次打 `--upload-port`
4. **只燒 app / 只燒 bootloader / 先 erase** — 不必記 PlatformIO 的 target 名稱

另外，全域規則要求「可重複的固定流程一律腳本化」，燒錄目前還是靠人手打指令，本來就是該補腳本的位置。

---

## 建議形狀

沿用專案既有的「shell 入口 + 獨立 Python 實作」慣例（同 `gen-sound.sh` → `gen_sound.py`），不用 `monitor.sh` 的 heredoc 寫法——燒錄邏輯量比 monitor 大，內嵌會難維護。

```
scripts/flash.sh     # 入口：參數解析、路徑組裝、呼叫 penv 的 python
scripts/flash.py     # 實作：讀 sdkconfig/partitions.csv 組 argv、補 sys.path、esptool.main()
```

`flash.sh` 的參數面：

```bash
./scripts/flash.sh                      # build + 全燒 + 硬重置
./scripts/flash.sh --no-build           # 只燒現有產物
./scripts/flash.sh --app-only           # 只燒 0x10000（改 C 碼時最常用，快很多）
./scripts/flash.sh --erase              # 燒之前先 erase_flash
./scripts/flash.sh --port /dev/cu.xxx   # 覆寫自動偵測
./scripts/flash.sh --monitor 20         # 燒完接著讀 20 秒序列埠
```

`flash.py` 的骨架：

| 步驟 | 作法 |
|------|------|
| 決定埠 | 參數優先；否則 glob `/dev/cu.usbmodem*`，恰好一個才自動採用，多個就報錯要求指定 |
| 決定參數 | 從 `sdkconfig.<env>` 取 `CONFIG_ESPTOOLPY_FLASHMODE/FLASHFREQ/FLASHSIZE`、`CONFIG_BOOTLOADER_OFFSET_IN_FLASH`；從 `partitions.csv` 取第一個 `app` 分區的 offset |
| 檢查產物 | 三個 bin 都存在，且比 `src/` 下最新的原始檔新；否則提示先 build 或自動跑 `pio run` |
| 燒錄 | `sys.path` 補 `tool-esptoolpy` 與其 `_contrib` → `esptool.main(argv)`，argv 用**底線**參數 |
| 直譯器 | `$HOME/.platformio/penv/bin/python`（與 `monitor.sh` 一致；系統 `python3` 沒有 pyserial） |

`--app-only` 之所以值得做：目前改一行 C 碼也是三塊全燒，只燒 app 分區能省掉 bootloader 與 partition table 的寫入與驗證。

---

## 建議

**做，但範圍限定在 flash，不碰 build。** 風險集中在「參數來源」與「esptool 版本語法」兩處，兩者都靠「從 sdkconfig 讀 + 固定用底線語法」解掉。

實作前後的驗收方式：先 `flash_id` 確認 16MB 連得上，燒完接 `monitor.sh --reset` 看開機日誌沒有 image header 不符的警告，畫面能正常進擲筊。

---
規格: note/v1
標題: PlatformIO 的 ESP-IDF 專案不會產生也不會燒 SPIFFS image，要自己來
分類: build
觸發時機: 在 PlatformIO + ESP-IDF 專案要用 SPIFFS 分區放資料，或發現 spiffs_create_partition_image() 沒作用時
摘要: spiffs_create_partition_image() 是 IDF 的 custom target，PlatformIO 的建置不執行它、upload 也只燒 bootloader/partitions/firmware，image 不會產生更不會進 flash。要自己用 IDF 內附的 spiffsgen.py 打包、再用 esptool 燒到分區 offset，且打包參數必須與 sdkconfig 一致。
狀態: 生效
建立日期: 2026-08-29
---

## 結論
在 PlatformIO 的 ESP-IDF 專案裡，`spiffs_create_partition_image(... FLASH_IN_PROJECT)`
**寫了也沒用**——那是 IDF 的 custom target，PlatformIO 走自己的 SCons 建置與 upload 流程，
既不會產生 image，`pio run -t upload` 也只燒 bootloader、partitions、firmware 三個檔案。
症狀是韌體跑起來 `esp_vfs_spiffs_register()` 回 `ESP_FAIL`，看起來像檔案系統壞了，
實際上那塊 flash 從來沒被寫過。

自己做兩件事：

1. **打包**：用 IDF 內附的 `components/spiffs/spiffsgen.py`
   （純 Python，不需要另外裝 mkspiffs 執行檔）
   ```
   python3 spiffsgen.py <分區大小> <內容目錄> <輸出檔> \
       --page-size 256 --obj-name-len 32 --meta-len 4 --use-magic --use-magic-len
   ```
   **參數必須與 sdkconfig 一致**（`CONFIG_SPIFFS_PAGE_SIZE`／`OBJ_NAME_LEN`／
   `META_LENGTH`／`USE_MAGIC`／`USE_MAGIC_LENGTH`），不一致就掛不起來，
   而且錯誤訊息只有 `ESP_FAIL`，看不出是打包參數錯。所以要從 sdkconfig 讀而非寫死。

2. **燒錄**：`esptool write_flash <offset> <image>`。offset 要自己從 `partitions.csv` 算——
   分區的 offset 欄留空時是接在前一個分區後面。

本專案的實作：`scripts/mkspiffs.py`（參數取自 sdkconfig 與 partitions.csv）
由 `scripts/gen-content.sh` 呼叫，燒錄走 `scripts/flash-content.sh`。

## 為什麼
- PlatformIO 用 SCons 驅動 CMake 產生建置檔，但只 build 它認得的 target；
  IDF 的 `spiffs_create_partition_image` 掛在 IDF 自己的 flash target 相依鏈上，不在 PlatformIO 的路徑裡
- `flasher_args.json` 裡**看得到** `storage.bin` 的條目，容易誤判「應該有燒到」——
  那是 IDF 佈局的產物，與 PlatformIO 實際下的 esptool 指令無關（`pio run -t upload --verbose` 可看到真正的參數）

## 驗證
```bash
pio run -e <env> -t upload --verbose | grep write_flash   # 只有三個檔案，沒有 storage
find .pio/build/<env> -name storage.bin                   # 找不到
```
燒進去之後，裝置端 `esp_spiffs_info()` 讀得到容量與已用位元組，即代表 image 有效。
本板實測：9MB 分區、掛載 296ms、讀 7.6KB JSON 加 cJSON 解析 10ms。

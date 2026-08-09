---
規格: note/v1
標題: PlatformIO 加了 idf_component.yml 後要清掉 .pio/build，否則元件不會下載
分類: build
觸發時機: 在 PlatformIO 的 ESP-IDF 專案新增或修改 src/idf_component.yml，編譯卻回報找不到該元件的標頭檔時
摘要: PlatformIO 只在 CMake 重新設定時才跑 ESP-IDF component manager；既有 .pio/build 存在時不會重跑，新增的相依會被無聲忽略。pio run -t clean 不夠，要 rm -rf .pio/build 才會觸發，成功後專案根目錄會出現 managed_components/。
狀態: 生效
建立日期: 2026-08-06
---

## 結論
新增 `src/idf_component.yml` 後直接 `rm -rf .pio/build` 再編譯。不要先試 `pio run -t clean`——它保留 CMake 快取，component manager 一樣不會跑。成功的判斷依據是專案根目錄出現 `managed_components/`。

## 為什麼
- component manager 只在 CMake configure 階段執行；`.pio/build` 內有既有 CMake 快取時整個 configure 被跳過。
- 相依被忽略時**不會有任何警告**，只會在編譯階段變成 `fatal error: xxx.h: No such file or directory`，看起來像路徑寫錯而不是相依沒裝。

## 驗證
```bash
rm -rf .pio/build
~/.platformio/penv/bin/pio run
ls managed_components/     # 應出現元件目錄
```
本機實測：加入 `waveshare/esp_lcd_sh8601` 後直接編譯失敗（找不到 `esp_lcd_sh8601.h`），`rm -rf .pio/build` 後成功，並產生 `managed_components/waveshare__esp_lcd_sh8601`。

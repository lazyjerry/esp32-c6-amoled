# 任務計劃：以 Python 改寫燒錄流程可行性研究

## 目標
釐清本專案「燒錄」的實際執行鏈路，判定改用 Python 是否可行，並給出符合既有腳本慣例的設計與風險清單。

## 執行模式
一次完成

## 報告保存路徑
`docs/knowledge-skill/以_Python_改寫燒錄可行性研究-001/`

## 研究動作計畫
<!-- 啟動提問選擇「不先確認研究計畫」，本區塊不啟用 -->
未啟用。

## 階段
- [x] 階段 1：規劃與設定
  - [x] 完成後更新 notes.md
- [x] 階段 2：研究/收集資訊（專案設定、PlatformIO builder、esptool 套件、既有腳本與筆記）
  - [x] 完成後更新 notes.md
- [x] 階段 3：實機驗證（以 Python `import esptool` 對真板做唯讀 `flash_id`）
  - [x] 完成後更新 notes.md
- [x] 階段 4：審查與交付（report.md、知識沉澱）
  - [x] 完成後更新 notes.md

## 關鍵問題
1. 目前燒錄到底是誰在執行？→ 已答：`tool-esptoolpy/esptool.py`，本來就是 Python，PlatformIO 只是組參數。
2. Python 能不能直接驅動 esptool？→ 已答：可以，`sys.path` 補 `<pkg>` 與 `<pkg>/_contrib` 後 `esptool.main(argv)`，已對真板實測通過。
3. 燒錄參數從哪來才不會寫死？→ 已答：`sdkconfig.<env>`（flash mode/freq/size、bootloader offset）+ `partitions.csv`（app offset）。
4. 能否連編譯一起改？→ 已答：不建議，build 是 SCons+CMake+ninja，Python 重寫無效益，應保留 `pio run`。

## 已做決策
- **驗證用唯讀指令（`flash_id`）而非真的寫入**：確認鏈路可行即可，避免在研究階段動到板子上的韌體。
- **建議形狀採「shell 入口 + 獨立 .py」**：與 `gen-sound.sh` / `gen_sound.py` 一致；`monitor.sh` 的 heredoc 只適合小量邏輯。
- **本次只研究不落地**：使用者問的是「是否可以」，實作待確認後另行進行。

## 遇到的錯誤
- `~/.platformio/penv/bin/python -c "import esptool"` → `ModuleNotFoundError: No module named 'esptool'`：esptool 不在 penv，改用套件目錄 `~/.platformio/packages/tool-esptoolpy`。
- 加了套件目錄後 → `ModuleNotFoundError: No module named 'intelhex'`：相依裝在該套件的 `_contrib/`，`sys.path` 需同時加入 `_contrib` 才可用。

## 狀態
**階段 4 完成** - 報告已產出，進入知識沉澱。

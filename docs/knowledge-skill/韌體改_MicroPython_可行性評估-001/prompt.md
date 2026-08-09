# 任務請求：韌體改 MicroPython 可行性評估

## 原始請求

```
請改用 python 取代 cpp
```

選單澄清後確認範圍為：**韌體改 MicroPython** — 板上跑 Python 取代目前的 ESP-IDF C 韌體。

> 補充：專案自有原始碼全是 `.c`，沒有自己的 `.cpp`。唯一的 `.cpp` 在 `managed_components/lvgl__lvgl/` 的 ThorVG，屬第三方。

## 期望產出

- [x] 判定「板上跑 Python 取代 C 韌體」是否成立
- [x] 逐層盤點：顯示、LVGL、音訊、IMU、電源、觸控
- [x] 若不成立，指出卡點與可行的替代路徑

**產出類型：**
- 文件：`docs/knowledge-skill/韌體改_MicroPython_可行性評估-001/report.md`
- 程式碼：本次不動任何程式碼；燒錄 MicroPython 會抹掉現有韌體，需先取得確認

## 參考文件

| 檔案路徑 | 引用範圍描述 |
|----------|--------------|
| `CLAUDE.md` | 硬體實測事實表、應用：擲筊、已驗證可用 |
| `src/` | 現行 C 韌體組成（main / board / cast / cast_ui / imu / audio） |
| `sdkconfig.esp32-c6-devkitc-1` | 確認 IDF_TARGET 與無 SPIRAM 設定 |
| `src/idf_component.yml` | 現行依賴的 IDF 元件清單 |

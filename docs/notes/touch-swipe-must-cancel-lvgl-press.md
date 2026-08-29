---
規格: note/v1
標題: 自寫觸控 swipe 要用 lv_indev_reset 取消該次觸碰，否則滑過按鈕就等於按下去
分類: driver
觸發時機: 要在本板做觸控滑動換頁、或滑動時發現底下的按鈕被誤觸時
摘要: 本板觸控只能裸讀單點座標，滑動要自己判。判定成立的當下必須呼叫 lv_indev_reset(indev, NULL)，否則手指抬起時 LVGL 仍會補一次 CLICKED。座標與面板 368x448 為 1:1，不需縮放也不需鏡射。
狀態: 生效
建立日期: 2026-08-29
---

## 結論
把觸控接成 LVGL 指標裝置（`lv_indev_create()` + `LV_INDEV_TYPE_POINTER`），滑動判定寫在同一支 `read_cb` 裡：

- 座標與面板 1:1，`0x38` 讀到的值直接當像素用，不縮放、不鏡射、不對調 XY。只要夾在 `0~367 / 0~447`。
- 判定門檻：橫移 ≥ 90px（約四分之一螢幕寬）、**橫移 > 縱移 × 2**、700ms 內完成。
- 判定成立的當下做三件事：記下方向、標記這次觸碰已消耗（後續座標一路忽略到手指離開）、**呼叫 `lv_indev_reset(indev, NULL)`**。
- `lv_indev_create()` 會自動綁到當下的預設 display，所以只要在 LVGL 起來之後呼叫即可，不必自己傳 display handle。但這幾個 LVGL 呼叫要包在 `lvgl_port_lock()` 裡。

讀取週期設 20ms（`lv_timer_set_period(lv_indev_get_read_timer(indev), 20)`），LVGL 預設的 33ms 取樣太疏，滑動軌跡會不夠細。

## 為什麼
- **不 reset 就會誤觸**：只把 `data->state` 改成 `RELEASED` 沒有用——LVGL 看到完整的 press→release 週期，照樣在放開位置的物件上送出 `LV_EVENT_CLICKED`。橫掃過按鈕就等於按了它。`lv_indev_reset()` 清掉 `act_obj` 並設 `reset_query`，該次觸碰整個作廢。
- **在 `read_cb` 裡呼叫 reset 是安全的**：`lv_indev_read()` 在 `indev_read_core()` 之後、`indev_pointer_proc()` 之前就會處理 `reset_query`，所以是延後生效，不會在處理途中把狀態抽掉。
- **橫移 > 縱移 × 2 這個條件有實際作用**：實測 6 次大幅斜拉被它擋下，例如 (281,133)→(22,338)，橫移 259px 但縱移 205px。少了這條，斜著滑就會換頁。
- 這顆晶片沒有手勢引擎，也不能用官方元件（見 [touch-not-real-ft5x06.md](touch-not-real-ft5x06.md)），所以上面每一項都得自己做。

## 驗證
驗證程式在 [docs/examples/touch-indev-verify.c](../examples/touch-indev-verify.c)，畫面是「跟著手指的圓點 + 置中按鈕 + 滑動計數」，按下與放開各記一筆座標。

2026-08-29 實測：

- 座標 x=11~346、y=10~425。**XY 沒對調**——若對調，y 會來自 0~367 的範圍而碰不到 400 以上。
- 20 次 `CLICKED`，座標全落在按鈕範圍內（220×110 置中 → x∈[74,294]、y∈[169,279]）。
- 13 次滑動（左 9 右 4）方向與位移正負一致。**橫掃過按鈕時 TAP 計數不動**，這就是 reset 生效的證據。

驗證畫面把按鈕放正中央是個疏漏：鏡射之後點擊仍落在按鈕上，測不出左右反了沒。要驗鏡射得把按鈕放角落，或直接看圓點有沒有跟著手指。

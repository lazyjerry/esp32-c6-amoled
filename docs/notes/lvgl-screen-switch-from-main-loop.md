---
規格: note/v1
標題: LVGL 回呼裡不要切畫面，也不要在 exit() 刪掉正在顯示的畫面
分類: lvgl
觸發時機: 要用 screen_mgr 做多畫面切換、或畫面切換後當機／畫面全黑時
摘要: 畫面切換一律在主迴圈做：LVGL 事件回呼只設旗標，由該畫面的 tick() 執行 screen_mgr_goto()。寫面板暫存器（亮度 0x51）同樣要留給主迴圈——在回呼裡寫會回報成功但面板不理。畫面物件建一次就快取，不要在 exit() 刪除——exit() 早於下一個畫面的 enter()，刪掉的正是當下顯示中的那一個。
狀態: 生效
建立日期: 2026-08-29
---

## 結論
兩條規則，違反哪一條都會在切畫面時炸：

**一、切畫面只在主迴圈做。** LVGL 事件回呼（`LV_EVENT_CLICKED` 之類）跑在 LVGL 任務，
主迴圈跑在 app 任務，而 `screen_mgr` 沒有上鎖。回呼裡只設一個 `volatile bool` 旗標，
由該畫面的 `tick()`（主迴圈呼叫）真正執行 `screen_mgr_goto()`：

```c
static void on_click(lv_event_t *e) { s_goto_next = true; }
static void tick(void) { if (s_goto_next) { s_goto_next = false; screen_mgr_goto(&next); } }
```

**一之二、面板暫存器也只在主迴圈寫。** 亮度指令（SH8601 的 `0x51`）與畫面資料共用同一條
QSPI。在 LVGL 事件回呼裡寫，`esp_lcd_panel_io_tx_param()` 會回 `ESP_OK` 但面板毫無反應——
那時 LVGL 可能正把畫面推上同一條匯流排。設定頁拖亮度完全沒作用就是這樣來的——
log 裡值算對、寫入也「成功」。作法與切畫面相同：回呼只記下要調到多少，
`tick()` 拿著 `lvgl_port_lock()` 去寫。走 I2C 的周邊（codec 音量）不受影響，那是另一條匯流排。

**二、畫面物件建一次就留著，不要在 `exit()` 刪。** `screen_mgr_goto()` 的順序是
舊畫面 `exit()` → 新畫面 `enter()`，所以 `exit()` 執行時被刪的正是當下顯示中的畫面，
中間會有一段沒有 active screen。改成 `enter()` 裡「沒建過才建」，之後只 `lv_screen_load()`。
`exit()` 拿來暫停這個畫面的 `lv_timer` 就好——看不見的畫面不該還在算動畫。

`lv_screen_load()` 的時機屬於 `screen_mgr`，不要在各畫面的 `init()` 裡搶著載入。

## 為什麼
- LVGL 全域狀態不是執行緒安全的，`lvgl_port_lock()` 保護的是 LVGL 自己，不保護 `screen_mgr`。
  兩個任務同時動 `s_current`，指標會停在半途。
- 三個畫面加起來的 LVGL 物件只有幾 KB，而堆積還有 270KB 以上。用記憶體換掉一整類
  生命週期錯誤是划算的；真的需要釋放時再說。

## 驗證
實測 20 次切換（正殿↔參拜簿↔設定↔擲筊）無異常，log 逐筆對得上：

```
I (43990) screen: 正殿 → 參拜簿
I (45020) screen: 正殿 → 擲筊
I (61180) cast_ui: 拋擲 48 幀 / 2140 ms（22 fps）
```

進入擲筊是走點擊（LVGL 回呼設旗標），離開是走觸控滑動（主迴圈派發事件），
兩條路徑都經過主迴圈才動 `screen_mgr`。

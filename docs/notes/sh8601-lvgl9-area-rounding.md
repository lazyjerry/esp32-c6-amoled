---
規格: note/v1
標題: SH8601 接 LVGL 9：偶奇對齊要掛 INVALIDATE_AREA 事件，v8 的 rounder_cb 已移除
分類: lvgl
觸發時機: 在 SH8601／QSPI AMOLED 上整合 LVGL 9，畫面出現斜切、撕裂或位移時
摘要: SH8601 要求繪圖區起點偶數、終點奇數，LVGL 預設不會對齊。LVGL 9 拿掉了 v8 的 rounder_cb，改用 lv_display_add_event_cb 監聽 LV_EVENT_INVALIDATE_AREA 修改 lv_area_t。另需 esp_lvgl_port 的 flags.swap_bytes 才有正確顏色。
狀態: 生效
建立日期: 2026-08-09
---

## 結論

`lvgl_port_add_disp()` 之後補一段區域對齊，缺了畫面會斜掉：

```c
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 &= ~1;  area->y1 &= ~1;
    area->x2 |=  1;  area->y2 |=  1;
}
lv_display_add_event_cb(disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);
```

顏色靠 `lvgl_port_display_cfg_t.flags.swap_bytes = true`，不要自己在應用層 byte-swap。

元件版本有下限：**esp_lvgl_port 2.9 需要 LVGL ≥ 9.3**（用到 `LV_COLOR_FORMAT_RGB565_SWAPPED`），配 9.2 會編不過。

繪圖緩衝用 `368 × 32 列`（約 23KB）雙緩衝。這片板子沒有 PSRAM，368×448 整張 frame buffer 要 330KB，配不下；緩衝再往上加會和 TLS handshake 搶堆積。

## 為什麼

- SH8601 的 column/row address set 以 2 像素為單位，奇數起點會讓後續整列位移
- LVGL 9 移除了 v8 的 `disp_drv.rounder_cb`，對應機制改成顯示器層級的 `LV_EVENT_INVALIDATE_AREA` 事件，參數就是待重繪的 `lv_area_t`

## 驗證

實機跑 LVGL 標籤畫面，文字邊緣不撕裂、位置不隨重繪飄移即為正確。顏色若整片偏色，先確認 `swap_bytes` 而不是改色值。

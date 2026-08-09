---
規格: note/v1
標題: 本板觸控只是位址相同，不是真 FT5x06；套官方元件會把晶片寫到不回應 I2C
分類: driver
觸發時機: 要整合本板觸控、或看到 esp_lcd_touch_ft5x06 初始化後出現 I2C hardware timeout detected 時
摘要: 觸控在 0x38 應答、vendor id(0xA3)=0x64，但暫存器語意與 FT5x06 不同。espressif/esp_lcd_touch_ft5x06 的 init 會寫入 8 個 FT5x06 電源管理暫存器（含 2 秒進 Monitor 模式），寫完晶片就停止回應 I2C。改成直接讀 0x02~0x06 五個 byte 即可，不需任何觸控元件。
狀態: 生效
建立日期: 2026-08-06
---

## 結論
不要用 `espressif/esp_lcd_touch_ft5x06`。直接對 `0x38` 從暫存器 `0x02` 連讀 5 個 byte：`[0]` 低 4 bits 是觸控點數，`[1][2]` 是 X（`(buf[1]&0x0F)<<8 | buf[2]`），`[3][4]` 是 Y。20ms 輪詢即可，不必接 INT。

## 為什麼
- 這顆只是 I2C 位址與 FT5x06 相同，暫存器語意不同。官方 Wiki 標 FT6146、官方 BSP 叫它 FT5X06，兩邊都只憑位址對應。
- 該元件的 `touch_ft5x06_init()` 會寫入 8 個 FT5x06 電源管理暫存器（threshold 群 + `TIME_ENTER_MONITOR=2` 秒）。寫進這顆之後它就停止回應 I2C，每次讀取要等約 3.7 秒才吐 `I2C hardware timeout detected`。
- 觸控資料區（`0x02`~`0x06`）的格式兩者一致，所以只讀不寫完全沒問題。

## 驗證
判斷是不是這個問題的關鍵一步：在**建立觸控驅動之前**先讀一次 `0xA3`。

```
panel_io 讀 0xA3：ESP_OK value=0x64     <- 驅動建立前，正常
E lcd_panel.io.i2c: panel_io_i2c_rx_buffer(145): i2c transaction failed
E i2c.master: I2C hardware timeout detected   <- 驅動建立後
```

前面成功、後面失敗 → 是 init 寫壞狀態，不是接線或位址問題。

改用裸讀後實測：vendor id `0x64`，75 秒內取得 546 筆座標，`TD_STATUS` 在觸碰時為 `0x01`、放開為 `0x00`，座標隨手指連續變化（例：x=128 y=86 → x=176 y=143）。

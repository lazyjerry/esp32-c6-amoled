---
規格: note/v1
標題: SH8601 QSPI：自己送面板指令要「指令 <<8 再帶 0x02 opcode」，組錯就靜默無效
分類: driver
觸發時機: 執行期寫面板暫存器（亮度 0x51 之類）沒反應，但函式回 ESP_OK
摘要: QSPI 的 32 bit 寫入是 opcode(0x02)<<24 | 指令<<8 | 位址。指令要往左挪 8 位，只補 opcode 而把指令留在最低 byte 一樣無效。組錯時 SPI 傳輸照樣成功、回 ESP_OK，面板卻收不到有效指令。走 esp_lcd_sh8601 元件的路徑都對，只有直接呼叫 esp_lcd_panel_io_tx_param 要自己組。
狀態: 生效
建立日期: 2026-08-29
---

## 結論
自己呼叫 `esp_lcd_panel_io_tx_param()` 對 SH8601 送指令時，32 bit 的指令值要這樣組：

```c
// opcode(0x02) << 24 | 指令 << 8 | 位址(0x00)
#define LCD_CMD_WRITE(cmd) ((((uint32_t)(cmd) & 0xFF) << 8) | (0x02UL << 24))
esp_lcd_panel_io_tx_param(io, LCD_CMD_WRITE(0x51), &level, 1);   // 亮度 → 0x02005100
```

**兩個部分缺一不可**：補了 opcode 卻把指令留在最低的那個 byte（`0x02000051`）一樣沒有作用。
組錯時 **SPI 傳輸照樣成功、函式照樣回 `ESP_OK`**，面板單純收不到有效指令。
錯誤碼幫不上忙，只能從「明明成功卻沒作用」反推。

## 為什麼
`esp_lcd_sh8601` 的 io 是 `lcd_cmd_bits = 32`、`quad_mode = true`。QSPI 面板的一次寫入是
「opcode + 指令 + 位址」三段塞進 32 bit，元件內部的 `tx_param()` 就是這段算式：

```c
lcd_cmd &= 0xff;
lcd_cmd <<= 8;
lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;   // 0x02
```

官方 BSP 的 `bsp_display_brightness_set()` 一模一樣。所以走元件的路徑
（`init_cmds`、`esp_lcd_panel_*` 那組 API）都是對的，只有繞過元件直接送指令的地方要自己組。

## 驗證
`board_display_brightness()` 原本兩段都缺，所以**開機淡入從來沒有真的淡入過**，
設定頁拖亮度也毫無反應。分段自測（`0x53` 兩種值 × `0x51` 明暗）四段畫面全無變化，
排除了「`0x53` 沒開 BCTRL」與「LVGL 佔用匯流排」；只補 opcode 仍然無效；
補上 `<<8` 之後立刻生效，log 逐格 `ESP_OK` 且畫面跟著變：

```
I (24704) settings-ui: tick 套用亮度 97 → ESP_OK
I (24914) settings-ui: tick 套用亮度 41 → ESP_OK
```

**「回報成功」不等於「面板收到了」**——這條匯流排上的寫入沒有回讀確認，
之後要加任何面板暫存器操作（idle 模式、色彩反轉之類）都適用同一條檢查。

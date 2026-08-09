---
規格: note/v1
標題: 觸控 IC 掃不到不代表沒有：reset 掛在 TCA9554，冷開機時被拉住
分類: hardware
觸發時機: 在 ESP32-C6-Touch-AMOLED-1.8 上 I2C 掃描找不到觸控位址（0x38／0x15），或要判定板子 V1/V2 版本時
摘要: 本板觸控與 LCD 的 reset 接在 TCA9554（I2C 0x20）的 bit5／bit4，冷開機時擴充晶片全腳為輸入、觸控被拉在 reset，掃描掃不到；要先把該兩位元設為輸出並拉低再拉高，觸控才會出現在匯流排上。TCA9554 是獨立晶片，狀態不隨 CPU reset 清除，因此後續重開機看得到觸控，必須整片斷電才會回到冷開機狀態。
狀態: 生效
建立日期: 2026-08-06
---

## 結論
掃不到觸控時不要懷疑觸控壞掉或腳位錯。先對 TCA9554（`0x20`）把 bit4（LCD_RST）與 bit5（TOUCH_RST）設成輸出、拉低 50ms 再拉高，然後重掃。判定版本：`0x38` → V1（SH8601 + FT5x06）、`0x15` → V2（CO5300 + CST820）。改寫 TCA9554 時只動這兩個位元，其餘位元先讀回再回寫——bit7 是喇叭電源，整片覆寫會誤動。

## 為什麼
- TCA9554 冷開機時全部腳位是輸入態，觸控 reset 沒有被驅動成高電位，觸控 IC 不上線。
- TCA9554 是匯流排上的獨立晶片，**狀態不隨 CPU reset 清除**。所以放開一次之後重新燒錄、重開機都還看得到觸控——要回到冷開機狀態必須整片斷電。這會讓「第一次掃不到、後來都掃得到」看起來像偶發問題。
- 官方 BSP 的 `bsp_board_detect()` 也是先呼叫 `bsp_display_touch_reset()` 才 probe，順序不能顛倒。

## 驗證
本機實測（`src/main.c` 的 boardid 程式）：

```
---- reset 前 ----（TCA9554 仍在冷開機預設）
  0x18 0x20 0x34 0x51 0x6B        <- 沒有 0x38，也沒有 0x15
TCA9554 初始狀態 config=0x4F output=0xFF
---- reset 後 ----
  0x18 0x20 0x34 0x38 0x51 0x6B   <- 0x38 出現
```

腳位與常數來源：`Waveshare-ESP32-components/bsp/esp32_c6_touch_amoled_1_8/include/bsp/esp32_c6_touch_amoled_1_8.h`（`BSP_LCD_RST`=pin4、`BSP_LCD_TOUCH_RST`=pin5、I2C SDA=8／SCL=7）。

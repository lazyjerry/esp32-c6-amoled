---
規格: note/v1
標題: 本板兩顆按鍵：BOOT 在 GPIO9 喚不醒 deep sleep，PWR 直通 AXP2101
分類: hardware
觸發時機: 要在 ESP32-C6-Touch-AMOLED-1.8 上做按鍵休眠／喚醒，或規劃低功耗模式時
摘要: 板上只有 BOOT 與 PWR 兩顆鍵（無 RESET）。C6 的 LP IO 只有 GPIO0~7 且被 LCD QSPI／SD／I2C 佔滿，BOOT 在 GPIO9 只能喚醒 light sleep；PWR 直通 AXP2101 PWRKEY，長按硬體斷電攔不到，短按可由 I2C 讀 INTSTS2 bit3 得知，並可寫 COMMON_CONFIG bit0 軟關機，比 light sleep 省得多。
狀態: 生效
建立日期: 2026-08-09
---

## 結論

**省電優先走 PWR 的軟關機，不要用 light sleep。** 短按旗標讀到後寫 `COMMON_CONFIG`(`0x10`) bit0，AXP2101 切掉所有電軌降到 μA 等級；light sleep 只是 CPU 睡著，還在 mA 等級。開機不必寫韌體，按 PWR 即可（`0x27` bit[1:0] 設門檻，128ms／512ms／1s／2s）。

PWR 鍵不接 SoC，走 AXP2101（I2C `0x34`），短按與長按是兩條完全不同的路徑：

- **短按**在 `INTSTS2`(`0x49`) bit3 留旗標，輪詢讀到即可當自訂按鍵，寫 1 清除；要先在 `INTEN2`(`0x41`) bit3 開啟
- **長按**由 PMIC 硬體斷電。bit2 的長按旗標會亮，但電緊接著就斷，韌體來不及反應。唯一能改的是 `0x10` bit2（停用長按斷電）與 `0x27` bit[3:2]（4／6／8／10 秒門檻），**別停用**，那是唯一的硬體保命開關
- 另有兩個邊緣中斷：bit1 按下、bit0 放開。要自己算「按住 N 秒」用這兩個，不必動 PMIC 設定
- 開機過程本身會留下旗標，初始化時先清一次，否則一啟動就會誤判成按了一下

真要休眠才走 **light sleep**，喚醒源只能是 BOOT（GPIO9）：`gpio_wakeup_enable(9, GPIO_INTR_LOW_LEVEL)` + `esp_sleep_enable_gpio_wakeup()`。**不要規劃 deep sleep 靠按鍵喚醒**，這片板子做不到，deep sleep 只剩 RTC timer 一種醒法。進睡前與醒來後都要等按鍵放開，否則低電位會立刻把自己叫醒或被判成第二次切換。

## 為什麼

- ESP32-C6 只有 **GPIO0~7** 是 LP／RTC IO（`esp_hal_gpio/esp32c6/rtc_io_periph.c` 的 `rtc_io_num_map`，GPIO8 起全是 `-1`），`esp_sleep_is_valid_wakeup_gpio()` 查的就是這張表
- 這片板子把 LP IO 全用掉了：`0~5` LCD QSPI、`6` SD CS、`7` I2C SCL，沒有一支接到按鍵
- light sleep 的 GPIO 喚醒不限 RTC IO，所以 GPIO9 可用（前提是沒開 `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP`）
- 官方 BSP 標頭寫 `BSP_CAPS_BUTTONS 0`，按鍵定義要自己查，別指望 BSP

## 驗證

```bash
IDF=~/.platformio/packages/framework-espidf
sed -n '10,30p' $IDF/components/esp_hal_gpio/esp32c6/rtc_io_periph.c   # GPIO8 起為 -1
```

實機：light sleep 期間 USB Serial/JTAG 會斷線（`SOC_USB_SERIAL_JTAG_SUPPORT_LIGHT_SLEEP` 在 C6 的 soc_caps.h 是註解掉的），醒來後 macOS 重新列舉 `/dev/cu.usbmodem1101`，`pio device monitor` 要重開。

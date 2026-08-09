---
規格: note/v1
標題: BSP 的 I2S DOUT/DSIN 以 ESP 為視角，喇叭功放在 TCA9554 bit7 不是 GPIO
分類: hardware
觸發時機: 要在 ESP32-C6-Touch-AMOLED-1.8 上出聲，或 I2S 設定看起來都對卻完全沒聲音時
摘要: ESP 端資料輸出是 GPIO23、輸入是 GPIO21，反接就是全靜音。功放電源掛在 TCA9554 bit7，es8311_codec_cfg_t 的 pa_pin 要填 -1 並自己去操作擴充晶片。
狀態: 生效
建立日期: 2026-08-09
---

## 結論
播放路徑的 `i2s_std_gpio_config_t` 填 `.mclk=19 .bclk=20 .ws=22 .dout=23 .din=21`（只播放的話 `din` 填 `I2S_GPIO_UNUSED`）。喇叭功放不是 GPIO，要先把 TCA9554（`0x20`）bit7 設成輸出再拉高，沒拉高一樣是完全無聲；`es8311_codec_cfg_t.pa_pin` 填 `-1`，另外 `master_mode=false`、`use_mclk=true`。

## 為什麼
- BSP 的 `BSP_I2S_DOUT` = GPIO23、`BSP_I2S_DSIN` = GPIO21，命名是**以 ESP 為視角**：`BSP_I2S_GPIO_CFG` 裡 `.dout = BSP_I2S_DOUT`、`.din = BSP_I2S_DSIN`。若照「codec 的 DOUT」去理解就會把 23 和 21 對調，時脈全對、資料線接到輸入腳，結果是安靜到像 codec 沒初始化。
- `BSP_POWER_AMP_IO` 定義成 `IO_EXPANDER_PIN_NUM_7`，不是 SoC 的腳位。BSP 自己是呼叫 `esp_io_expander_set_level()` 開它。
- TCA9554 冷開機時所有腳是輸入，功放預設不通電；這和觸控 reset 被拉住是同一顆晶片、同一類問題，見 [tca9554-holds-touch-reset.md](tca9554-holds-touch-reset.md)。

## 驗證
`esp_codec_dev_open()` 成功只代表 I2C 那條路通了，和有沒有聲音無關——log 會照印：

```
I ES8311: Work in Slave mode
I I2S_IF: STD: TX, sample_rate_hz: 16000, mclk_multiple: 256
I Adev_Codec: Open codec device OK
```

要確認資料線，把 `.dout` 改成另一支再燒一次：如果兩種接法都「初始化成功但沒聲音」，問題在功放電源；只有一種有聲音，那支就是 ESP 的輸出。

---
規格: note/v1
標題: esp_codec_dev 的 I2S 資料層只收偶數聲道，單聲道音源要自己攤成兩個 slot
分類: driver
觸發時機: 用 espressif/esp_codec_dev 播單聲道 PCM，esp_codec_dev_open 直接回失敗時
摘要: sample_info 的 channel 必須是偶數，填 1 會被擋掉。另外 audio_codec_i2c_cfg_t 的 addr 是 8 位元寫入位址（ES8311 填 0x30，匯流排上看到的是 0x18），且在 IDF 5.3 以上要填 bus_handle 才會走新的 i2c_master 驅動。
狀態: 生效
建立日期: 2026-08-09
---

## 結論
用 `espressif/esp_codec_dev` 播單聲道 PCM 時，`esp_codec_dev_sample_info_t.channel` 要填 `2`，把每個取樣複製到左右兩個 slot 再送。分段轉換即可，不必為此把整段音源存成雙聲道。

建立控制介面時另外兩件事：`audio_codec_i2c_cfg_t.addr` 填的是**8 位元寫入位址**（ES8311 是 `0x30`，I2C 掃描看到的 `0x18` 是右移一位的結果），且 IDF 5.3 以上要一併填 `bus_handle`，元件才會走新的 `i2c_master` 驅動而不是已被移除的舊版 `driver/i2c.h`。

## 為什麼
- `audio_codec_data_i2s.c` 的 `set_fmt` 有一道檢查：`if (fs->channel == 0 || (fs->channel >> 1 << 1) != fs->channel)` 就報 `Not support channel`。奇數聲道一律擋掉，填 1 會讓 `esp_codec_dev_open()` 直接失敗。
- `audio_codec_ctrl_i2c.c` 內部做 `device_address = (i2c_cfg->addr >> 1)`，所以標頭裡的 `ES8311_CODEC_DEFAULT_ADDR` 本來就是 8 位元格式，不要自己先除以 2。
- 新舊 I2C 驅動的分支條件是 `ESP_IDF_VERSION >= 5.3 && !CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE`。PlatformIO 沒有把 `ESP_IDF_VERSION` 環境變數餵給 Kconfig（編譯時會看到這則 warning），該選項因此不會出現在 sdkconfig，未定義即視為 0，剛好落在新驅動這一邊。

## 驗證
成功開啟時的 log：

```
I ES8311: Work in Slave mode
I I2S_IF: STD: TX, data_bit: 16, slot_bit: 16, slot_mode: STEREO, slot_mask: 0x3
I Adev_Codec: Open codec device OK
```

`slot_mode: STEREO` 就是 `channel = 2` 生效的樣子。填 1 的話停在 `Not support channel 1`，`Open codec device` 那行不會出現。

// ES8311 + I2S 播放內建的 PCM 音效
#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t audio_init(void);

// 排入一次筊落地的撞擊聲；volume 0~100 是這一聲的相對音量。
// 不阻塞，佇列滿了就丟掉——動畫的節奏比補播一聲重要
void audio_play_clack(uint8_t volume);

// 全域輸出音量 0~100，設定頁調的就是這個。
// audio_play_clack() 的 volume 是單一音效在這之下的相對值，兩者相乘
esp_err_t audio_set_volume(uint8_t percent);
uint8_t audio_volume(void);

// ES8311 + I2S 播放內建的 PCM 音效
#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t audio_init(void);

// 排入一次筊落地的撞擊聲；volume 0~100 是這一聲的相對音量。
// 不阻塞，佇列滿了就丟掉——動畫的節奏比補播一聲重要
void audio_play_clack(uint8_t volume);

// 一聲鐘。1.1 秒，比筊聲長得多——排在動畫中間會蓋掉後面的撞擊聲，
// 只用在「一段落結束」的地方
void audio_play_bell(uint8_t volume);

// 全域輸出音量 0~100，設定頁調的就是這個。這是**使用者刻度**——
// 內部會壓到 codec 還不破音的範圍，100 是「這台機器最大的乾淨音量」而不是 codec 滿檔。
// audio_play_clack() 的 volume 是單一音效在這之下的相對值，兩者相乘
esp_err_t audio_set_volume(uint8_t percent);
uint8_t audio_volume(void);

// QMI8658 加速度計：分辨「上下甩」與「左右晃」兩種手勢
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t imu_init(void);

// 取走一次手勢事件，沒有事件回 false。事件由背景任務產生，取走即清除。
// 沒有要處理的時候也該定期取走，否則會留到下一個狀態才觸發
// 目前的重力方向（g）。姿勢判定用——例如「前傾」是這個向量相對某個基準轉了多少角度。
// 尚未取到第一筆讀數時回 false
bool imu_gravity(float out[3]);

bool imu_take_shake(void);   // 上下甩：擲筊
bool imu_take_swipe(void);   // 左右晃：把結果畫面收掉

// QMI8658 加速度計：偵測「由下往上、再由上往下」的甩動
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t imu_init(void);

// 取走一次搖動事件，沒有事件回 false。事件由背景任務產生，取走即清除
bool imu_take_shake(void);

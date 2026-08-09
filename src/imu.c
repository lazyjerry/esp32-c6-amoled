// QMI8658 只用加速度計。偵測不看單一軸，而是先用低通估出重力方向，
// 再把動態加速度投影到那條軸上——板子怎麼拿都算得出「上下」，符合三軸甩動的需求。
#include "imu.h"

#include <math.h>

#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define QMI8658_ADDR 0x6B

#define REG_WHO_AM_I 0x00
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03
#define REG_CTRL7    0x08
#define REG_AX_L     0x35

#define WHO_AM_I_VALUE 0x05
#define CTRL1_ADDR_AI  (1 << 6)   // 位址自動遞增，一次讀完六個資料暫存器要靠它
#define CTRL2_ACC_8G_125HZ 0x26   // aFS=±8g, aODR=125Hz
#define CTRL7_ACC_EN   0x01

#define LSB_PER_G 4096.0f         // ±8g 檔位

#define SAMPLE_MS       10
#define GRAVITY_ALPHA   0.05f     // 重力低通，時間常數約 200ms
#define SWING_G         1.2f      // 一個半程要跨過的動態加速度
#define SWING_HALVES    3         // 上→下→上，少於三段當成撞到桌子
#define SWING_WINDOW_MS 900
#define COOLDOWN_MS     2000

static const char *TAG = "imu";

static i2c_master_dev_handle_t s_dev;
static volatile bool s_shake;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 200);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 200);
}

static esp_err_t read_accel(float *x, float *y, float *z)
{
    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(reg_read(REG_AX_L, raw, sizeof(raw)), TAG, "accel");

    *x = (int16_t)(raw[0] | (raw[1] << 8)) / LSB_PER_G;
    *y = (int16_t)(raw[2] | (raw[3] << 8)) / LSB_PER_G;
    *z = (int16_t)(raw[4] | (raw[5] << 8)) / LSB_PER_G;
    return ESP_OK;
}

static void imu_task(void *arg)
{
    float gx = 0, gy = 0, gz = 0;
    bool primed = false;
    int halves = 0, sign = 0;
    int64_t window_us = 0, fired_us = 0;

    while (1) {
        float ax, ay, az;
        if (read_accel(&ax, &ay, &az) == ESP_OK) {
            if (!primed) {
                gx = ax; gy = ay; gz = az;
                primed = true;
            }
            gx += GRAVITY_ALPHA * (ax - gx);
            gy += GRAVITY_ALPHA * (ay - gy);
            gz += GRAVITY_ALPHA * (az - gz);

            float norm = sqrtf(gx * gx + gy * gy + gz * gz);
            if (norm > 0.3f) {
                // 動態加速度在重力軸上的投影：正負就是上下
                float p = ((ax - gx) * gx + (ay - gy) * gy + (az - gz) * gz) / norm;
                int s = (p > SWING_G) ? 1 : (p < -SWING_G ? -1 : 0);
                int64_t now = esp_timer_get_time();

                if (halves > 0 && now - window_us > SWING_WINDOW_MS * 1000) {
                    halves = 0;
                    sign = 0;
                }
                if (s != 0 && s != sign) {
                    if (halves == 0) window_us = now;
                    halves++;
                    sign = s;
                    ESP_LOGI(TAG, "甩動 %d/%d（%.1f g）", halves, SWING_HALVES, p);

                    if (halves >= SWING_HALVES && now - fired_us > COOLDOWN_MS * 1000) {
                        fired_us = now;
                        halves = 0;
                        sign = 0;
                        s_shake = true;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

esp_err_t imu_init(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI8658_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_dev), TAG, "add dev");

    uint8_t who = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_WHO_AM_I, &who, 1), TAG, "who am i");
    ESP_RETURN_ON_FALSE(who == WHO_AM_I_VALUE, ESP_ERR_NOT_FOUND, TAG,
                        "WHO_AM_I 讀到 0x%02X，預期 0x%02X", who, WHO_AM_I_VALUE);

    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL1, CTRL1_ADDR_AI), TAG, "ctrl1");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL2, CTRL2_ACC_8G_125HZ), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL7, CTRL7_ACC_EN), TAG, "ctrl7");
    vTaskDelay(pdMS_TO_TICKS(50));

    float x = 0, y = 0, z = 0;
    ESP_RETURN_ON_ERROR(read_accel(&x, &y, &z), TAG, "first read");
    // 靜置時合力應該接近 1g；差太多表示量程或位元組順序設錯
    ESP_LOGI(TAG, "QMI8658 就緒，靜置 %.2f %.2f %.2f g（合力 %.2f）",
             x, y, z, sqrtf(x * x + y * y + z * z));

    ESP_RETURN_ON_FALSE(xTaskCreate(imu_task, "imu", 3072, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

bool imu_take_shake(void)
{
    if (!s_shake) return false;
    s_shake = false;
    return true;
}

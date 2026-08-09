// QMI8658 只用加速度計。兩種手勢都不看單一軸，而是先用低通估出重力方向，
// 再看動態加速度落在哪裡：沿重力軸的分量是上下甩（擲筊），垂直於重力的是左右晃（重置）。
// 板子怎麼拿都算得出來，符合三軸的需求。
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

#define SAMPLE_MS     10
#define GRAVITY_ALPHA 0.05f       // 重力低通，時間常數約 200ms

static const char *TAG = "imu";

// 一次「半程」是動態加速度跨過門檻、而且方向和上一次相反。
// 上下甩要三段（上→下→上）才算數，免得放下板子也觸發；左右晃是重置用的輕手勢，兩段就夠。
typedef struct {
    const char *name;
    float thresh;          // g
    int halves_needed;
    int window_ms;         // 幾段之間允許的間隔
    int cooldown_ms;
    int halves, sign;
    int64_t window_us, fired_us;
    volatile bool fired;
} swing_t;

static swing_t s_shake = {.name = "上下甩", .thresh = 1.2f, .halves_needed = 3,
                          .window_ms = 900, .cooldown_ms = 2000};
static swing_t s_swipe = {.name = "左右晃", .thresh = 0.9f, .halves_needed = 2,
                          .window_ms = 700, .cooldown_ms = 1500};

static i2c_master_dev_handle_t s_dev;

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

static void swing_feed(swing_t *sw, float value, int64_t now)
{
    if (sw->halves > 0 && now - sw->window_us > sw->window_ms * 1000) {
        sw->halves = 0;
        sw->sign = 0;
    }

    int s = (value > sw->thresh) ? 1 : (value < -sw->thresh ? -1 : 0);
    if (s == 0 || s == sw->sign) return;

    if (sw->halves == 0) sw->window_us = now;
    sw->halves++;
    sw->sign = s;
    ESP_LOGI(TAG, "%s %d/%d（%.1f g）", sw->name, sw->halves, sw->halves_needed, value);

    if (sw->halves < sw->halves_needed) return;

    // 數滿就重來。冷卻中的話只是吞掉，不能讓計數累積成 3/2、4/2
    sw->halves = 0;
    sw->sign = 0;
    if (now - sw->fired_us > sw->cooldown_ms * 1000) {
        sw->fired_us = now;
        sw->fired = true;
    }
}

static bool swing_take(swing_t *sw)
{
    if (!sw->fired) return false;
    sw->fired = false;
    return true;
}

static void imu_task(void *arg)
{
    float g[3] = {0, 0, 0};
    bool primed = false;

    while (1) {
        float a[3];
        if (read_accel(&a[0], &a[1], &a[2]) == ESP_OK) {
            if (!primed) {
                for (int i = 0; i < 3; i++) g[i] = a[i];
                primed = true;
            }
            for (int i = 0; i < 3; i++) g[i] += GRAVITY_ALPHA * (a[i] - g[i]);

            float norm = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
            if (norm > 0.3f) {
                float dyn[3] = {a[0] - g[0], a[1] - g[1], a[2] - g[2]};
                float up = (dyn[0] * g[0] + dyn[1] * g[1] + dyn[2] * g[2]) / norm;

                // 最接近水平的那一軸（重力分量最小）拿來當左右的帶號量；
                // 直接用垂直分量的長度會沒有正負，數不出方向反轉
                int lat = 0;
                for (int i = 1; i < 3; i++) {
                    if (fabsf(g[i]) < fabsf(g[lat])) lat = i;
                }

                int64_t now = esp_timer_get_time();
                swing_feed(&s_shake, up, now);
                swing_feed(&s_swipe, dyn[lat], now);
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

bool imu_take_shake(void) { return swing_take(&s_shake); }

bool imu_take_swipe(void) { return swing_take(&s_swipe); }

#include "touch.h"

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#define TOUCH_ADDR    0x38
#define REG_TD_STATUS 0x02   // 觸控點數，後接 0x03~0x06 為第一點 XH/XL/YH/YL
#define REG_VENDOR_ID 0xA3
#define VENDOR_ID     0x64   // 實測值，用來確認匯流排上的確是這顆

// I2C 逾時。這支跑在 LVGL 的 timer 任務裡，卡住就是整個畫面卡住，
// 所以寧可丟掉一次取樣也不等太久——匯流排上還有 IMU 與 codec 在搶
#define I2C_TIMEOUT_MS 100

// 讀取週期。20ms 是筆記裡實測可用的值，比 LVGL 預設的 33ms 密，滑動軌跡才夠細
#define READ_PERIOD_MS 20

// 判定門檻：螢幕寬 368，橫移四分之一寬才算滑動，點按時的手抖不會誤判
#define SWIPE_MIN_DX 90
// 橫移要明顯大於縱移，否則那是上下捲動不是換頁
#define SWIPE_DX_OVER_DY 2
// 慢慢拖過去不算滑動——那多半是在拖曳畫面上的東西
#define SWIPE_MAX_MS 700

static const char *TAG = "touch";

static i2c_master_dev_handle_t s_dev;
static lv_indev_t *s_indev;

// 一次觸碰的追蹤狀態，只在 LVGL 任務裡讀寫
static lv_point_t s_last;
static int32_t s_x0, s_y0;
static uint32_t s_t0;
static bool s_tracking;
static bool s_consumed;   // 這次觸碰已判成滑動，剩下的座標一路忽略到手指離開

// LVGL 任務寫、主迴圈取走。單一寫入者對單一讀取者的旗標，和 imu.c 同樣作法
static volatile touch_swipe_t s_pending;

static esp_err_t touch_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, I2C_TIMEOUT_MS);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint8_t buf[5] = {0};
    esp_err_t err = touch_read(REG_TD_STATUS, buf, sizeof(buf));
    bool pressed = (err == ESP_OK) && ((buf[0] & 0x0F) > 0);

    // 放開時仍要回報最後座標，LVGL 靠它決定這次點擊落在哪個物件上
    data->point = s_last;

    if (!pressed) {
        s_tracking = false;
        s_consumed = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    int32_t x = ((buf[1] & 0x0F) << 8) | buf[2];
    int32_t y = ((buf[3] & 0x0F) << 8) | buf[4];
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= BOARD_LCD_H_RES) x = BOARD_LCD_H_RES - 1;
    if (y >= BOARD_LCD_V_RES) y = BOARD_LCD_V_RES - 1;
    s_last.x = x;
    s_last.y = y;
    data->point = s_last;

    if (s_consumed) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (!s_tracking) {
        s_tracking = true;
        s_x0 = x;
        s_y0 = y;
        s_t0 = lv_tick_get();
    } else {
        int32_t dx = x - s_x0, dy = y - s_y0;
        int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        if (adx >= SWIPE_MIN_DX && adx > ady * SWIPE_DX_OVER_DY &&
            lv_tick_elaps(s_t0) <= SWIPE_MAX_MS) {
            s_pending = dx < 0 ? TOUCH_SWIPE_LEFT : TOUCH_SWIPE_RIGHT;
            s_consumed = true;
            // 取消這次觸碰。不取消的話手指抬起時 LVGL 會照常補一次 click，
            // 滑過按鈕就等於按下去了。reset 是延後生效的，在 read_cb 裡呼叫是安全的
            lv_indev_reset(indev, NULL);
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
    }

    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t touch_init(void)
{
    ESP_RETURN_ON_FALSE(lv_display_get_default(), ESP_ERR_INVALID_STATE, TAG,
                        "要先起 LVGL，indev 才綁得到 display");

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c_bus(), &cfg, &s_dev), TAG, "add dev");

    // 只讀不寫。讀得到就表示 TCA9554 的觸控 reset 已經放開（board_init 做的）
    uint8_t vendor = 0;
    ESP_RETURN_ON_ERROR(touch_read(REG_VENDOR_ID, &vendor, 1), TAG, "讀不到觸控 @0x38");
    if (vendor != VENDOR_ID) {
        ESP_LOGW(TAG, "vendor id=0x%02X，與實測的 0x%02X 不同", vendor, VENDOR_ID);
    }

    // LVGL 不是執行緒安全的，這裡跑在主任務、read_cb 跑在 LVGL 任務，要先拿鎖
    lvgl_port_lock(0);
    s_indev = lv_indev_create();
    if (s_indev) {
        lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_indev, read_cb);
        lv_timer_set_period(lv_indev_get_read_timer(s_indev), READ_PERIOD_MS);
    }
    lvgl_port_unlock();
    ESP_RETURN_ON_FALSE(s_indev, ESP_ERR_NO_MEM, TAG, "indev");

    ESP_LOGI(TAG, "觸控就緒（vendor 0x%02X，%d ms 輪詢）", vendor, READ_PERIOD_MS);
    return ESP_OK;
}

touch_swipe_t touch_take_swipe(void)
{
    touch_swipe_t s = s_pending;
    if (s != TOUCH_SWIPE_NONE) s_pending = TOUCH_SWIPE_NONE;
    return s;
}

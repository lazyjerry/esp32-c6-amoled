// PCF85063 暫存器與時間格式都是 BCD，且秒暫存器的 bit7 借去當振盪器停止旗標，
// 讀秒時一定要遮掉，否則 40 秒之後會讀成 100 以上的值。
#include "rtc.h"

#include "board.h"
#include "esp_check.h"
#include "esp_log.h"

#define PCF85063_ADDR    0x51
#define REG_CONTROL_1    0x00
#define REG_SECONDS      0x04   // bit7 = OS（振盪器停止過）
#define CTRL1_STOP       (1 << 5)
#define SECONDS_OS       (1 << 7)

static const char *TAG = "rtc";

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd_to_bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin_to_bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 200);
}

static esp_err_t write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[8];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[i + 1] = data[i];
    return i2c_master_transmit(s_dev, buf, len + 1, 200);
}

esp_err_t rtc_init(void)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "I2C 匯流排還沒建立，先 board_init()");

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add dev");

    // 確認振盪器沒有被停住。冷開機時 Control_1 應該是 0x00，
    // 但這顆晶片的狀態不隨 CPU reset 清除，前一版韌體留下的設定會留著
    uint8_t ctrl1 = 0;
    ESP_RETURN_ON_ERROR(read_regs(REG_CONTROL_1, &ctrl1, 1), TAG, "ctrl1");
    if (ctrl1 & CTRL1_STOP) {
        ctrl1 &= ~CTRL1_STOP;
        ESP_RETURN_ON_ERROR(write_regs(REG_CONTROL_1, &ctrl1, 1), TAG, "放開 STOP");
        ESP_LOGW(TAG, "振盪器原本被 STOP 住，已放開");
    }
    ESP_LOGI(TAG, "PCF85063 就緒，Control_1=0x%02X", ctrl1);
    return ESP_OK;
}

esp_err_t rtc_oscillator_stopped(bool *stopped)
{
    uint8_t sec = 0;
    ESP_RETURN_ON_ERROR(read_regs(REG_SECONDS, &sec, 1), TAG, "seconds");
    *stopped = (sec & SECONDS_OS) != 0;
    return ESP_OK;
}

esp_err_t rtc_get_time(struct tm *out)
{
    uint8_t r[7] = {0};
    ESP_RETURN_ON_ERROR(read_regs(REG_SECONDS, r, sizeof(r)), TAG, "read time");

    out->tm_sec  = bcd_to_bin(r[0] & 0x7F);   // 遮掉 OS 旗標
    out->tm_min  = bcd_to_bin(r[1] & 0x7F);
    out->tm_hour = bcd_to_bin(r[2] & 0x3F);   // 24 小時制
    out->tm_mday = bcd_to_bin(r[3] & 0x3F);
    out->tm_wday = r[4] & 0x07;
    out->tm_mon  = bcd_to_bin(r[5] & 0x1F) - 1;   // 晶片 1~12，struct tm 是 0~11
    out->tm_year = bcd_to_bin(r[6]) + 100;        // 晶片 00~99 視為 2000~2099
    out->tm_yday = 0;
    out->tm_isdst = -1;
    return ESP_OK;
}

esp_err_t rtc_set_time(const struct tm *t)
{
    uint8_t r[7] = {
        bin_to_bcd(t->tm_sec) & 0x7F,   // 寫 0 到 bit7 順便清掉 OS 旗標
        bin_to_bcd(t->tm_min),
        bin_to_bcd(t->tm_hour),
        bin_to_bcd(t->tm_mday),
        (uint8_t)(t->tm_wday & 0x07),
        bin_to_bcd(t->tm_mon + 1),
        bin_to_bcd((uint8_t)(t->tm_year % 100)),
    };
    return write_regs(REG_SECONDS, r, sizeof(r));
}

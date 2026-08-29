// 音訊路徑：I2S0（ESP 當 master）→ ES8311 → TCA9554 bit7 開的功放 → 喇叭。
// 腳位取自官方 BSP：MCLK=19 BCLK=20 WS=22，ESP 的資料輸出是 23（DIN 21 是錄音用，這裡沒開）。
#include "audio.h"

#include "board.h"
#include "driver/i2s_std.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sounds/sounds.h"

#define I2S_MCLK 19
#define I2S_BCLK 20
#define I2S_WS   22
#define I2S_DOUT 23

#define OUT_VOLUME 75
// codec 音量的上限。ES8311 的刻度接近滿檔時會把訊號推進削頂區，實測最高檔破音。
// 使用者看到的 0~100 對應到 codec 的 0~VOL_CEIL，最高一格就落在還乾淨的位置——
// 與其讓使用者自己避開最後幾格，不如讓整條刻度都是可用的
#define VOL_CEIL 80
#define CHUNK_FRAMES 256

static const char *TAG = "audio";

static esp_codec_dev_handle_t s_dev;
// 全域音量，存的是**使用者刻度** 0~100。送進 codec 前才換算成 codec 刻度
static uint8_t s_master = OUT_VOLUME;
static QueueHandle_t s_queue;

// 佇列裡放的是「哪一種音、多大聲」。原本只有一種音時傳個音量就夠，
// 加了鐘之後音量單獨傳會分不出要播哪一個
typedef struct {
    bool bell;
    uint8_t volume;
} audio_req_t;
// 單聲道音源要攤成兩個 slot：esp_codec_dev 的 I2S 資料層只接受偶數聲道
static int16_t s_chunk[CHUNK_FRAMES * 2];

static esp_err_t i2s_init(i2s_chan_handle_t *tx)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   // 沒資料時送零，否則會一直重播最後一個 DMA 緩衝
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, tx, NULL), TAG, "new channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SND_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    return i2s_channel_init_std_mode(*tx, &std_cfg);
}

static void play(const audio_req_t *req)
{
    int32_t gain = req->volume > 100 ? 100 : req->volume;
    const int16_t *pcm = req->bell ? snd_bell : snd_clack;
    size_t len = req->bell ? snd_bell_len : snd_clack_len;

    for (size_t i = 0; i < len; i += CHUNK_FRAMES) {
        size_t frames = len - i;
        if (frames > CHUNK_FRAMES) frames = CHUNK_FRAMES;

        for (size_t j = 0; j < frames; j++) {
            int16_t s = (int16_t)((pcm[i + j] * gain) / 100);
            s_chunk[j * 2] = s;
            s_chunk[j * 2 + 1] = s;
        }
        if (esp_codec_dev_write(s_dev, s_chunk, frames * 2 * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "寫入 codec 失敗");
            return;
        }
    }
}

static void audio_task(void *arg)
{
    audio_req_t req;
    while (1) {
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) == pdTRUE) {
            play(&req);
        }
    }
}

esp_err_t audio_init(void)
{
    i2s_chan_handle_t tx = NULL;
    ESP_RETURN_ON_ERROR(i2s_init(&tx), TAG, "i2s");

    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .tx_handle = tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "i2s data if");

    // addr 這裡是 8 位元寫入位址，元件內部才右移成匯流排上看到的 0x18
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = board_i2c_bus(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "i2c ctrl if");

    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,          // 功放電源掛在 TCA9554，不是 GPIO，由 board_speaker_power 管
        .master_mode = false,  // ESP 出時脈，codec 當 slave
        .use_mclk = true,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&codec_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "es8311");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_dev, ESP_FAIL, TAG, "codec dev");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .sample_rate = SND_SAMPLE_RATE,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_dev, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "open");
    esp_codec_dev_set_out_vol(s_dev, s_master * VOL_CEIL / 100);

    ESP_RETURN_ON_ERROR(board_speaker_power(true), TAG, "喇叭電源");

    s_queue = xQueueCreate(4, sizeof(audio_req_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "queue");
    ESP_RETURN_ON_FALSE(xTaskCreate(audio_task, "audio", 3072, NULL, 6, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");

    ESP_LOGI(TAG, "ES8311 就緒，%d Hz", SND_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t audio_set_volume(uint8_t percent)
{
    s_master = percent > 100 ? 100 : percent;
    if (!s_dev) return ESP_OK;   // 還沒起來也記著，audio_init() 會套用
    int vol = s_master * VOL_CEIL / 100;
    return esp_codec_dev_set_out_vol(s_dev, vol) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

uint8_t audio_volume(void) { return s_master; }

static void enqueue(bool bell, uint8_t volume)
{
    if (s_queue == NULL) return;
    audio_req_t req = {.bell = bell, .volume = volume};
    xQueueSend(s_queue, &req, 0);
}

void audio_play_clack(uint8_t volume) { enqueue(false, volume); }
void audio_play_bell(uint8_t volume) { enqueue(true, volume); }

// 【已歸檔】M0/S1 實測證明本板沒有連通的 microSD 卡座，這支不進正式韌體。
// 程式碼本身正確（sdspi 掛載流程），日後若外接 SD 模組可直接取用，記得改腳位常數。
// 驗證過程見 docs/knowledge-skill/M0-S1_SD卡與SPI2分時共用驗證-001/notes.md
// 腳位取自官方 BSP：Waveshare-ESP32-components/bsp/esp32_c6_touch_amoled_1_8
// BSP 以 SDMMC 命名（CLK/CMD/DATA），走 SPI 時 CMD 是 MOSI、DATA 是 MISO
#include "sdcard.h"

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#define SD_HOST SPI2_HOST   // 與面板同一條 bus，見 sdcard.h 的說明

static const char *TAG = "sdcard";

static sdmmc_card_t *s_card;

esp_err_t sdcard_mount(void)
{
    return sdcard_mount_pins(SDCARD_PIN_CS, SDCARD_PIN_CLK, SDCARD_PIN_MOSI, SDCARD_PIN_MISO);
}

esp_err_t sdcard_mount_pins(int cs, int clk, int mosi, int miso)
{
    ESP_RETURN_ON_FALSE(!s_card, ESP_ERR_INVALID_STATE, TAG, "已掛載");

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    // sdspi 不會自己拉 MISO，板上若沒有外部上拉電阻，卡的回應會讀成浮動值。
    // 要在 spi_bus_initialize() 之後設，否則會被它的腳位配置蓋掉
    gpio_set_pull_mode(miso, GPIO_PULLUP_ONLY);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SD_HOST;
    slot.gpio_cs = cs;

    const esp_vfs_fat_mount_config_t mount_cfg = {
        // 韌體不該格式化使用者的卡；掛不起來就讓上層決定怎麼辦
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        spi_bus_free(SD_HOST);
        ESP_LOGE(TAG, "掛載失敗（%s）——卡未插好、非 FAT32、或接線不符", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
    if (!s_card) return ESP_OK;
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_card);
    s_card = NULL;
    // 卸載失敗也要放掉 bus，否則面板永遠裝不回去
    esp_err_t bus_err = spi_bus_free(SD_HOST);
    return err != ESP_OK ? err : bus_err;
}

sdmmc_card_t *sdcard_handle(void) { return s_card; }

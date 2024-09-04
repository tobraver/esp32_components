#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "board.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_wifi.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "http_stream.h"
#include "sys_arch.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "http_sdmmc.h"
#include "audio_sys.h"

const char *TAG = "main";

void wifi_connect(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    esp_netif_init();

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "Wait for wifi to connect");
    periph_wifi_cfg_t wifi_cfg = {
        .ssid = "wang",
        .password = "12345678",
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);

    // Start wifi peripheral
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
}

void app_main(void)
{
    http_sdmmc_init();

    wifi_connect();

    char* url = "http://192.168.1.121:8096/Items/29a2d4cae79863a3f454f2f3c58d2e90/Download?api_key=c64271a59d0a4e3e9acabd178f9dd427";
    char* path = "/sdcard/test.mp3";
    uint32_t start = audio_sys_get_time_ms();
    if(http_sdmmc_download(url, path) != ESP_OK) {
        ESP_LOGE(TAG, "Download file failed");
    } else {
        ESP_LOGI(TAG, "Download file success");
    }
    uint32_t end = audio_sys_get_time_ms();
    ESP_LOGI(TAG, "Download use time: %d ms", end - start);


    FILE* fd = fopen(path, "rb");
    if(fd != NULL) {
        ESP_LOGI(TAG, "file read susccess");
        struct stat file_stat;
        stat(path, &file_stat);
        printf("file size: %ld\n", file_stat.st_size);
        fclose(fd);
    } else {
        ESP_LOGE(TAG, "file read failed");
    }

    http_sdmmc_deinit();
}

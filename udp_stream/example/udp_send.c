
#include <string.h>
#include <inttypes.h>

#include "audio_common.h"
#include "board.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "eth.h"
#include "udp_stream.h"
#include "raw_stream.h"
#include "audio_pipeline.h"

const char *TAG = "main";

esp_err_t udp_stream_event_handle(udp_stream_event_msg_t *msg, udp_stream_status_t state, void *event_ctx)
{
    printf("udp event, state = %d\n", state);
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    esp_netif_init();

    eth_init();
    eth_config("192.168.1.171", "255.255.255.0", "192.168.1.254", "192.168.1.254", "192.168.1.254");
    eth_wait_connected();
    printf("eth connect success...\n");

    udp_stream_cfg_t udp_cfg = UDP_STREAM_CFG_DEFAULT();
    udp_cfg.host = "192.168.1.121";
    udp_cfg.port = 8000;
    udp_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t udp_stream = udp_stream_init(&udp_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    audio_element_handle_t raw_stream = raw_stream_init(&raw_cfg);

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);
    audio_pipeline_register(pipeline, raw_stream, "raw_stream");
    audio_pipeline_register(pipeline, udp_stream, "udp_stream");

    const char *link_tag[2] = {"raw_stream", "udp_stream"};
    audio_pipeline_link(pipeline, &link_tag[0], 2);
    audio_pipeline_run(pipeline);

    while (1) {
        raw_stream_write(raw_stream, "hello", 5);
        vTaskDelay(1);
    }
}

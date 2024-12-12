
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
    // eth_config("192.168.1.171", "255.255.255.0", "192.168.1.254", "192.168.1.254", "192.168.1.254");
    eth_config("192.168.1.172", "255.255.255.0", "192.168.1.254", "192.168.1.254", "192.168.1.254");
    eth_wait_connected();
    printf("eth connect success...\n");

    udp_stream_cfg_t udp_cfg = UDP_STREAM_CFG_DEFAULT();
    udp_cfg.host = "239.205.155.250";
    udp_cfg.port = 9999;
    udp_cfg.use_mreq = true;
    udp_cfg.type = AUDIO_STREAM_READER;
    udp_cfg.event_handler = udp_stream_event_handle;
    audio_element_handle_t udp_stream = udp_stream_init(&udp_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_cfg.out_rb_size = 1024;
    audio_element_handle_t raw_stream = raw_stream_init(&raw_cfg);
    audio_element_set_input_timeout(raw_stream, 30*1000);
    audio_element_set_output_timeout(raw_stream, 30*1000);

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);

    audio_pipeline_register(pipeline, udp_stream, "udp_stream");
    audio_pipeline_register(pipeline, raw_stream, "raw_stream");

    const char *link_tag[2] = {"udp_stream", "raw_stream"};
    audio_pipeline_link(pipeline, &link_tag[0], 2);
    audio_pipeline_run(pipeline);
    ESP_LOGI(TAG, "Start audio_pipeline");

    int index = 0;
    char buffer[100];
    audio_element_state_t state = AEL_STATE_NONE;
    while (1) {
        state = audio_element_get_state(udp_stream);
        if(state >= AEL_STATE_STOPPED) {
            printf("udp_stream exit, state = %d\n", state);
            break;
        }
        int ret = raw_stream_read(raw_stream, buffer, 100);
        if(ret > 0) {
            // printf("read %.*s bytes\n", ret, buffer);
            printf("[%d] bytes = %d\n", index++, ret);
        } else {
            printf("raw read error\n");
            break;
        }
    }
    ESP_LOGI(TAG, "Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, udp_stream);
    audio_pipeline_unregister(pipeline, raw_stream);

    audio_pipeline_deinit(pipeline);
    audio_element_deinit(udp_stream);
    audio_element_deinit(raw_stream);
}

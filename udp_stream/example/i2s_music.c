
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
#include "wav_decoder.h"
#include "mp3_decoder.h"
#include "pcm_decoder.h"
#include "i2s_stream.h"
#include "audio_pipeline.h"

const char *TAG = "main";

esp_err_t udp_stream_event_handle(udp_stream_event_msg_t *msg, udp_stream_status_t state, void *event_ctx)
{
    printf("udp event, state = %d\n", state);
    return ESP_OK;
}

#define MUSIC_TYPE  0 // 0: mp3, 1: wav, 2: pcm

void app_main(void)
{
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_enable_pa(board_handle->audio_hal, false);
    audio_hal_set_volume(board_handle->audio_hal, 50);

    udp_stream_cfg_t udp_cfg = UDP_STREAM_CFG_DEFAULT();
    udp_cfg.host = "239.205.155.251";
    udp_cfg.port = 9999;
    udp_cfg.use_mreq = true;
    udp_cfg.type = AUDIO_STREAM_READER;
    udp_cfg.timeout_ms = 3000;
    udp_cfg.out_rb_size = 50 * 1024;
    udp_cfg.event_handler = udp_stream_event_handle;
    audio_element_handle_t udp_stream = udp_stream_init(&udp_cfg);

#if MUSIC_TYPE == 0
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    audio_element_handle_t mp3_decoder = mp3_decoder_init(&mp3_cfg);
#elif MUSIC_TYPE == 1
    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    audio_element_handle_t wav_decoder = wav_decoder_init(&wav_cfg);
#elif MUSIC_TYPE == 2
    pcm_decoder_cfg_t pcm_cfg = DEFAULT_PCM_DECODER_CONFIG();
    audio_element_handle_t pcm_decoder = pcm_decoder_init(&pcm_cfg);
#endif

    // 16k mono music
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.i2s_config.sample_rate = 16000;
    i2s_cfg.i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    audio_element_handle_t i2s_stream = i2s_stream_init(&i2s_cfg);

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    audio_pipeline_register(pipeline, udp_stream, "udp_stream");
#if MUSIC_TYPE == 0
    audio_pipeline_register(pipeline, mp3_decoder, "mp3_decoder");
#elif MUSIC_TYPE == 1
    audio_pipeline_register(pipeline, wav_decoder, "wav_decoder");
#elif MUSIC_TYPE == 2
    audio_pipeline_register(pipeline, pcm_decoder, "pcm_decoder");
#endif
    audio_pipeline_register(pipeline, i2s_stream, "i2s_stream");

#if MUSIC_TYPE == 0
    const char *link_tag[3] = {"udp_stream", "mp3_decoder", "i2s_stream"};
#elif MUSIC_TYPE == 1
    const char *link_tag[3] = {"udp_stream", "wav_decoder", "i2s_stream"};
#elif MUSIC_TYPE == 2
    const char *link_tag[3] = {"udp_stream", "pcm_decoder", "i2s_stream"};
#endif
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    audio_pipeline_run(pipeline);
    ESP_LOGI(TAG, "Start audio_pipeline");
    audio_hal_enable_pa(board_handle->audio_hal, true);

    audio_element_state_t state = AEL_STATE_NONE;
    while (1) {
        state = audio_element_get_state(i2s_stream);
        if(state >= AEL_STATE_STOPPED) {
            printf("i2s_stream exit, state = %d\n", state);
            audio_hal_enable_pa(board_handle->audio_hal, false);
            break;
        }
        state = audio_element_get_state(udp_stream);
        if(state >= AEL_STATE_ERROR) {
            printf("udp_stream exit, state = %d\n", state);
            audio_hal_enable_pa(board_handle->audio_hal, false);
            break;
        }
        vTaskDelay(1);
    }

    ESP_LOGI(TAG, "Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, udp_stream);
#if MUSIC_TYPE == 0
    audio_pipeline_unregister(pipeline, mp3_decoder);
#elif MUSIC_TYPE == 1
    audio_pipeline_unregister(pipeline, wav_decoder);
#elif MUSIC_TYPE == 2
    audio_pipeline_unregister(pipeline, pcm_decoder);
#endif
    audio_pipeline_unregister(pipeline, i2s_stream);

    audio_pipeline_deinit(pipeline);
    audio_element_deinit(udp_stream);
#if MUSIC_TYPE == 0
    audio_element_deinit(mp3_decoder);
#elif MUSIC_TYPE == 1
    audio_element_deinit(wav_decoder);
#elif MUSIC_TYPE == 2
    audio_element_deinit(pcm_decoder);
#endif
    audio_element_deinit(i2s_stream);
}

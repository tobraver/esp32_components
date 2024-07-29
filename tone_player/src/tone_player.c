#include "tone_player.h"
#include "audio_tone_uri.h"

#include "esp_log.h"

#include "audio_element.h"
#include "audio_idf_version.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_thread.h"
#include "board.h"
#include "esp_audio.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "tone_stream.h"

static const char* TAG = "tone_player";

typedef struct {
    esp_audio_handle_t hd;
    uint8_t is_init;
} tone_player_desc_t;

static tone_player_desc_t s_player_desc;

static esp_audio_handle_t tone_player_setup_player(void)
{
    esp_audio_cfg_t cfg = DEFAULT_ESP_AUDIO_CONFIG();
    audio_board_handle_t board_handle = audio_board_init();
    if(board_handle == NULL) {
        ESP_LOGE(TAG, "board init failed");
        return NULL;
    }

    cfg.vol_handle = board_handle->audio_hal;
    cfg.vol_set = (audio_volume_set)audio_hal_set_volume;
    cfg.vol_get = (audio_volume_get)audio_hal_get_volume;
    cfg.resample_rate = 48000;
    cfg.prefer_type = ESP_AUDIO_PREFER_MEM;

    esp_audio_handle_t player = esp_audio_create(&cfg);
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    // Create readers and add to esp_audio
    tone_stream_cfg_t tone_cfg = TONE_STREAM_CFG_DEFAULT();
    tone_cfg.type = AUDIO_STREAM_READER;
    tone_cfg.task_prio = 18;
    esp_audio_input_stream_add(player, tone_stream_init(&tone_cfg));

    // Add decoders and encoders to esp_audio
    mp3_decoder_cfg_t mp3_dec_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_dec_cfg.task_prio = 19;
    esp_audio_codec_lib_add(player, AUDIO_CODEC_TYPE_DECODER, mp3_decoder_init(&mp3_dec_cfg));

    // Create writers and add to esp_audio
    i2s_stream_cfg_t i2s_writer = I2S_STREAM_CFG_DEFAULT();
    i2s_writer.i2s_config.sample_rate = 48000;
#if (CONFIG_ESP32_S3_KORVO2_V3_BOARD == 1) && (CONFIG_AFE_MIC_NUM == 1)
    i2s_writer.i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
#else
    i2s_writer.i2s_config.bits_per_sample = CODEC_ADC_BITS_PER_SAMPLE;
    i2s_writer.need_expand = (CODEC_ADC_BITS_PER_SAMPLE != I2S_BITS_PER_SAMPLE_16BIT);
#endif
    i2s_writer.type = AUDIO_STREAM_WRITER;
    esp_audio_output_stream_add(player, i2s_stream_init(&i2s_writer));

    // Set default volume
    esp_audio_vol_set(player, TONE_PLAYER_DEF_VOLUME);
    AUDIO_MEM_SHOW(TAG);

    ESP_LOGI(TAG, "esp_audio instance is:%p\r\n", player);
    return player;
}

bool tone_player_init(void)
{
    if(get_tone_uri_num() != TONE_TYPE_MAX) {
        ESP_LOGE(TAG, "please remake your tone file, see readme.md");
        return false;
    }

    if(s_player_desc.is_init) {
        ESP_LOGW(TAG, "tone player is already init");
        return true;
    }

    s_player_desc.hd = tone_player_setup_player();
    if(s_player_desc.hd) {
        ESP_LOGE(TAG, "tone player setup failed");
        return false;
    }

    s_player_desc.is_init = true;
    ESP_LOGI(TAG, "tone player init success");
    return true;
}

bool tone_player_deinit(void)
{
    if(!s_player_desc.is_init) {
        ESP_LOGW(TAG, "tone player is not init");
        return false;
    }

    if(s_player_desc.hd) {
        esp_audio_destroy(s_player_desc.hd);
        ESP_LOGI(TAG, "tone player destory success");
        s_player_desc.hd = NULL;
    }

    s_player_desc.is_init = false; 
    return true;
}

// block until play finish
bool tone_player_sync_play(tone_type_t type)
{
    if(type >= TONE_TYPE_MAX) {
        ESP_LOGE(TAG, "tone type is invalid");
        return false;
    }

    if(s_player_desc.hd == NULL) {
        ESP_LOGE(TAG, "tone player is not init");
        return false;
    }

    extern const char* tone_uri[];
    return esp_audio_sync_play(s_player_desc.hd, tone_uri[type], 0) == ESP_OK ? true : false;
}

// not block
bool tone_player_async_play(tone_type_t type)
{
    if(type >= TONE_TYPE_MAX) {
        ESP_LOGE(TAG, "tone type is invalid");
        return false;
    }

    if(s_player_desc.hd == NULL) {
        ESP_LOGE(TAG, "tone player is not init");
        return false;
    }

    extern const char* tone_uri[];
    return esp_audio_play(s_player_desc.hd, AUDIO_CODEC_TYPE_DECODER, tone_uri[type], 0) == ESP_OK ? true : false;
}

bool tone_player_stop(bool is_wait)
{
    if(s_player_desc.hd == NULL) {
        ESP_LOGE(TAG, "tone player is not init");
        return false;
    }

    return esp_audio_stop(s_player_desc.hd, is_wait ? TERMINATION_TYPE_DONE : TERMINATION_TYPE_NOW) == ESP_OK ? true : false;
}

bool tone_player_is_playing(void)
{
    if(s_player_desc.hd == NULL) {
        ESP_LOGE(TAG, "tone player is not init");
        return true;
    }

    esp_audio_state_t state;
    if(esp_audio_state_get(s_player_desc.hd, &state) != ESP_OK) {
        ESP_LOGE(TAG, "get audio state failed");
        return true;
    }

    if(state.status == AUDIO_STATUS_RUNNING) {
        return false;
    }
    return true;
}

bool tone_player_set_volume(int32_t volume)
{
    if(s_player_desc.hd == NULL) {
        ESP_LOGE(TAG, "tone player is not init");
        return false;
    }

    volume = volume > 100 ? 100 : volume;
    volume = volume < 0 ? 0 : volume;
    return esp_audio_vol_set(s_player_desc.hd, volume) == ESP_OK ? true : false;
}

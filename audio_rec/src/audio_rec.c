#include "audio_rec.h"
#include "string.h"


#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "amrnb_encoder.h"
#include "amrwb_encoder.h"
#include "audio_element.h"
#include "audio_idf_version.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "board.h"
#include "esp_audio.h"
#include "filter_resample.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "wav_decoder.h"
#include "pcm_decoder.h"
#include "periph_adc_button.h"
#include "raw_stream.h"
#include "recorder_encoder.h"
#include "recorder_sr.h"
#include "tone_stream.h"
#include "es7210.h"

#include "model_path.h"

#define NO_ENCODER  (0)
#define ENC_2_AMRNB (1)
#define ENC_2_AMRWB (2)

/**
 * @brief audio s_rec_desc.recorder encoder config
 */
#define AUDIO_REC_ENC_ENABLE (NO_ENCODER)

/**
 * @brief audio s_rec_desc.recorder wakenet config
 */
#define AUDIO_REC_WAKENET_ENABLE (true)

/**
 * @brief audio s_rec_desc.recorder multinet config
 */
#define AUDIO_REC_MULTINET_ENABLE (true)

/**
 * @brief audio s_rec_desc.recorder speech commands of default will be clear 
 */
#define AUDIO_REC_SPEECH_CMDS_RESET (true)

/**
 * @brief audio s_rec_desc.recorder user speech commands
 * 
 */
#define AUDIO_REC_SPEECH_COMMANDS     ("da kai dian deng,kai dian deng;guan bi dian deng,guan dian deng;guan deng;")

/**
 * @brief check audio board config
 */
#ifndef CODEC_ADC_SAMPLE_RATE
#warning "Please define CODEC_ADC_SAMPLE_RATE first, default value is 48kHz may not correctly"
#define CODEC_ADC_SAMPLE_RATE    48000
#endif

#ifndef CODEC_ADC_BITS_PER_SAMPLE
#warning "Please define CODEC_ADC_BITS_PER_SAMPLE first, default value 16 bits may not correctly"
#define CODEC_ADC_BITS_PER_SAMPLE  I2S_BITS_PER_SAMPLE_16BIT
#endif

#ifndef RECORD_HARDWARE_AEC
#warning "The hardware AEC is disabled!"
#define RECORD_HARDWARE_AEC  (false)
#endif

#ifndef CODEC_ADC_I2S_PORT
#define CODEC_ADC_I2S_PORT  (0)
#endif

enum _rec_voice_ctrl_t {
    REC_VOICE_START = 1,
    REC_VOICE_STOP,
    REC_VOICE_CANCEL,
    REC_VOICE_EXIT,
};

static char *TAG = "m_audio_rec";

typedef struct {
    bool                    is_init;
    audio_pipeline_handle_t player;
    audio_rec_handle_t      recorder;
    audio_element_handle_t  raw_read;
    audio_element_handle_t  raw_write;
    ringbuf_handle_t        raw_write_cache;
    QueueHandle_t           voice_ctrl;
    bool                    voice_exit;
    bool                    voice_reading;
    char*                   command_word;
    audio_rec_player_type_t player_type;
    audio_rec_event_cb_t    event_cb;
} audio_rec_desc_t;

/**
 * @brief audio recorder desc 
 */
static audio_rec_desc_t s_rec_desc;

static audio_pipeline_handle_t audio_rec_setup_player(void)
{
    if(s_rec_desc.player) {
        ESP_LOGW(TAG, "## audio rec player is already setup");
        return s_rec_desc.player;
    }

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_rec_desc.player = audio_pipeline_init(&pipeline_cfg);
    if(s_rec_desc.player == NULL) {
        ESP_LOGE(TAG, "## audio rec player init failed");
        return NULL;
    }

    // Create readers and add to esp_audio
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    s_rec_desc.raw_write = raw_stream_init(&raw_cfg);

    // Add decoders and encoders to esp_audio
    audio_element_handle_t raw_decoder = NULL;
    if(s_rec_desc.player_type == AUDIO_REC_PLAYER_TYPE_WAV) {
        wav_decoder_cfg_t wav_dec_cfg = DEFAULT_WAV_DECODER_CONFIG();
        wav_dec_cfg.task_core = 1;
        wav_dec_cfg.task_prio = 19;
        raw_decoder = wav_decoder_init(&wav_dec_cfg);
    } else if(s_rec_desc.player_type == AUDIO_REC_PLAYER_TYPE_MP3) {
        mp3_decoder_cfg_t mp3_dec_cfg = DEFAULT_MP3_DECODER_CONFIG();
        mp3_dec_cfg.task_core = 1;
        mp3_dec_cfg.task_prio = 19;
        raw_decoder = mp3_decoder_init(&mp3_dec_cfg);
    } else { // default is pcm
        pcm_decoder_cfg_t pcm_dec_cfg = DEFAULT_PCM_DECODER_CONFIG();
        pcm_dec_cfg.task_core = 1;
        pcm_dec_cfg.task_prio = 19;
        raw_decoder = pcm_decoder_init(&pcm_dec_cfg);
    }

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
    i2s_writer.uninstall_drv = false;
    audio_element_handle_t i2s_stream_writer = i2s_stream_init(&i2s_writer);

    audio_pipeline_register(s_rec_desc.player, s_rec_desc.raw_write, "raw_write");
    audio_pipeline_register(s_rec_desc.player, raw_decoder, "raw_decoder");
    audio_pipeline_register(s_rec_desc.player, i2s_stream_writer, "i2s_writer");

    const char *link_tag[3] = {"raw_write", "raw_decoder", "i2s_writer"};
    audio_pipeline_link(s_rec_desc.player, &link_tag[0], 3);
    audio_pipeline_run(s_rec_desc.player);
    return s_rec_desc.player;
}

static esp_err_t audio_rec_setdown_player(void)
{
    if (s_rec_desc.player == NULL)
    {
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "## audio rec player setdown");
    audio_pipeline_handle_t player = s_rec_desc.player;
    audio_rec_desc_lock();
    s_rec_desc.player = NULL;
    s_rec_desc.raw_write = NULL;
    audio_rec_desc_unlock();
    audio_pipeline_stop(player);
    audio_pipeline_wait_for_stop(player);
    audio_pipeline_terminate(player);
    audio_element_handle_t raw_write = audio_pipeline_get_el_by_tag(player, "raw_write");
    audio_element_handle_t player_dec = audio_pipeline_get_el_by_tag(player, "player_dec");
    audio_element_handle_t i2s_writer = audio_pipeline_get_el_by_tag(player, "i2s_writer");
    audio_pipeline_unregister(player, raw_write);
    audio_pipeline_unregister(player, player_dec);
    audio_pipeline_unregister(player, i2s_writer);
    audio_pipeline_deinit(player);
    audio_element_deinit(raw_write);
    audio_element_deinit(player_dec);
    audio_element_deinit(i2s_writer);
    return ESP_OK;
}

static void audio_rec_voice_read_task(void *args)
{
    const int buf_len = 2 * 1024;
    uint8_t *voiceData = audio_calloc(1, buf_len);
    int msg = 0;
    TickType_t delay = portMAX_DELAY;
    s_rec_desc.voice_exit = false;

    while (!s_rec_desc.voice_exit) {
        if (xQueueReceive(s_rec_desc.voice_ctrl, &msg, delay) == pdTRUE) {
            switch (msg) {
                case REC_VOICE_START: {
                    ESP_LOGW(TAG, "## voice read begin");
                    delay = 0;
                    s_rec_desc.voice_reading = true;
                    break;
                }
                case REC_VOICE_STOP: {
                    ESP_LOGW(TAG, "## voice read stopped");
                    delay = portMAX_DELAY;
                    s_rec_desc.voice_reading = false;
                    break;
                }
                case REC_VOICE_CANCEL: {
                    ESP_LOGW(TAG, "## voice read cancel");
                    delay = portMAX_DELAY;
                    s_rec_desc.voice_reading = false;
                    break;
                }
                case REC_VOICE_EXIT: {
                    ESP_LOGW(TAG, "## voice read exit");
                    delay = portMAX_DELAY;
                    s_rec_desc.voice_reading = false;
                    s_rec_desc.voice_exit = true;
                    break;
                }
                default:
                    break;
            }
        }

        if (s_rec_desc.voice_reading) {
            int ret = audio_recorder_data_read(s_rec_desc.recorder, voiceData, buf_len, portMAX_DELAY);
            if (ret <= 0) {
                ESP_LOGW(TAG, "audio recorder read finished %d", ret);
                delay = portMAX_DELAY;
                s_rec_desc.voice_reading = false;
            } else {
                if(s_rec_desc.event_cb) {
                    s_rec_desc.event_cb(AUDIO_REC_SPEAKING, voiceData, ret);
                }
            }
        }
    }

    free(voiceData);
    vTaskDelete(NULL);
}

static void audio_rec_raw_cache_task(void *args)
{
    ESP_LOGI(TAG, "## audio rec raw cache task start");

    int cache_len = AUDIO_REC_PLAYER_CACHE_SIZE / 10;
    uint8_t* cache_buff = audio_calloc(1, cache_len);

    while (!s_rec_desc.voice_exit) {
        if(s_rec_desc.raw_write_cache == NULL) {
            ESP_LOGE(TAG, "## raw write cache is null");
            break;
        }
        int len = rb_read(s_rec_desc.raw_write_cache, (char*)cache_buff, cache_len, pdTICKS_TO_MS(100));
        if(len < 0) {
            ESP_LOGE(TAG, "## raw write cache read failed");
            break;
        }
        if(s_rec_desc.raw_write) {
            if(raw_stream_write(s_rec_desc.raw_write, (char*)cache_buff, len) < 0) {
                ESP_LOGE(TAG, "## raw write failed");
            }
        }
    }
    ESP_LOGI(TAG, "## audio rec raw cache task end");
    free(cache_buff);
    vTaskDelete(NULL);
}

static esp_err_t audio_rec_engine_event_cb(audio_rec_evt_t type, void *user_data)
{
    if (AUDIO_REC_WAKEUP_START == type) {
        ESP_LOGI(TAG, "@@ REC_EVENT_WAKEUP_START");
        if (s_rec_desc.voice_reading) {
            int msg = REC_VOICE_CANCEL;
            if (xQueueSend(s_rec_desc.voice_ctrl, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "## rec cancel send failed");
            }
        }
        if(s_rec_desc.event_cb) {
            s_rec_desc.event_cb(AUDIO_REC_SPEAK_START, NULL, 0);
        }
    } else if (AUDIO_REC_VAD_START == type) {
        ESP_LOGI(TAG, "@@ REC_EVENT_VAD_START");
        if (!s_rec_desc.voice_reading) {
            int msg = REC_VOICE_START;
            if (xQueueSend(s_rec_desc.voice_ctrl, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "## rec start send failed");
            }
        }
        if(s_rec_desc.event_cb) {
            s_rec_desc.event_cb(AUDIO_REC_SPEAK_START, NULL, 0);
        }
    } else if (AUDIO_REC_VAD_END == type) {
        ESP_LOGI(TAG, "@@ REC_EVENT_VAD_STOP");
        if (s_rec_desc.voice_reading) {
            int msg = REC_VOICE_STOP;
            if (xQueueSend(s_rec_desc.voice_ctrl, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "## rec stop send failed");
            }
        }
        if(s_rec_desc.event_cb) {
            s_rec_desc.event_cb(AUDIO_REC_SPEAK_END, NULL, 0);
        }
    } else if (AUDIO_REC_WAKEUP_END == type) {
        ESP_LOGI(TAG, "@@ REC_EVENT_WAKEUP_END");
        if(s_rec_desc.event_cb) {
            s_rec_desc.event_cb(AUDIO_REC_SLEEP, NULL, 0);
        }
    } else if (AUDIO_REC_COMMAND_DECT <= type) {
        int cmd_word = type;
        ESP_LOGI(TAG, "@@ AUDIO_REC_COMMAND_DECT");
        ESP_LOGW(TAG, "## command %d", cmd_word);
        if(s_rec_desc.event_cb) {
            s_rec_desc.event_cb(AUDIO_REC_SPEAK_WORD, &cmd_word, sizeof(cmd_word));
        }
    } else {
        ESP_LOGE(TAG, "## Unkown event");
    }
    return ESP_OK;
}

/**
 * @brief audio recorder set afe data from i2s stream
 */
static int audio_rec_input_cb_for_afe(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    return raw_stream_read(s_rec_desc.raw_read, (char *)buffer, buf_sz);
}

static audio_rec_handle_t audio_rec_start_recorder(void)
{
    audio_element_handle_t i2s_stream_reader;
    audio_pipeline_handle_t pipeline;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    if (NULL == pipeline) {
        ESP_LOGE(TAG, "audio recorder init failed");
        return NULL;
    }

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_port = CODEC_ADC_I2S_PORT;
    i2s_cfg.i2s_config.use_apll = 0;
    i2s_cfg.i2s_config.sample_rate = CODEC_ADC_SAMPLE_RATE;
#if (CONFIG_ESP32_S3_KORVO2_V3_BOARD == 1) && (CONFIG_AFE_MIC_NUM == 1)
    i2s_cfg.i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
#else
    i2s_cfg.i2s_config.bits_per_sample = CODEC_ADC_BITS_PER_SAMPLE;
#endif
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    audio_element_handle_t filter = NULL;
#if CODEC_ADC_SAMPLE_RATE != (16000)
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = CODEC_ADC_SAMPLE_RATE;
    rsp_cfg.dest_rate = 16000;
    rsp_cfg.task_prio = 18;
#if (CONFIG_ESP32_S3_KORVO2_V3_BOARD == 1) && (CONFIG_AFE_MIC_NUM == 2)
    rsp_cfg.mode = RESAMPLE_UNCROSS_MODE;
    rsp_cfg.src_ch = 4;
    rsp_cfg.dest_ch = 4;
    rsp_cfg.max_indata_bytes = 1024;
#endif
    filter = rsp_filter_init(&rsp_cfg);
#endif

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    s_rec_desc.raw_read = raw_stream_init(&raw_cfg);

    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline, s_rec_desc.raw_read, "raw");

    if (filter) {
        audio_pipeline_register(pipeline, filter, "filter");
        const char *link_tag[3] = {"i2s", "filter", "raw"};
        audio_pipeline_link(pipeline, &link_tag[0], 3);
    } else {
        const char *link_tag[2] = {"i2s", "raw"};
        audio_pipeline_link(pipeline, &link_tag[0], 2);
    }

    audio_pipeline_run(pipeline);
    ESP_LOGI(TAG, "Recorder has been created");

    recorder_sr_cfg_t recorder_sr_cfg = DEFAULT_RECORDER_SR_CFG();
    recorder_sr_cfg.afe_cfg.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    recorder_sr_cfg.afe_cfg.wakenet_init = AUDIO_REC_WAKENET_ENABLE;
    recorder_sr_cfg.multinet_init = AUDIO_REC_MULTINET_ENABLE;
    recorder_sr_cfg.afe_cfg.aec_init = RECORD_HARDWARE_AEC;
    recorder_sr_cfg.afe_cfg.agc_mode = AFE_MN_PEAK_NO_AGC;
    recorder_sr_cfg.afe_cfg.afe_perferred_priority = 17;
    recorder_sr_cfg.feed_task_prio = 18;
    recorder_sr_cfg.fetch_task_prio = 19;
#if (CONFIG_ESP32_S3_KORVO2_V3_BOARD == 1) && (CONFIG_AFE_MIC_NUM == 1)
    recorder_sr_cfg.afe_cfg.pcm_config.mic_num = 1;
    recorder_sr_cfg.afe_cfg.pcm_config.ref_num = 1;
    recorder_sr_cfg.afe_cfg.pcm_config.total_ch_num = 2;
    recorder_sr_cfg.input_order[0] = DAT_CH_0;
    recorder_sr_cfg.input_order[1] = DAT_CH_1;

    es7210_mic_select(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC3);
#endif

#if AUDIO_REC_ENC_ENABLE
    recorder_encoder_cfg_t recorder_encoder_cfg = { 0 };
#if AUDIO_REC_ENC_ENABLE == ENC_2_AMRNB
    rsp_filter_cfg_t filter_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    filter_cfg.src_ch = 1;
    filter_cfg.src_rate = 16000;
    filter_cfg.dest_ch = 1;
    filter_cfg.dest_rate = 8000;
    filter_cfg.stack_in_ext = true;
    filter_cfg.max_indata_bytes = 1024;

    amrnb_encoder_cfg_t amrnb_cfg = DEFAULT_AMRNB_ENCODER_CONFIG();
    amrnb_cfg.contain_amrnb_header = true;
    amrnb_cfg.stack_in_ext = true;

    recorder_encoder_cfg.resample = rsp_filter_init(&filter_cfg);
    recorder_encoder_cfg.encoder = amrnb_encoder_init(&amrnb_cfg);
#elif AUDIO_REC_ENC_ENABLE == ENC_2_AMRWB
    amrwb_encoder_cfg_t amrwb_cfg = DEFAULT_AMRWB_ENCODER_CONFIG();
    amrwb_cfg.contain_amrwb_header = true;
    amrwb_cfg.stack_in_ext = true;
    amrwb_cfg.out_rb_size = 4 * 1024;

    recorder_encoder_cfg.encoder = amrwb_encoder_init(&amrwb_cfg);
#endif
#endif

    audio_rec_cfg_t cfg = AUDIO_RECORDER_DEFAULT_CFG();
    cfg.read = (recorder_data_read_t)&audio_rec_input_cb_for_afe;
    cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &cfg.sr_iface);
#if AUDIO_REC_SPEECH_CMDS_RESET
    if(s_rec_desc.command_word == NULL) {
        ESP_LOGW(TAG, "## command word is empty");
    } else {
        char err[200];
        recorder_sr_reset_speech_cmd(cfg.sr_handle, s_rec_desc.command_word, err);
    }
#endif
#if AUDIO_REC_ENC_ENABLE
    cfg.encoder_handle = recorder_encoder_create(&recorder_encoder_cfg, &cfg.encoder_iface);
#endif
    cfg.event_cb = audio_rec_engine_event_cb;
    cfg.wakeup_time = AUDIO_REC_WAKEUP_TIMEOUT;
    cfg.vad_start = AUDIO_REC_VAD_SPEAK_TIME;
    cfg.vad_off = AUDIO_REC_VAD_SILENCE_TIME;
    cfg.wakeup_end = AUDIO_REC_WAKEUP_TIMEOUT;
    s_rec_desc.recorder = audio_recorder_create(&cfg);
    return s_rec_desc.recorder;
}

/**
 * @brief audio recorder init
 * 
 * @param cmd_word[in] : format:"da kai dian deng,kai dian deng;guan bi dian deng,guan dian deng;guan deng;"
 */
bool audio_rec_init(audio_rec_conf_t conf)
{
    if(s_rec_desc.is_init) {
        ESP_LOGW(TAG, "## audio rec is already init");
        return true;
    }

    memset(&s_rec_desc, 0, sizeof(s_rec_desc));
    s_rec_desc.player_type = conf.player_type;
    s_rec_desc.event_cb = conf.event_cb;

    char* cmd_word = conf.cmd_word;
    if(cmd_word && strlen(cmd_word)) {
        s_rec_desc.command_word = audio_calloc(1, strlen(cmd_word) + 1);
        if(s_rec_desc.command_word == NULL) {
            ESP_LOGE(TAG, "## malloc command word memory failed!");
            return false;
        } else {
            memcpy(s_rec_desc.command_word, cmd_word, strlen(cmd_word));
        }
    } 

    if(audio_board_init() == NULL) {
        ESP_LOGE(TAG, "## board init faile, please check your board!");
        return false;
    }
    audio_board_handle_t board_hd = audio_board_get_handle();
    board_hd->audio_hal->audio_codec_ctrl(AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    board_hd->audio_hal->audio_codec_set_volume(AUDIO_REC_PLAYER_DEF_VOLUME);

    if(audio_rec_setup_player() == NULL) {
        ESP_LOGE(TAG, "## setup player failed!");
        return false;
    }

    if(audio_rec_start_recorder() == NULL) {
        ESP_LOGE(TAG, "## start recorder failed!");
        return false;
    }

    if(s_rec_desc.raw_write_cache == NULL) {
        s_rec_desc.raw_write_cache = rb_create(AUDIO_REC_PLAYER_CACHE_SIZE, 1);
        ESP_LOGI(TAG, "## create raw write cache!");
    }
    if(s_rec_desc.raw_write_cache == NULL) {
        ESP_LOGE(TAG, "## create raw write cache failed!");
        return false;
    }

    s_rec_desc.voice_ctrl = xQueueCreate(3, sizeof(int));
    if(s_rec_desc.voice_ctrl == NULL) {
        ESP_LOGE(TAG, "## create voice ctrl queue failed!");
        return false;
    }

    audio_thread_create(NULL, "read_task", audio_rec_voice_read_task, NULL, 4 * 1024, 5, true, 0);
    audio_thread_create(NULL, "raw_cache", audio_rec_raw_cache_task, NULL, 4 * 1024, 21, true, 0);
    s_rec_desc.is_init = true;
    return true;
}

bool audio_rec_deinit(void)
{
    if(!s_rec_desc.is_init) {
        ESP_LOGW(TAG, "## audio rec is not init");
        return true;
    }

    if(s_rec_desc.player != NULL) {
        ESP_LOGW(TAG, "## destory player");
        audio_pipeline_stop(s_rec_desc.player);
        audio_pipeline_wait_for_stop(s_rec_desc.player);
        audio_pipeline_deinit(s_rec_desc.player);
        s_rec_desc.player = NULL;
    }

    if(s_rec_desc.recorder != NULL) {
        ESP_LOGW(TAG, "## destory recorder");
        audio_recorder_destroy(s_rec_desc.recorder);
        s_rec_desc.recorder = NULL;
    }

    int msg = REC_VOICE_EXIT;
    if (xQueueSend(s_rec_desc.voice_ctrl, &msg, 0) != pdPASS) {
        ESP_LOGE(TAG, "## rec exit send failed");
    } else {
        while (1) {
            if(s_rec_desc.voice_exit == true) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "## wait for voice task exit");
        }
    }    

    if(s_rec_desc.voice_ctrl) {
        ESP_LOGW(TAG, "## destory voice ctrl queue");
        vQueueDelete(s_rec_desc.voice_ctrl);
        s_rec_desc.voice_ctrl = NULL;
    }
    
    if(s_rec_desc.raw_write_cache != NULL) {
        ESP_LOGW(TAG, "## destory raw write cache");
        rb_destroy(s_rec_desc.raw_write_cache);
        s_rec_desc.raw_write_cache = NULL;
    }

    if(s_rec_desc.command_word) {
        ESP_LOGW(TAG, "## free command word memory");
        free(s_rec_desc.command_word);
        s_rec_desc.command_word = NULL;
    }

    audio_board_handle_t board_hd = audio_board_get_handle();
    if(board_hd) {
        ESP_LOGW(TAG, "## audio board deinit");
        audio_board_deinit(board_hd);
    }

    return true;
}

bool audio_rec_install_player(void)
{
    return audio_rec_setup_player() != NULL ? true : false;
}

bool audio_rec_unstall_player(void)
{
    return audio_rec_setdown_player() == ESP_OK ? true : false;
}

bool audio_rec_set_volume(int volume)
{
    if(!s_rec_desc.is_init) {
        ESP_LOGW(TAG, "## audio rec is not init");
        return true;
    }
    volume = volume > 100 ? 100 : volume;
    volume = volume < 0 ? 0 : volume;

    audio_board_handle_t board_hd = audio_board_get_handle();
    if(board_hd == NULL) {
        ESP_LOGE(TAG, "## audio board is not init");
        return false;
    }
    return board_hd->audio_hal->audio_codec_set_volume(volume) == ESP_OK ? true : false;
}

bool audio_rec_play(void* src, int len)
{
    if(!s_rec_desc.is_init) {
        ESP_LOGW(TAG, "## audio rec is not init");
        return true;
    }

    if(s_rec_desc.player == NULL) {
        ESP_LOGW(TAG, "## audio rec player is not init");
        return true;
    }

    if(s_rec_desc.raw_write == NULL) {
        ESP_LOGW(TAG, "## audio rec raw is not init");
        return true;
    }

    if(s_rec_desc.raw_write_cache == NULL) {
        ESP_LOGW(TAG, "## audio rec raw write cache is not init");
        return true;
    }
    // return raw_stream_write(s_rec_desc.raw_write, src, len) < 0 ? false : true;
    return rb_write(s_rec_desc.raw_write_cache, src, len, portMAX_DELAY) < 0 ? false : true;
}

bool audio_rec_enter_sleep(void)
{
    if(!s_rec_desc.is_init) {
        ESP_LOGW(TAG, "## audio rec is not init");
        return false;
    }

    if(s_rec_desc.recorder == NULL) {
        ESP_LOGW(TAG, "## audio rec is not null");
        return false;
    }

    return audio_recorder_trigger_stop(s_rec_desc.recorder) == ESP_OK ? true : false; // vad must enable!
}


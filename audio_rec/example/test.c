#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_event.h"
#include "string.h"
#include "prefs.h"
#include "sys.h"
#include "audio_thread.h"
#include "audio_sys.h"
#include "esp_system.h"
#include "audio_rec.h"

void audio_rec_event_callback(audio_rec_event_t event, void* src, int len)
{
    switch (event)
    {
    case AUDIO_REC_WAKEUP:{
        printf("wake up\n");
    }break;
    case AUDIO_REC_SPEAK_START:{
        printf("speak start\n");
    }break;
    case AUDIO_REC_SPEAKING:{
        printf("speaking, len: %d\n", len);
        // you can send data to ws server, ws_send(src, len);
    }break;
    case AUDIO_REC_SPEAK_WORD:{
        int word = *((int *)src);
        printf("speak word, %d\n", word);
        // you can handle different word
    }break;
    case AUDIO_REC_SPEAK_END:{
        printf("speak end\n");
        // you can send silence to ws server, such as: 512 bytes zero
    }break;
    case AUDIO_REC_SLEEP:{
        printf("sleep\n");
    }break;
    default:
        break;
    }
}

void app_main(void)
{
    audio_rec_conf_t conf;
    conf.cmd_word = "da kai feng shan;guan bi feng shan;";
    conf.player_type = AUDIO_REC_PLAYER_TYPE_MP3;
    conf.event_cb = audio_rec_event_callback;
    audio_rec_init(conf);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    while(1){
        ESP_LOGI("", "[APP] Free memory: %ld bytes,esp_get_free_internal_heap_size :%ld bytes,esp_get_minimum_free_heap_size:%ld bytes", esp_get_free_heap_size(),esp_get_free_internal_heap_size(),esp_get_minimum_free_heap_size());
        audio_sys_get_real_time_stats();
        vTaskDelay(10 * 1000 / portTICK_PERIOD_MS);
    }
}

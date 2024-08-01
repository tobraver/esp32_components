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
#include "user_conf.h"
#include "wifi_sta.h"
#include "mqtt_tcp_client.h"

#include "http_ota.h"

bool http_ota_user_prepare_cb(void)
{
    // you can init some source, handshake, etc.
    return true;
}

bool http_ota_user_need_upgrade_cb(void)
{
    // you can check user device is need upgrade or not
    return true;
}

bool http_ota_user_upgrade_pkt_cb(uint8_t* buff, uint32_t len)
{
    // you can send data to device by one time or many times
    // this funciton will be called until no packet left.
    printf("len: %ld\n", len);
    return true;
}

bool http_ota_user_finished_check_cb(void)
{
    // you can check user device upgrade success.
    return true;
}

void app_main(void)
{
    user_conf_init();

    wifi_sta_start("esp32", "12345678");
    while (1)
    {
        printf("connecing...\n");
        if(wifi_sta_is_connected()) {
            break;
        }
        vTaskDelay(1000);
    }
    // wifi_sta_wait_connected(10*1000);
    printf("wifi connected, ssid: %s, password: %s, ip: %s\n", 
            wifi_sta_get_ssid(), wifi_sta_get_password(), wifi_sta_get_ip_str());

    http_ota_init();
    http_ota_set_url(HTTP_OTA_UPDATE_TYPE_MUSIC, "http://note-imgs-bed.oss-cn-beijing.aliyuncs.com/tmp/audio_tone.bin");
    http_ota_set_url(HTTP_OTA_UPDATE_TYPE_MODEL, "http://note-imgs-bed.oss-cn-beijing.aliyuncs.com/tmp/srmodels.bin");
    http_ota_set_url(HTTP_OTA_UPDATE_TYPE_FIRMWARE, "http://note-imgs-bed.oss-cn-beijing.aliyuncs.com/tmp/hello_world.bin");
    http_ota_set_url(HTTP_OTA_UPDATE_TYPE_USER_1, "http://note-imgs-bed.oss-cn-beijing.aliyuncs.com/tmp/audio_tone.bin");
    http_ota_set_prepare_cb(HTTP_OTA_UPDATE_TYPE_USER_1, http_ota_user_prepare_cb);
    http_ota_set_need_upgrade_cb(HTTP_OTA_UPDATE_TYPE_USER_1, http_ota_user_need_upgrade_cb);
    http_ota_set_upgrade_pkt_cb(HTTP_OTA_UPDATE_TYPE_USER_1, http_ota_user_upgrade_pkt_cb);
    http_ota_set_finished_check_cb(HTTP_OTA_UPDATE_TYPE_USER_1, http_ota_user_finished_check_cb);
    http_ota_upgrade();
}

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

void on_mqtt_open_cb(void)
{
    printf("mqtt open success!\n");
}

void on_mqtt_close_cb(void)
{
    printf("mqtt close success!\n");
}

void on_mqtt_error_cb(void)
{
    printf("mqtt error success!\n");
}

void on_mqtt_message_cb(char* topic, char* payload, int length)
{
    printf("mqtt message, topic: %s, msg: %s, len: %d\n", topic, payload, length);
}


mqtt_cli_t hclient = {
    .url = "a1EBSQ9GYN7.iot-as-mqtt.cn-shanghai.aliyuncs.com:1883",
    .client_id = "FESA234FBDS24|securemode=3,signmethod=hmacsha1,timestamp=789|",
    .user_name = "dameng&a1EBSQ9GYN7",
    .password = "f885382c15b40936ed2dd4cf58a869b50256427a",
    .m_on_mqtt_open_cb = on_mqtt_open_cb,
    .m_on_mqtt_close_cb = on_mqtt_close_cb,
    .m_on_mqtt_error_cb = on_mqtt_error_cb,
    .m_on_mqtt_message_cb = on_mqtt_message_cb,
};

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
    wifi_sta_stop();

    mqtt_cli_init(&hclient); 
}

#include "wifi_sta.h"

void test(void)
{
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
}

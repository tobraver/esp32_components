#include "wifi_sta.h"
#include "ws_client.h"

ws_cli_t g_platform_client;

void platform_on_open_handler(void)
{
    printf("on open\n");
}
void platform_on_close_handler(void)
{
    printf("on close\n");
}
void platform_on_error_handler(void)
{
    printf("on error\n");
}

void platform_on_message_handler(char* payload, int length)
{
    printf("on message, payload: %.*s\n", length, payload);
    payload[length] = 0;
    ws_cli_send_text(&g_platform_client, payload);
}

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
    // wifi_sta_stop();

    g_platform_client.url = "ws://192.168.1.121:6001";
    g_platform_client.buf_size = 1024*10;
    g_platform_client.m_on_open_callback = platform_on_open_handler;
    g_platform_client.m_on_close_callback = platform_on_close_handler;
    g_platform_client.m_on_message_callback = platform_on_message_handler;
    g_platform_client.m_on_error_callback = platform_on_error_handler;

    ws_cli_init(&g_platform_client);
    ws_cli_start(&g_platform_client);
}

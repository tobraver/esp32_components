#include "ws_camera.h"
#include "wifi_sta.h"
#include "ws_client.h"
#include "esp_log.h"

static const char* TAG = "ws_camera";

typedef struct {
    char* url;
    ws_cli_t ws_cli;
    uint8_t is_connect;
} ws_camera_desc_t;

static ws_camera_desc_t s_ws_desc;

void ws_camera_on_open(void);
void ws_camera_on_close(void);
void ws_camera_on_error(void);
void ws_camera_on_message(char* msg, int len);


void ws_camera_start(char* url)
{
    while (wifi_sta_is_connected() == false) {
        ESP_LOGW(TAG, "wait for wifi connect.");
        vTaskDelay(1000);
    }
    s_ws_desc.url = url;
    s_ws_desc.is_connect = false;
    s_ws_desc.ws_cli.buf_size = 10*1024;
    s_ws_desc.ws_cli.m_on_open_callback = ws_camera_on_open;
    s_ws_desc.ws_cli.m_on_close_callback = ws_camera_on_close;
    s_ws_desc.ws_cli.m_on_error_callback = ws_camera_on_error;
    s_ws_desc.ws_cli.m_on_message_callback = ws_camera_on_message;
    ws_cli_init(&s_ws_desc.ws_cli);
    ws_cli_start(&s_ws_desc.ws_cli);
}

void ws_camera_stop(void)
{
    ws_cli_stop(&s_ws_desc.ws_cli);
}

void ws_camera_on_open(void)
{
    s_ws_desc.is_connect = true;
    ESP_LOGI(TAG, "ws camera connected!");
    // you can send connect message to server
}

void ws_camera_on_close(void)
{
    s_ws_desc.is_connect = false;
    ESP_LOGE(TAG, "ws camera disconnect, server close!");
}

void ws_camera_on_error(void)
{
    s_ws_desc.is_connect = false;
    ESP_LOGE(TAG, "ws camera disconnect, server or network error!");
}

void ws_camera_on_message(char* msg, int len)
{
    ESP_LOGI(TAG, "ws camera recv, len: %d, message: %.*s", len, len, msg);
    // you can handle message from server
    ws_camera_send_buff((uint8_t*)msg, len);
}

bool ws_camera_is_connected(void)
{
    return s_ws_desc.is_connect;
}

void ws_camera_send_str(char* str)
{
    ws_cli_send_text(&s_ws_desc.ws_cli, str);
}

void ws_camera_send_buff(uint8_t* buff, int len)
{
    ws_cli_send_bin(&s_ws_desc.ws_cli, (char*)buff, len);
}
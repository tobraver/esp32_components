#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include "stdio.h"
#include "stdint.h"
#include "esp_websocket_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*on_mqtt_open_cb_t)(void);
typedef void (*on_mqtt_close_cb_t)(void);
typedef void (*on_mqtt_message_cb_t)(char* topic, char* payload, int length);
typedef void (*on_mqtt_error_cb_t)(void);


typedef struct {
    char* url;
    int   buf_size;
    char* client_id;
    char* user_name;
    char* password;
    esp_mqtt_client_handle_t m_client;
    on_mqtt_open_cb_t m_on_mqtt_open_cb;
    on_mqtt_close_cb_t m_on_mqtt_close_cb;
    on_mqtt_message_cb_t m_on_mqtt_message_cb;
    on_mqtt_error_cb_t m_on_mqtt_error_cb;
} mqtt_cli_t;

void mqtt_cli_init(ws_cli_t* hclient);
void mqtt_cli_set_on_open(ws_cli_t* hclient, on_mqtt_open_cb_t callback);
void mqtt_cli_set_on_close(ws_cli_t* hclient, on_mqtt_close_cb_t callback);
void mqtt_cli_set_on_message(ws_cli_t* hclient, on_mqtt_message_cb_t callback);
void mqtt_cli_set_on_error(ws_cli_t* hclient, on_mqtt_error_cb_t callback);
void mqtt_cli_start(ws_cli_t* hclient);
void mqtt_cli_stop(ws_cli_t* hclient);
void mqtt_cli_subscribe(mqtt_cli_t* hclient, char *topic);
void mqtt_cli_unsubscribe(mqtt_cli_t* hclient, char *topic);
void mqtt_cli_send_buff(mqtt_cli_t* hclient, char* topic, char* buff, int len);

#ifdef __cplusplus
}
#endif
#endif // !__MQTT_CLIENT_H__

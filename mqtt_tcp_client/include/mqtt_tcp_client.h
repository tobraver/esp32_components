#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"
#include "mqtt_client.h"

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

bool mqtt_cli_init(mqtt_cli_t* hclient);
bool mqtt_cli_set_on_open(mqtt_cli_t* hclient, on_mqtt_open_cb_t callback);
bool mqtt_cli_set_on_close(mqtt_cli_t* hclient, on_mqtt_close_cb_t callback);
bool mqtt_cli_set_on_message(mqtt_cli_t* hclient, on_mqtt_message_cb_t callback);
bool mqtt_cli_set_on_error(mqtt_cli_t* hclient, on_mqtt_error_cb_t callback);
bool mqtt_cli_start(mqtt_cli_t* hclient);
bool mqtt_cli_stop(mqtt_cli_t* hclient);
bool mqtt_cli_subscribe(mqtt_cli_t* hclient, char *topic);
bool mqtt_cli_unsubscribe(mqtt_cli_t* hclient, char *topic);
bool mqtt_cli_send_buff(mqtt_cli_t* hclient, char* topic, char* buff, int len);

#ifdef __cplusplus
}
#endif
#endif // !__MQTT_CLIENT_H__

#include "mqtt_client.h"

static void mqtt_cli_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    mqtt_cli_t* hclient = (mqtt_cli_t*)handler_args;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        if(hclient->m_on_open_callback != NULL) {
            hclient->m_on_open_callback();
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        if(hclient->m_on_close_callback != NULL) {
            hclient->m_on_close_callback();
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_DATA:
        if(hclient->m_on_message_callback != NULL) {
            hclient->m_on_message_callback(event->topic, event->data, event->data_len);
        } 
        break;
    case MQTT_EVENT_ERROR:
        if(hclient->m_on_error_callback != NULL) {
            hclient->m_on_error_callback();
        } 
        break;
    default:
        break;
    }
}

void mqtt_cli_init(mqtt_cli_t* hclient)
{
    esp_mqtt_client_config_t conf = { 0 };
    memset(&conf, 0, sizeof(conf));
    conf.uri = hclient->url;
    conf.client_id = hclient->client_id;
    conf.user_name = hclient->user_name;
    conf.password = hclient->password;
    conf.buf_size = hclient->buf_size;
    conf.task_stack = hclient->buf_size + 4096;
    hclient->m_client = esp_mqtt_client_init(&conf);
    esp_mqtt_client_register_event(hclient->m_client, ESP_EVENT_ANY_ID, mqtt_cli_event_handler, (void *)hclient);
}

void mqtt_cli_set_on_open(mqtt_cli_t* hclient, on_open_callback_t callback)
{
    hclient->m_on_open_callback = callback;
}

void mqtt_cli_set_on_close(mqtt_cli_t* hclient, on_close_callback_t callback)
{
    hclient->m_on_close_callback = callback;
}

void mqtt_cli_set_on_message(mqtt_cli_t* hclient, on_message_callback_t callback)
{
    hclient->m_on_message_callback = callback;
}

void mqtt_cli_set_on_error(mqtt_cli_t* hclient, on_error_callback_t callback)
{
    hclient->m_on_error_callback = callback;
}

void mqtt_cli_start(mqtt_cli_t* hclient)
{
    esp_mqtt_client_start(hclient->m_client);
}

void mqtt_cli_stop(mqtt_cli_t* hclient)
{
    esp_mqtt_client_stop(hclient->m_client);
}

void mqtt_cli_subscribe(mqtt_cli_t* hclient, char *topic)
{
    esp_mqtt_client_subscribe(hclient->m_client, topic, 0);
}

void mqtt_cli_unsubscribe(mqtt_cli_t* hclient, char *topic)
{
    esp_mqtt_client_unsubscribe(hclient->m_client, topic);
}

void mqtt_cli_send_buff(mqtt_cli_t* hclient, char* topic, char* buff, int len)
{
    if(buff == NULL || len == 0) {
        return;
    }
    esp_mqtt_client_publish(hclient->m_client, topic, buff, len, 0, 0);
}

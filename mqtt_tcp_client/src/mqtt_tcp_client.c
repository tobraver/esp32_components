#include "mqtt_tcp_client.h"
#include "string.h"
#include "esp_log.h"

static const char* TAG = "mqtt_tcp_client";

static void mqtt_cli_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    mqtt_cli_t* hclient = (mqtt_cli_t*)handler_args;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        if(hclient->m_on_mqtt_open_cb != NULL) {
            hclient->m_on_mqtt_open_cb();
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        if(hclient->m_on_mqtt_close_cb != NULL) {
            hclient->m_on_mqtt_close_cb();
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_DATA:
        if(hclient->m_on_mqtt_message_cb != NULL) {
            hclient->m_on_mqtt_message_cb(event->topic, event->data, event->data_len);
        } 
        break;
    case MQTT_EVENT_ERROR:
        if(hclient->m_on_mqtt_error_cb != NULL) {
            hclient->m_on_mqtt_error_cb();
        } 
        break;
    default:
        break;
    }
}

bool mqtt_cli_init(mqtt_cli_t* hclient)
{
    if(hclient == NULL) {
        ESP_LOGE(TAG, "mqtt client init failed, hclient is NULL");
        return false;
    }
    esp_mqtt_client_config_t conf = { 0 };
    memset(&conf, 0, sizeof(conf));
    conf.broker.address.uri = hclient->url;
    conf.credentials.client_id = hclient->client_id;
    conf.credentials.username = hclient->user_name;
    conf.credentials.authentication.password = hclient->password;
    conf.buffer.size = hclient->buf_size;
    hclient->m_client = esp_mqtt_client_init(&conf);
    if(hclient->m_client == NULL) {
        ESP_LOGE(TAG, "mqtt client init failed, url: %s", hclient->url);
        return false;
    }
    esp_err_t error = esp_mqtt_client_register_event(hclient->m_client, ESP_EVENT_ANY_ID, mqtt_cli_event_handler, (void *)hclient);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mqtt client register event failed, url %s", hclient->url);
        return false;
    }
    ESP_LOGI(TAG, "mqtt client init success, url: %s", hclient->url);
    return true;
}

bool mqtt_cli_set_on_open(mqtt_cli_t* hclient, on_mqtt_open_cb_t callback)
{
    if(hclient == NULL || callback == NULL) {
        return false;
    }
    hclient->m_on_mqtt_open_cb = callback;
    return true;
}

bool mqtt_cli_set_on_close(mqtt_cli_t* hclient, on_mqtt_close_cb_t callback)
{
    if(hclient == NULL || callback == NULL) {
        return false;
    }
    hclient->m_on_mqtt_close_cb = callback;
    return true;
}

bool mqtt_cli_set_on_message(mqtt_cli_t* hclient, on_mqtt_message_cb_t callback)
{
    if(hclient == NULL || callback == NULL) {
        return false;
    }
    hclient->m_on_mqtt_message_cb = callback;
    return true;
}

bool mqtt_cli_set_on_error(mqtt_cli_t* hclient, on_mqtt_error_cb_t callback)
{
    if(hclient == NULL || callback == NULL) {
        return false;
    }
    hclient->m_on_mqtt_error_cb = callback;
    return true;
}

bool mqtt_cli_start(mqtt_cli_t* hclient)
{
    if(hclient == NULL || hclient->m_client) {
        ESP_LOGE(TAG, "mqtt client start failed, hclient is NULL or m_client is NULL");
        return false;
    }
    esp_err_t error = esp_mqtt_client_start(hclient->m_client);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mqtt client start failed, url: %s", hclient->url);
        return false;
    }
    ESP_LOGI(TAG, "mqtt client start success, url: %s", hclient->url);
    return true;
}

bool mqtt_cli_stop(mqtt_cli_t* hclient)
{
    if(hclient == NULL) {
        return true;
    }
    esp_err_t error = esp_mqtt_client_stop(hclient->m_client);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mqtt client stop failed, url: %s", hclient->url);
        return false;
    }
    ESP_LOGI(TAG, "mqtt client stop success, url: %s", hclient->url);
    return true;
}

bool mqtt_cli_subscribe(mqtt_cli_t* hclient, char *topic)
{
    if(hclient == NULL || hclient->m_client) {
        ESP_LOGE(TAG, "mqtt client subscribe failed, hclient is NULL or m_client is NULL");
        return false;
    }
    esp_err_t error = esp_mqtt_client_subscribe(hclient->m_client, topic, 0);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mqtt client subscribe failed, topic: %s, qos0", topic);
        return false;
    }
    ESP_LOGI(TAG, "mqtt client subscribe success, topic: %s, qos0", topic);
    return true;
}

bool mqtt_cli_unsubscribe(mqtt_cli_t* hclient, char *topic)
{
    if(hclient == NULL || hclient->m_client) {
        ESP_LOGE(TAG, "mqtt client unsubscribe failed, hclient is NULL or m_client is NULL");
        return false;
    }
    esp_err_t error = esp_mqtt_client_unsubscribe(hclient->m_client, topic);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mqtt client unsubscribe failed, topic: %s", topic);
        return false;
    }
    ESP_LOGI(TAG, "mqtt client unsubscribe success, topic: %s", topic);
    return true;
}

bool mqtt_cli_send_buff(mqtt_cli_t* hclient, char* topic, char* buff, int len)
{
    if(hclient == NULL || hclient->m_client) {
        ESP_LOGE(TAG, "mqtt client send buff failed, hclient is NULL or m_client is NULL");
        return false;
    }
    if(buff == NULL || len == 0) {
        ESP_LOGE(TAG, "mqtt client send buff failed, buff is NULL or len == 0");
        return false;
    }
    int ret = esp_mqtt_client_publish(hclient->m_client, topic, buff, len, 0, 0);
    if(ret < 0) {
        ESP_LOGE(TAG, "mqtt client send buff failed, topic: %s", topic);
        return false;
    }
    ESP_LOGI(TAG, "mqtt client send buff success, topic: %s, msg_id: %d", topic, ret);
    return true;
}

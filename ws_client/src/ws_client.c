#include "ws_client.h"

void ws_cli_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ws_cli_t* hclient = (ws_cli_t*)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) 
    {
    case WEBSOCKET_EVENT_CONNECTED:
    {
        if(hclient->m_on_open_callback != NULL)
        {
            hclient->m_on_open_callback();
        }
    } break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    {
        if(hclient->m_on_close_callback != NULL)
        {
            hclient->m_on_close_callback();
        }
    } break;
    case WEBSOCKET_EVENT_DATA:
    {
        // 0x01: text frame; 0x02: chunk frame
        if((data->op_code == 0x01) || (data->op_code == 0x02))
        {
            if(hclient->m_on_message_callback != NULL)
            {
                hclient->m_on_message_callback((char*)data->data_ptr, data->data_len);
            } 
        }
        
    } break;
    case WEBSOCKET_EVENT_ERROR:
    {
        if(hclient->m_on_error_callback != NULL)
        {
            hclient->m_on_error_callback();
        } 
    } break;
    }
}

void ws_cli_init(ws_cli_t* hclient)
{
    esp_websocket_client_config_t conf = {};
    memset(&conf, 0, sizeof(conf));
    conf.uri = hclient->url;
    conf.buffer_size = hclient->buf_size;
    conf.task_stack = hclient->buf_size + 4096;
    hclient->m_client = esp_websocket_client_init(&conf);
    esp_websocket_register_events(hclient->m_client, WEBSOCKET_EVENT_ANY, ws_cli_event_handler, (void *)hclient);
}

void ws_cli_set_on_open(ws_cli_t* hclient, on_open_callback_t callback)
{
    hclient->m_on_open_callback = callback;
}

void ws_cli_set_on_close(ws_cli_t* hclient, on_close_callback_t callback)
{
    hclient->m_on_close_callback = callback;
}

void ws_cli_set_on_message(ws_cli_t* hclient, on_message_callback_t callback)
{
    hclient->m_on_message_callback = callback;
}

void ws_cli_set_on_error(ws_cli_t* hclient, on_error_callback_t callback)
{
    hclient->m_on_error_callback = callback;
}

void ws_cli_start(ws_cli_t* hclient)
{
    esp_websocket_client_start(hclient->m_client);
}

void ws_cli_stop(ws_cli_t* hclient)
{
    esp_websocket_client_stop(hclient->m_client);
}

void ws_cli_send_text(ws_cli_t* hclient, const char* msg)
{
    if(msg == NULL)
    {
        return ;
    }

    esp_websocket_client_handle_t client = hclient->m_client;
    if(esp_websocket_client_is_connected(client)) 
    {
        esp_websocket_client_send_text(client, msg, strlen(msg), portMAX_DELAY);
    }
}

void ws_cli_send_bin(ws_cli_t* hclient, const char* buff, int len)
{
    if((buff == NULL) || (len == 0))
    {
        return ;
    }

    esp_websocket_client_handle_t client = hclient->m_client;
    if(esp_websocket_client_is_connected(client)) 
    {
        esp_websocket_client_send_bin(client, buff, len, portMAX_DELAY);
    }
}

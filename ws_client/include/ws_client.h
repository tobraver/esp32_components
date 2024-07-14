#ifndef __WS_CLIENT_H__
#define __WS_CLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdio.h"
#include "stdint.h"
#include "esp_websocket_client.h"

typedef void (*on_open_callback_t)(void);
typedef void (*on_close_callback_t)(void);
typedef void (*on_message_callback_t)(char* payload, int length);
typedef void (*on_error_callback_t)(void);


typedef struct {
    const char* url;
    int         buf_size;
    esp_websocket_client_handle_t m_client;
    on_open_callback_t m_on_open_callback;
    on_close_callback_t m_on_close_callback;
    on_message_callback_t m_on_message_callback;
    on_error_callback_t m_on_error_callback;
} ws_cli_t;

void ws_cli_init(ws_cli_t* hclient);
void ws_cli_set_on_open(ws_cli_t* hclient, on_open_callback_t callback);
void ws_cli_set_on_close(ws_cli_t* hclient, on_close_callback_t callback);
void ws_cli_set_on_message(ws_cli_t* hclient, on_message_callback_t callback);
void ws_cli_set_on_error(ws_cli_t* hclient, on_error_callback_t callback);
void ws_cli_start(ws_cli_t* hclient);
void ws_cli_stop(ws_cli_t* hclient);
void ws_cli_send_text(ws_cli_t* hclient, const char* msg);
void ws_cli_send_bin(ws_cli_t* hclient, const char* buff, int len);


#ifdef __cplusplus
}
#endif
#endif // !__WS_CLIENT_H__

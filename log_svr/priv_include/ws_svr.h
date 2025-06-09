#ifndef __WS_SVR_H__
#define __WS_SVR_H__

#include "stdio.h"
#include "esp_err.h"

// ws server port
#define WS_SVR_PORT 9999

esp_err_t ws_svr_start(void);
esp_err_t ws_svr_stop(void);
esp_err_t ws_svr_connected(void);
esp_err_t ws_svr_send_text(void* buff, size_t len);

#endif // __WS_SVR_H__

#ifndef __WIFI_STA_H__
#define __WIFI_STA_H__

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#if __cplusplus
extern "C" {
#endif

bool wifi_sta_start(char* ssid, char* password);
bool wifi_sta_stop(void);
bool wifi_sta_is_connected(void);
bool wifi_sta_wait_connected(uint32_t timeout_ms);
char* wifi_sta_if_name(void);
uint32_t wifi_sta_get_ip_addr(void);
char* wifi_sta_get_ip_str(void);
char* wifi_sta_get_ssid(void);
char* wifi_sta_get_password(void);

#if __cplusplus
}
#endif
#endif // !__WIFI_STA_H__

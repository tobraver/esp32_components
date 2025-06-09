#ifndef __LOG_SVR_H__
#define __LOG_SVR_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define LOG_SVR_DURATION_NUM    10

typedef char* (*log_svr_get_config_t)(void);
typedef int (*log_svr_get_connect_t)(void); // 0: connected, other: not connected
typedef int (*log_svr_get_file_support_t)(void); // 0: support, other: not support
typedef int (*log_svr_response_handler_t)(char* response, char* modify_time);
typedef int (*log_svr_get_netif_t)(void); // 0: ETH, 1: 4G
typedef int (*log_svr_get_plugin_t)(void); // 1: plugin
typedef int (*log_svr_get_login_t)(void); // 0: logout, 1: login
typedef int (*log_svr_trigger_finish_t)(uint32_t type, uint64_t timestamp);

typedef struct {
    log_svr_get_config_t get_url;
    log_svr_get_config_t get_boundary;
    log_svr_get_config_t get_token;
    log_svr_get_config_t get_basetoken;
    log_svr_get_config_t get_module;
    log_svr_get_config_t get_orgid;
    log_svr_get_config_t get_mac;
    log_svr_get_config_t get_file_name;
    log_svr_get_netif_t get_netif;
    log_svr_get_plugin_t get_plugin;
    log_svr_get_connect_t get_connect;
    log_svr_get_login_t get_login;
    log_svr_get_file_support_t get_file_support;
    log_svr_response_handler_t response_handler;
    log_svr_trigger_finish_t trigger_finish;
} log_svr_config_t;

void log_svr_init(log_svr_config_t config);
void log_svr_deinit(void);
void log_svr_trigger_output(uint32_t type, uint64_t timestamp);
void log_svr_output_duration(char* json);
bool log_svr_duration_verify(void);
bool log_svr_utils_file_upload(const char* module, const char* name, char* content, int len, log_svr_response_handler_t response_handle);


#ifdef __cplusplus
}
#endif // __cplusplus
#endif

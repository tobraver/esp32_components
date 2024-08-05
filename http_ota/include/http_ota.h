#ifndef __HTTP_OTA_H__
#define __HTTP_OTA_H__

#include "stdio.h"
#include "stdint.h"
#include "stdbool.h"

typedef enum {
    HTTP_OTA_UPDATE_TYPE_MODEL,
    HTTP_OTA_UPDATE_TYPE_MUSIC,
    HTTP_OTA_UPDATE_TYPE_USER_1,
    HTTP_OTA_UPDATE_TYPE_USER_2,
    HTTP_OTA_UPDATE_TYPE_USER_3,
    HTTP_OTA_UPDATE_TYPE_FIRMWARE,
    HTTP_OTA_UPDATE_TYPE_MAX,
} http_ota_update_type_t;

typedef bool (*http_ota_prepare_cb_t)(void);
typedef bool (*http_ota_need_upgrade_cb_t)(void);
typedef bool (*http_ota_upgrade_pkt_cb_t)(uint8_t* buff, uint32_t len);
typedef bool (*http_ota_finished_check_cb_t)(void);
typedef void (*http_ota_upgrade_status_cb_t)(http_ota_update_type_t type, bool is_succ);

#if __cplusplus
extern "C" {
#endif

bool http_ota_init(void);
bool http_ota_deinit(void);
bool http_ota_set_url(http_ota_update_type_t type, const char* url);
bool http_ota_set_prepare_cb(http_ota_update_type_t type, http_ota_prepare_cb_t cb);
bool http_ota_set_need_upgrade_cb(http_ota_update_type_t type, http_ota_need_upgrade_cb_t cb);
bool http_ota_set_upgrade_pkt_cb(http_ota_update_type_t type, http_ota_upgrade_pkt_cb_t cb);
bool http_ota_set_finished_check_cb(http_ota_update_type_t type, http_ota_finished_check_cb_t cb);
bool http_ota_set_status_cb(http_ota_upgrade_status_cb_t cb);
bool http_ota_upgrade(void);
bool http_ota_is_update(void);

#if __cplusplus
}
#endif
#endif // !__HTTP_OTA_H__

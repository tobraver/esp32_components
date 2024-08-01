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

typedef enum {
    HTTP_OTA_MSG_TYPE_START = 0,
    HTTP_OTA_MSG_TYPE_DATA,
    HTTP_OTA_MSG_TYPE_END,
} http_ota_msg_type_t;

typedef bool (*http_ota_prepare_cb_t)(void);
typedef bool (*http_ota_need_upgrade_cb_t)(void);
typedef bool (*http_ota_upgrade_pkt_cb_t)(uint8_t* buff, uint32_t len);
typedef bool (*http_ota_finished_check_cb_t)(void);

#if __cplusplus
extern "C" {
#endif


#if __cplusplus
}
#endif
#endif // !__HTTP_OTA_H__

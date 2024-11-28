#ifndef __HTTP_UPGRADE_H__
#define __HTTP_UPGRADE_H__

#include "stdio.h"
#include "esp_err.h"
#include "ota_service.h"

#define HTTP_UPGRADE_MD5_ENABLE     1

#define HTTP_UPGRADE_WEB_LABEL      "storage"
#define HTTP_UPGRADE_MODEL_LABEL    "model"
#define HTTP_UPGRADE_MUSIC_LABEL    "flash_tone"
#define HTTP_UPGRADE_APP_LABEL      "app"

typedef enum {
    HTTP_UPGRADE_EVENT_TYPE_ENTER,
    HTTP_UPGRADE_EVENT_TYPE_DOWNLOAD,
    HTTP_UPGRADE_EVENT_TYPE_RESUME,
    HTTP_UPGRADE_EVENT_TYPE_MD5,
} http_upgrade_event_type_t;

typedef struct {
    int type; // @ref http_upgrade_event_type_t
    void* data;
    int len;
} http_upgrade_event_t;

typedef esp_err_t (*http_upgrade_read_resume_t)(const char* label, uint64_t* value);
typedef esp_err_t (*http_upgrade_write_resume_t)(const char* label, uint64_t value);
typedef esp_err_t (*http_upgrade_read_md5_t)(const char* label, char md5[33]);
typedef esp_err_t (*http_upgrade_write_md5_t)(const char* label, char md5[33]);
typedef esp_err_t (*http_upgrade_event_cb_t)(const char* label, http_upgrade_event_t* event);

typedef struct {
    http_upgrade_read_resume_t read_resume;
    http_upgrade_write_resume_t write_resume;
    http_upgrade_read_md5_t read_md5;
    http_upgrade_write_md5_t write_md5;
    http_upgrade_event_cb_t event_cb;
} http_upgrade_config_t;

esp_err_t http_upgrade_init(http_upgrade_config_t config);
esp_err_t http_upgrade_get_default_proc(ota_upgrade_ops_t *ops);
esp_err_t http_upgrade_cancel_resume(const char *label);

#endif // __HTTP_UPGRADE_H__

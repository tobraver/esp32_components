#include "http_ota.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "audio_mem.h"
#include "esp_https_ota.h"
#include "ota_proc_default.h"
#include "esp_fs_ota.h"
#include "fatfs_stream.h"
#include "http_stream.h"

#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "http_ota";

extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_server_cert_pem_end");

#define UPGRADE_READER_BUF_LEN 1024

// [warning] not modify
typedef struct {
    void *ota_handle;
    esp_err_t (*get_img_desc)(void *handle, esp_app_desc_t *new_app_info);
    esp_err_t (*perform)(void *handle);
    int (*get_image_len_read)(void *handle);
    bool (*all_read)(void *handle);
    esp_err_t (*finish)(void *handle);
} ota_app_upgrade_ctx_t;

typedef struct {
    int write_offset;
    char read_buf[UPGRADE_READER_BUF_LEN];
    audio_element_handle_t r_stream;
} http_ota_upgrade_ctx_t;

typedef struct {
    uint8_t is_init;
    uint8_t is_update;
    uint8_t is_app_succ;
    SemaphoreHandle_t lock;
    periph_service_handle_t handle;
    ota_upgrade_ops_t upgrade_list[HTTP_OTA_UPDATE_TYPE_MAX];
    ota_upgrade_ops_t current_list[HTTP_OTA_UPDATE_TYPE_MAX];
    http_ota_prepare_cb_t prepare_cb[HTTP_OTA_UPDATE_TYPE_MAX];
    http_ota_need_upgrade_cb_t need_upgrade_cb[HTTP_OTA_UPDATE_TYPE_MAX];
    http_ota_upgrade_pkt_cb_t upgrade_pkt_cb[HTTP_OTA_UPDATE_TYPE_MAX];
    http_ota_finished_check_cb_t finished_check_cb[HTTP_OTA_UPDATE_TYPE_MAX];
    http_ota_upgrade_status_cb_t status_sb;
} http_ota_desc_t;

static http_ota_desc_t s_ota_desc;

static char* http_ota_get_label(http_ota_update_type_t type);
static http_ota_update_type_t http_ota_get_type(char* label);
static ota_service_err_reason_t _http_ota_ops_partition_need_upgrade(void *handle, ota_node_attr_t *node);
static ota_service_err_reason_t _http_ota_ops_app_need_upgrade(void *handle, ota_node_attr_t *node);

// notify update stauts to user
#define HTTP_OTA_NOTIFY_UPDATE_STATUS(type, succ)  if(s_ota_desc.status_sb) { s_ota_desc.status_sb(type, succ); }

void http_ota_desc_lock(void)
{
    if(s_ota_desc.lock) {
        xSemaphoreTake(s_ota_desc.lock, portMAX_DELAY);
    }
}

void http_ota_desc_unlock(void)
{
    if(s_ota_desc.lock) {
        xSemaphoreGive(s_ota_desc.lock);
    }
}

esp_err_t http_ota_event_handler(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    if (evt->type == OTA_SERV_EVENT_TYPE_RESULT) {
        ota_result_t *result_data = evt->data;
        ota_upgrade_ops_t* ops = &s_ota_desc.current_list[result_data->id];
        http_ota_update_type_t type = http_ota_get_type(ops->node.label);
        if (result_data->result != ESP_OK) {
            ESP_LOGE(TAG, "List id: %d, Label: %s, OTA failed", result_data->id, ops->node.label);
            HTTP_OTA_NOTIFY_UPDATE_STATUS(type, false);
        } else {
            if(strcmp(ops->node.label, http_ota_get_label(HTTP_OTA_UPDATE_TYPE_FIRMWARE)) == 0) {
                if(s_ota_desc.is_app_succ) {
                    ESP_LOGI(TAG, "List id: %d, Label: %s, OTA success", result_data->id, ops->node.label);
                    HTTP_OTA_NOTIFY_UPDATE_STATUS(type, true);
                    esp_restart();
                } else {
                    ESP_LOGE(TAG, "List id: %d, Label: %s, OTA failed", result_data->id, ops->node.label);
                    HTTP_OTA_NOTIFY_UPDATE_STATUS(type, false);
                }
            } else {
                HTTP_OTA_NOTIFY_UPDATE_STATUS(type, true);
                ESP_LOGI(TAG, "List id: %d, Label: %s, OTA success", result_data->id, ops->node.label);
            }
        }
    } else if (evt->type == OTA_SERV_EVENT_TYPE_FINISH) {
        http_ota_desc_lock();
        s_ota_desc.is_update = false;
        http_ota_desc_unlock();
        ESP_LOGI(TAG, "http ota finished...");
    }

    return ESP_OK;
}

bool http_ota_init(void)
{
    if(s_ota_desc.is_init) {
        ESP_LOGI(TAG, "Http ota is already init");
        return true;
    }

    s_ota_desc.is_update = false;
    if(s_ota_desc.lock == NULL) {
        s_ota_desc.lock = xSemaphoreCreateMutex();
    }
    if(s_ota_desc.lock == NULL) {
        ESP_LOGE(TAG, "Create ota lock failed");
        return false;
    }

    ota_service_config_t cfg = OTA_SERVICE_DEFAULT_CONFIG();
    cfg.task_stack = 8 * 1024;
    cfg.evt_cb = http_ota_event_handler;
    cfg.cb_ctx = NULL;
    periph_service_handle_t ota_service = ota_service_create(&cfg);
    if(ota_service == NULL) {
        ESP_LOGE(TAG, "Create ota service failed");
        return false;
    }

    s_ota_desc.handle = ota_service;
    s_ota_desc.is_init = true;
    ESP_LOGI(TAG, "Create ota service success");
    return true;
}

bool http_ota_deinit(void)
{
    if(s_ota_desc.is_init == false) {
        ESP_LOGW(TAG, "Http ota is not init");
        return true;
    }
    
    http_ota_desc_lock();
    periph_service_destroy(s_ota_desc.handle);
    s_ota_desc.is_update = false;
    s_ota_desc.is_init = false;
    s_ota_desc.handle = NULL;
    http_ota_desc_unlock();
    return true;
}

static char* s_http_ota_label[HTTP_OTA_UPDATE_TYPE_MAX] = {
    [HTTP_OTA_UPDATE_TYPE_MODEL] = "model",
    [HTTP_OTA_UPDATE_TYPE_MUSIC] = "flash_tone",
    [HTTP_OTA_UPDATE_TYPE_USER_1] = "user1",
    [HTTP_OTA_UPDATE_TYPE_USER_2] = "user2",
    [HTTP_OTA_UPDATE_TYPE_USER_3] = "user3",
    [HTTP_OTA_UPDATE_TYPE_FIRMWARE] = "firmware"
};

static char* http_ota_get_label(http_ota_update_type_t type)
{
    if(type >= HTTP_OTA_UPDATE_TYPE_MAX) {
        return "null";
    }
    return s_http_ota_label[type];
}

static http_ota_update_type_t http_ota_get_type(char* label)
{
    http_ota_update_type_t type = HTTP_OTA_UPDATE_TYPE_MAX;
    if(label == NULL) {
        return type;
    }
    for(http_ota_update_type_t i=0; i<HTTP_OTA_UPDATE_TYPE_MAX; i++) {
        if(strcmp(label, s_http_ota_label[i]) == 0) {
            type = i;
            break;
        }
    }
    return type;
}

char* http_ota_get_cert_pem(void)
{
    return (char*)server_cert_pem_start;
}

bool http_ota_set_url(http_ota_update_type_t type, const char* url)
{
    if(s_ota_desc.is_update) {
        ESP_LOGW(TAG, "Http ota is updating");
        return false;
    }

    if(type >= HTTP_OTA_UPDATE_TYPE_MAX) {
        ESP_LOGE(TAG, "Http ota type error");
        return false;
    }

    http_ota_desc_lock();

    s_ota_desc.upgrade_list[type].node.uri = NULL;
    s_ota_desc.upgrade_list[type].node.label = http_ota_get_label(type);
    s_ota_desc.upgrade_list[type].node.cert_pem = http_ota_get_cert_pem();
    s_ota_desc.upgrade_list[type].node.uri = (char*)url;
    s_ota_desc.upgrade_list[type].prepare = NULL;
    s_ota_desc.upgrade_list[type].need_upgrade = NULL;
    s_ota_desc.upgrade_list[type].execute_upgrade = NULL;
    s_ota_desc.upgrade_list[type].finished_check = NULL;
    s_ota_desc.upgrade_list[type].break_after_fail = true;
    s_ota_desc.upgrade_list[type].reboot_flag = false;

    if((type == HTTP_OTA_UPDATE_TYPE_MODEL) || (type == HTTP_OTA_UPDATE_TYPE_MUSIC)) {
        s_ota_desc.upgrade_list[type].node.type = ESP_PARTITION_TYPE_DATA;
        ota_data_get_default_proc(&s_ota_desc.upgrade_list[type]);
        s_ota_desc.upgrade_list[type].need_upgrade = _http_ota_ops_partition_need_upgrade;
    } else if(type == HTTP_OTA_UPDATE_TYPE_FIRMWARE) {
        s_ota_desc.upgrade_list[type].node.type = ESP_PARTITION_TYPE_APP;
        ota_app_get_default_proc(&s_ota_desc.upgrade_list[type]);
        s_ota_desc.upgrade_list[type].need_upgrade = _http_ota_ops_app_need_upgrade;
    } else {
        s_ota_desc.upgrade_list[type].node.type = ESP_PARTITION_TYPE_DATA;
    }

    http_ota_desc_unlock();
    return true;
}

http_ota_update_type_t http_ota_get_node_type(ota_node_attr_t *node)
{
    http_ota_update_type_t type = HTTP_OTA_UPDATE_TYPE_MAX;
    if(node == NULL) {
        ESP_LOGE(TAG, "node is null");
        return type;
    }
    for(http_ota_update_type_t i=0; i<HTTP_OTA_UPDATE_TYPE_MAX; i++) {
        if((s_ota_desc.upgrade_list[i].node.label != NULL) && \
           (strcmp(node->label, s_ota_desc.upgrade_list[i].node.label) == 0)) {
            type = i;
        }
    }
    return type;
}

static ota_service_err_reason_t _http_ota_ops_prepare(void **handle, ota_node_attr_t *node)
{
    http_ota_upgrade_ctx_t *context = audio_calloc(1, sizeof(http_ota_upgrade_ctx_t));
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    *handle = NULL;

    http_ota_update_type_t type = http_ota_get_node_type(node);
    if(type >= HTTP_OTA_UPDATE_TYPE_MAX) {
        ESP_LOGE(TAG, "prepare type error");
        audio_free(context);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }

    http_ota_prepare_cb_t exec_prepare_cb = s_ota_desc.prepare_cb[type];
    if(exec_prepare_cb == NULL) {
        ESP_LOGE(TAG, "prepare cb is null");
        audio_free(context);
        return OTA_SERV_ERR_REASON_NULL_POINTER;
    }

    if(exec_prepare_cb() == false) {
        ESP_LOGE(TAG, "prepare is failed");
        audio_free(context);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }
    
    AUDIO_NULL_CHECK(TAG, node->uri, {
                    audio_free(context);
                    return OTA_SERV_ERR_REASON_NULL_POINTER;
                    });
    ESP_LOGI(TAG, "radar upgrade uri %s", node->uri);
    if (strstr(node->uri, "file://")) {
        fatfs_stream_cfg_t fs_cfg = FATFS_STREAM_CFG_DEFAULT();
        fs_cfg.type = AUDIO_STREAM_READER;

        context->r_stream = fatfs_stream_init(&fs_cfg);
    } else if (strstr(node->uri, "https://") || strstr(node->uri, "http://")) {
        http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
        http_cfg.type = AUDIO_STREAM_READER;

        context->r_stream = http_stream_init(&http_cfg);
    } else {
        ESP_LOGE(TAG, "not support uri");
        audio_free(context);
        return OTA_SERV_ERR_REASON_URL_PARSE_FAIL;
    }
    audio_element_set_uri(context->r_stream, node->uri);
    if (audio_element_process_init(context->r_stream) != ESP_OK) {
        ESP_LOGE(TAG, "reader stream init failed");
        audio_element_deinit(context->r_stream);
        audio_free(context);
        return OTA_SERV_ERR_REASON_STREAM_INIT_FAIL;
    }

    *handle = context;
    return OTA_SERV_ERR_REASON_SUCCESS;
}

static ota_service_err_reason_t _http_ota_ops_need_upgrade(void *handle, ota_node_attr_t *node)
{
    http_ota_update_type_t type = http_ota_get_node_type(node);
    if(type >= HTTP_OTA_UPDATE_TYPE_MAX) {
        ESP_LOGE(TAG, "need upgrade type error");
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }

    http_ota_need_upgrade_cb_t exec_need_upgrade_cb = s_ota_desc.need_upgrade_cb[type];
    if(exec_need_upgrade_cb == NULL) {
        ESP_LOGE(TAG, "need upgrade cb is null");
        return OTA_SERV_ERR_REASON_NULL_POINTER;
    }

    if(exec_need_upgrade_cb() == false) {
        ESP_LOGE(TAG, "need upgrade is failed");
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }
    return OTA_SERV_ERR_REASON_SUCCESS;
}

static ota_service_err_reason_t _http_ota_ops_partition_need_upgrade(void *handle, ota_node_attr_t *node)
{
    uint8_t target_mask = 0;
    char* target_label = http_ota_get_label(HTTP_OTA_UPDATE_TYPE_MODEL);
    if(node->label && target_label && strcmp(target_label, node->label) == 0) {
        target_mask |= (1 << HTTP_OTA_UPDATE_TYPE_MODEL);
    }
    target_label = http_ota_get_label(HTTP_OTA_UPDATE_TYPE_MUSIC);
    if(node->label && target_label && strcmp(target_label, node->label) == 0) {
        target_mask |= (1 << HTTP_OTA_UPDATE_TYPE_MUSIC);
    }
    if(target_mask == 0) {
        ESP_LOGE(TAG, "partition label [%s] not match label [model, music]", node->label);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }

    // label: model, flash_tone
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, node->label);
    if (partition == NULL) {
        ESP_LOGE(TAG, "data partition [%s] not found", node->label);
        return OTA_SERV_ERR_REASON_PARTITION_NOT_FOUND;
    }

    // read header from incoming stream
    uint8_t incoming_header[UPGRADE_READER_BUF_LEN] = { 0 };
    if (ota_data_image_stream_read(handle, (char *)incoming_header, sizeof(incoming_header)) != ESP_OK) {
        ESP_LOGE(TAG, "read partition [%s] incoming header failed, size: %d", node->label, sizeof(incoming_header));
        return OTA_SERV_ERR_REASON_STREAM_RD_FAIL;
    }
    char incoming_header_str[UPGRADE_READER_BUF_LEN] = { 0 };
    for(uint32_t i=0; i<sizeof(incoming_header); i++) {
        incoming_header_str[i] = incoming_header[i] ? incoming_header[i] : 0xff;
    }

    // verify header
    char* fmt_str = "";
    if(target_mask & (1 << HTTP_OTA_UPDATE_TYPE_MUSIC)) {
        fmt_str = "ESP_TONE_BIN";
    } else if(target_mask & (1 << HTTP_OTA_UPDATE_TYPE_MODEL)) {
        fmt_str = "MODEL_INFO";
    }
    if(strstr(incoming_header_str, fmt_str) == NULL) {
        ESP_LOGE(TAG, "partition [%s] header not match, file format is error!", node->label);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }
    
    // write flash
    if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
        ESP_LOGE(TAG, "Erase [%s] failed", node->label);
        return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
    }
    ota_data_partition_erase_mark(handle);
    ota_data_partition_write(handle, (char *)&incoming_header, sizeof(incoming_header));
    
    ESP_LOGI(TAG, "partition need upgrade, label: %s", node->label);
    return OTA_SERV_ERR_REASON_SUCCESS;
}

static ota_service_err_reason_t _http_ota_ops_app_need_upgrade(void *handle, ota_node_attr_t *node)
{
    http_ota_desc_lock();
    s_ota_desc.is_app_succ = false;
    http_ota_desc_unlock();

    ota_app_upgrade_ctx_t *context = (ota_app_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context->ota_handle, return OTA_SERV_ERR_REASON_NULL_POINTER);
    esp_app_desc_t update_desc;
    esp_err_t err = context->get_img_desc(context->ota_handle, &update_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get_img_desc failed");
        return OTA_SERV_ERR_REASON_GET_NEW_APP_DESC_FAIL;
    }
    
    const esp_app_desc_t* local_desc = esp_app_get_description();
    if(strcmp(local_desc->project_name, update_desc.project_name) != 0) {
        ESP_LOGE(TAG, "project name not match, local project name: %s, update project name: %s", local_desc->project_name, update_desc.project_name);
        return OTA_SERV_ERR_REASON_ERROR_PROJECT_NAME;
    }

    http_ota_desc_lock();
    s_ota_desc.is_app_succ = true;
    http_ota_desc_unlock();
    return OTA_SERV_ERR_REASON_SUCCESS;
}


static ota_service_err_reason_t _http_ota_ops_exec_upgrade(void *handle, ota_node_attr_t *node)
{
    int r_size = 0;
    http_ota_upgrade_ctx_t *context = (http_ota_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context->r_stream, return OTA_SERV_ERR_REASON_NULL_POINTER);

    http_ota_update_type_t type = http_ota_get_node_type(node);
    if(type >= HTTP_OTA_UPDATE_TYPE_MAX) {
        ESP_LOGE(TAG, "exec upgrade type error");
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }

    http_ota_upgrade_pkt_cb_t exec_upgrade_pkt_cb = s_ota_desc.upgrade_pkt_cb[type];
    if(exec_upgrade_pkt_cb == NULL) {
        ESP_LOGE(TAG, "exec upgrade packet cb is null");
        return OTA_SERV_ERR_REASON_NULL_POINTER;
    }

    http_ota_finished_check_cb_t exec_finished_check = s_ota_desc.finished_check_cb[type];
    if(exec_finished_check == NULL) {
        ESP_LOGE(TAG, "exec finished check cb is null");
        return OTA_SERV_ERR_REASON_NULL_POINTER;
    }

    while ((r_size = audio_element_input(context->r_stream, context->read_buf, UPGRADE_READER_BUF_LEN)) > 0) {
        ESP_LOGI(TAG, "write_offset %d, r_size %d", context->write_offset, r_size);
        if (exec_upgrade_pkt_cb((uint8_t*)context->read_buf, r_size) == true) {
            context->write_offset += r_size;
        } else {
            ESP_LOGE(TAG, "upgrade packet failed");
            return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
        }
    }
    if (r_size == AEL_IO_OK || r_size == AEL_IO_DONE) {
        if(exec_finished_check() == false) {
            ESP_LOGE(TAG, "exec finish check failed");
            return OTA_SERV_ERR_REASON_STREAM_RD_FAIL;
        }
        return OTA_SERV_ERR_REASON_SUCCESS;
    } else {
        return OTA_SERV_ERR_REASON_STREAM_RD_FAIL;
    }
}

static ota_service_err_reason_t _http_ota_ops_finish_check(void *handle, ota_node_attr_t *node, ota_service_err_reason_t result)
{
    http_ota_upgrade_ctx_t *context = (http_ota_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, context, return ESP_FAIL);
    if (context->r_stream) {
        audio_element_process_deinit(context->r_stream);
        audio_element_deinit(context->r_stream);
    }
    audio_free(handle);
    return result;
}

bool http_ota_set_prepare_cb(http_ota_update_type_t type, http_ota_prepare_cb_t cb)
{
    if(s_ota_desc.is_update) {
        ESP_LOGE(TAG, "ota is update");
        return false;
    }

    http_ota_desc_lock();
    s_ota_desc.prepare_cb[type] = cb;
    s_ota_desc.upgrade_list[type].prepare = _http_ota_ops_prepare;
    http_ota_desc_unlock();
    return true;
}

bool http_ota_set_need_upgrade_cb(http_ota_update_type_t type, http_ota_need_upgrade_cb_t cb)
{
    if(s_ota_desc.is_update) {
        ESP_LOGE(TAG, "ota is update");
        return false;
    }

    http_ota_desc_lock();
    s_ota_desc.need_upgrade_cb[type] = cb;
    s_ota_desc.upgrade_list[type].need_upgrade = _http_ota_ops_need_upgrade;
    http_ota_desc_unlock();
    return true;
}

bool http_ota_set_upgrade_pkt_cb(http_ota_update_type_t type, http_ota_upgrade_pkt_cb_t cb)
{
    if(s_ota_desc.is_update) {
        ESP_LOGE(TAG, "ota is update");
        return false;
    }

    http_ota_desc_lock();
    s_ota_desc.upgrade_pkt_cb[type] = cb;
    s_ota_desc.upgrade_list[type].execute_upgrade = _http_ota_ops_exec_upgrade;
    http_ota_desc_unlock();
    return true;
}

bool http_ota_set_finished_check_cb(http_ota_update_type_t type, http_ota_finished_check_cb_t cb)
{
    if(s_ota_desc.is_update) {
        ESP_LOGE(TAG, "ota is update");
        return false;
    }

    http_ota_desc_lock();
    s_ota_desc.finished_check_cb[type] = cb;
    s_ota_desc.upgrade_list[type].finished_check = _http_ota_ops_finish_check;
    http_ota_desc_unlock();
    return true;
}

bool http_ota_set_status_cb(http_ota_upgrade_status_cb_t cb)
{
    if(s_ota_desc.is_update) {
        ESP_LOGE(TAG, "ota is update");
        return false;
    }

    http_ota_desc_lock();
    s_ota_desc.status_sb = cb;
    http_ota_desc_unlock();
    return true;
}

bool http_ota_upgrade(void)
{
    if(s_ota_desc.is_update) {
        ESP_LOGE(TAG, "ota is update");
        return false;
    }

    http_ota_desc_lock();
    ota_upgrade_ops_t* upgrade_list = &s_ota_desc.current_list[0];
    uint32_t upgrade_num = 0;

    for(http_ota_update_type_t type = 0; type < HTTP_OTA_UPDATE_TYPE_MAX; type++) {
        if(s_ota_desc.upgrade_list[type].node.uri != NULL) {
            upgrade_list[upgrade_num++] = s_ota_desc.upgrade_list[type];
        }
    }
    http_ota_desc_unlock();

    if(upgrade_num == 0) {
        ESP_LOGW(TAG, "no upgrade list");
        return false;
    }

    for(int i = 0; i < upgrade_num; i++) {
        printf("List_id: %d, Lable: %s, Uri: %s\n", i, upgrade_list[i].node.label, upgrade_list[i].node.uri);
    }

    esp_err_t err = ota_service_set_upgrade_param(s_ota_desc.handle, upgrade_list, upgrade_num);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "set upgrade param failed");
        return false;
    }

    err = periph_service_start(s_ota_desc.handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "start ota service failed");
        return false;
    }

    http_ota_desc_lock();
    s_ota_desc.is_update = true;
    http_ota_desc_unlock();
    ESP_LOGI(TAG, "http ota upgrade");
    return true;
}

bool http_ota_is_update(void)
{
    return s_ota_desc.is_update;
}





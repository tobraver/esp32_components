#include "http_upgrade.h"

#include <string.h>

#include "audio_mem.h"
#include "esp_https_ota.h"
#include "esp_fs_ota.h"
#include "esp_log.h"
#include "fatfs_stream.h"
#include "http_stream.h"

#include "ota_proc_default.h"
#include "mbedtls/md5.h"

#define READER_BUF_LEN (1024 * 2)

typedef struct {
    audio_element_handle_t r_stream;
    const esp_partition_t *partition;
    char read_buf[READER_BUF_LEN];
    uint32_t wrote_size;
    // uint32_t total_size;
    bool need_resume;
    uint32_t resume_offset;
    uint32_t not_change;
} http_upgrade_ctx_t;

typedef struct {
    void *ota_handle;
    esp_err_t (*get_img_desc)(void *handle, esp_app_desc_t *new_app_info);
    esp_err_t (*perform)(void *handle);
    int (*get_image_len_read)(void *handle);
    bool (*all_read)(void *handle);
    esp_err_t (*finish)(void *handle);
} ota_app_upgrade_ctx_t;

// http upgrade status
#define HTTP_UPGRADE_STATUS_NEED_RESUME     (BIT0)
#define HTTP_UPGRADE_STATUS_OTA_COMPLETE    (BIT1)

typedef struct {
    uint32_t offset;
    uint32_t status;
} http_upgrade_resume_t;

typedef struct {
    http_upgrade_read_resume_t read_resume;
    http_upgrade_write_resume_t write_resume;
    http_upgrade_read_md5_t read_md5;
    http_upgrade_write_md5_t write_md5;
    http_upgrade_event_cb_t event_cb;
} http_upgrade_desc_t;

static http_upgrade_desc_t s_upgrade_desc;

static const char *TAG = "http_upgrade";

#define HTTP_UPGRADE_OTA_CPMPLETE_CHECK(context, node, finish) \
{ \
    if(context->not_change == true) { \
        ESP_LOGW(TAG, "(%s) partition %s not change, do not upgrade.", __FUNCTION__, node->label); \
        if(finish) { \
            audio_free(context); \
        } \
        return OTA_SERV_ERR_REASON_SUCCESS; \
    } \
}

static esp_err_t _http_upgrade_get_url_md5(const char* url, char md5[33]);
static esp_err_t _http_upgrade_get_partition_md5(const esp_partition_t *partition, uint32_t offset, uint32_t size, char md5[33]);

static http_upgrade_resume_t _http_upgrade_get_resume(const char* label)
{
    uint64_t value = 0;
    http_upgrade_resume_t resume = { 0 };
    if(s_upgrade_desc.read_resume) {
        if(s_upgrade_desc.read_resume(label, &value) == ESP_OK) {
            resume.offset = value & 0xFFFFFFFF;
            resume.status = (value >> 32) & 0xFFFFFFFF;
        }
    }
    return resume;
}

static esp_err_t _http_upgrade_set_resume(const char* label, http_upgrade_resume_t resume)
{
    uint64_t value = 0;
    esp_err_t ret = ESP_OK;
    if(s_upgrade_desc.write_resume) {
        value = ((uint64_t)resume.status << 32) | resume.offset;
        ret = s_upgrade_desc.write_resume(label, value);
    }
    return ret;
}

static esp_err_t _http_upgrade_get_md5(const char* label, char md5[33])
{
    esp_err_t ret = ESP_FAIL;
    if(label == NULL || md5 == NULL) {
        return ret;
    }
    if(s_upgrade_desc.read_md5) {
        ret = s_upgrade_desc.read_md5(label, md5);
    }
    return ret;
}

static esp_err_t _http_upgrade_set_md5(const char* label, char md5[33])
{
    esp_err_t ret = ESP_FAIL;
    if(label == NULL || md5 == NULL) {
        return ret;
    }
    if(s_upgrade_desc.write_md5) {
        ret = s_upgrade_desc.write_md5(label, md5);
    }
    return ret;
}

static esp_err_t _http_upgrade_event_cb(const char* label, http_upgrade_event_t* event)
{
    esp_err_t ret = ESP_FAIL;
    if(label == NULL || event == NULL) {
        return ret;
    }
    if(s_upgrade_desc.event_cb) {
        ret = s_upgrade_desc.event_cb(label, event);
    }
    return ret;
}

static esp_err_t _http_upgrade_enter_event_cb(const char* label)
{
    http_upgrade_event_t event = {
        .type = HTTP_UPGRADE_EVENT_TYPE_ENTER,
    };
    return _http_upgrade_event_cb(label, &event);
}

static esp_err_t _http_upgrade_download_event_cb(const char* label, uint32_t total, uint32_t wrote)
{
    uint32_t progress = wrote * 100 / total;
    http_upgrade_event_t event = {
        .type = HTTP_UPGRADE_EVENT_TYPE_DOWNLOAD,
        .data = &progress,
        .len = sizeof(progress),
    };
    return _http_upgrade_event_cb(label, &event);
}

static esp_err_t _http_upgrade_resume_event_cb(const char* label, uint32_t offset)
{
    http_upgrade_event_t event = {
        .type = HTTP_UPGRADE_EVENT_TYPE_RESUME,
        .data = &offset,
        .len = sizeof(offset),
    };
    return _http_upgrade_event_cb(label, &event);
}

static esp_err_t _http_upgrade_md5_event_cb(const char* label, uint32_t success)
{
    http_upgrade_event_t event = {
        .type = HTTP_UPGRADE_EVENT_TYPE_MD5,
        .data = &success,
        .len = sizeof(success),
    };
    return _http_upgrade_event_cb(label, &event);
}

static const esp_partition_t* _http_upgrade_find_partition(ota_node_attr_t *node)
{
    const esp_partition_t* partition = NULL;
    const esp_partition_t* running = NULL;
    if(node == NULL) {
        return partition;
    }
    if(node->label == NULL) {
        return partition;
    }
    if(node->type == ESP_PARTITION_TYPE_APP) {
        running = esp_ota_get_running_partition();
        partition = esp_ota_get_next_update_partition(NULL);
        if(partition == running) {
            partition = NULL;
            ESP_LOGE(TAG, "app upgrade partition error, write or erase the current running partition");
        }
    } else {
        partition = esp_partition_find_first(node->type, ESP_PARTITION_SUBTYPE_ANY, node->label);
    }
    if(running) {
        ESP_LOGI(TAG, "running partition %s, address: 0x%x, size: 0x%x", running->label, running->address, running->size);
    }
    if(partition) {
        ESP_LOGI(TAG, "upgrade partition %s, address: 0x%x, size: 0x%x", partition->label, partition->address, partition->size);
    }
    return partition;
}

static int _http_upgrade_event_handle(http_stream_event_msg_t *msg)
{
    http_upgrade_ctx_t *context = (http_upgrade_ctx_t *)msg->user_data;
    AUDIO_NULL_CHECK(TAG, context, return ESP_FAIL);

    if(msg->event_id == HTTP_STREAM_PRE_REQUEST) {
        char range[64] = {0};
        snprintf(range, sizeof(range), "bytes=%u-", context->resume_offset);
        // printf("## range: %s\n", range);
        esp_http_client_set_header(msg->http_client, "Range", range);
    }
    return ESP_OK;
}

static ota_service_err_reason_t http_upgrade_partition_prepare(void **handle, ota_node_attr_t *node)
{
    *handle = NULL;
    http_upgrade_ctx_t *context = audio_calloc(1, sizeof(http_upgrade_ctx_t));
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, node->label, {
                    audio_free(context);
                    return OTA_SERV_ERR_REASON_NULL_POINTER;
                    });
    context->partition = _http_upgrade_find_partition(node);
    if (context->partition == NULL) {
        ESP_LOGE(TAG, "partition %s not found", node->label);
        audio_free(context);
        return OTA_SERV_ERR_REASON_PARTITION_NOT_FOUND;
    }
    AUDIO_NULL_CHECK(TAG, node->uri, {
                    audio_free(context);
                    return OTA_SERV_ERR_REASON_NULL_POINTER;
                    });
    ESP_LOGI(TAG, "partition %s upgrade uri %s", node->label, node->uri);

    bool cancel_resume = true;
    char md5_1[33] = {0}, md5_2[33] = {0};
    if((_http_upgrade_get_url_md5(node->uri, md5_1) == ESP_OK) && \
        (_http_upgrade_get_md5(node->label, md5_2) == ESP_OK)) {
        if(strcmp(md5_1, md5_2) == 0) { // same download
            cancel_resume = false;
        }
    }
    if(cancel_resume) {
        http_upgrade_cancel_resume(node->label);
        ESP_LOGW(TAG, "partition %s find lastest url.", node->label);
    }
    _http_upgrade_set_md5(node->label, md5_1);

    http_upgrade_resume_t resume = _http_upgrade_get_resume(node->label);
    if(resume.status & HTTP_UPGRADE_STATUS_OTA_COMPLETE) {
        memset(md5_2, 0, sizeof(md5_2));
        _http_upgrade_get_partition_md5(context->partition, 0, resume.offset, md5_2);
        if(strcmp(md5_1, md5_2) == 0) {
            ESP_LOGW(TAG, "(%s) partition %s not change, do not upgrade.", __FUNCTION__, node->label);
            context->not_change = true;
            *handle = context; // upgrade context
            return OTA_SERV_ERR_REASON_SUCCESS;
        } else {
            ESP_LOGE(TAG, "partition %s complete, but md5 not match.", node->label);
        }
    }
    if(resume.status & HTTP_UPGRADE_STATUS_NEED_RESUME) {
        context->need_resume = true;
        context->resume_offset = resume.offset;
        ESP_LOGW(TAG, "partition %s need resume, offset: %u", node->label, context->resume_offset);
    }

    if (strstr(node->uri, "file://")) {
        fatfs_stream_cfg_t fs_cfg = FATFS_STREAM_CFG_DEFAULT();
        fs_cfg.type = AUDIO_STREAM_READER;

        context->r_stream = fatfs_stream_init(&fs_cfg);
    } else if (strstr(node->uri, "https://") || strstr(node->uri, "http://")) {
        http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
        http_cfg.type = AUDIO_STREAM_READER;
        http_cfg.user_data = context;
        http_cfg.event_handle = _http_upgrade_event_handle;

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

    *handle = context; // upgrade context
    return OTA_SERV_ERR_REASON_SUCCESS;
}

static ota_service_err_reason_t _http_upgrade_partition_write(http_upgrade_ctx_t *context, size_t r_size)
{
    esp_err_t ret = ESP_OK;
    uint32_t write_offset = context->wrote_size + context->resume_offset;
    // must erase the partition before writing to it
    if(write_offset + r_size < context->partition->size) {
        uint32_t first_sector = write_offset / SPI_FLASH_SEC_SIZE;
        uint32_t last_sector = (write_offset + r_size) / SPI_FLASH_SEC_SIZE;
        if ((write_offset % SPI_FLASH_SEC_SIZE) == 0) {
            ret = esp_partition_erase_range(context->partition, write_offset, ((last_sector - first_sector) + 1) * SPI_FLASH_SEC_SIZE);
        } else if (first_sector != last_sector) {
            ret = esp_partition_erase_range(context->partition, (first_sector + 1) * SPI_FLASH_SEC_SIZE, (last_sector - first_sector) * SPI_FLASH_SEC_SIZE);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "(%s) partition %s erase failed, error: %s", __FUNCTION__, context->partition->label, esp_err_to_name(ret));
            return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
        }
    }
    // write data to partition
    if (esp_partition_write(context->partition, write_offset, context->read_buf, r_size) == ESP_OK) {
        context->wrote_size += r_size;
        return OTA_SERV_ERR_REASON_SUCCESS;
    } else {
        ESP_LOGE(TAG, "(%s) partition %s write failed", __FUNCTION__, context->partition->label);
        return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
    }
}

static ota_service_err_reason_t http_upgrade_partition_need_upgrade(void *handle, ota_node_attr_t *node)
{
    http_upgrade_ctx_t *context = (http_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, node, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    HTTP_UPGRADE_OTA_CPMPLETE_CHECK(context, node, false); // if complete
    AUDIO_NULL_CHECK(TAG, context->r_stream, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context->partition, return OTA_SERV_ERR_REASON_NULL_POINTER);

    if(context->need_resume && context->resume_offset){
        ESP_LOGI(TAG, "need resume and upgrade, skip header verify.");
        return OTA_SERV_ERR_REASON_SUCCESS;
    }

    bool is_skip = false;
    const char* find_str = "";
    if(strstr(node->label, HTTP_UPGRADE_WEB_LABEL)) {
        find_str = "index.html";
    } else if(strstr(node->label, HTTP_UPGRADE_MODEL_LABEL)) {
        find_str = "MODEL_INFO";
    } else if(strstr(node->label, HTTP_UPGRADE_MUSIC_LABEL)) {
        find_str = "ESP_TONE_BIN";
    } else if(strstr(node->label, HTTP_UPGRADE_APP_LABEL)) {
        find_str = esp_ota_get_app_description()->project_name;
        if(strstr(find_str, "xuean")) {
            is_skip = true;
        }
    }

    // read header from incoming stream
    uint8_t incoming_header[READER_BUF_LEN] = { 0 };
    if (ota_data_image_stream_read(handle, (char *)incoming_header, sizeof(incoming_header)) != ESP_OK) {
        ESP_LOGE(TAG, "read partition %s incoming header failed, size: %d", node->label, sizeof(incoming_header));
        return OTA_SERV_ERR_REASON_STREAM_RD_FAIL;
    }
    for(uint32_t i=0; i<sizeof(incoming_header); i++) {
        context->read_buf[i] = incoming_header[i] ? incoming_header[i] : 0xff;
    }
    context->read_buf[READER_BUF_LEN-1] = '\0';

    if(!is_skip && strstr(context->read_buf, find_str) == NULL) {
        ESP_LOGE(TAG, "partition %s header not match, want %s, file format error.", node->label, find_str);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }

    // write incoming header to partition
    memcpy(context->read_buf, incoming_header, READER_BUF_LEN);
    if(_http_upgrade_partition_write(context, READER_BUF_LEN) != OTA_SERV_ERR_REASON_SUCCESS) {
        return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
    }
    _http_upgrade_enter_event_cb(node->label);
    return OTA_SERV_ERR_REASON_SUCCESS;
}

static ota_service_err_reason_t http_upgrade_partition_exec_upgrade(void *handle, ota_node_attr_t *node)
{
    int r_size = 0;
    http_upgrade_ctx_t *context = (http_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    HTTP_UPGRADE_OTA_CPMPLETE_CHECK(context, node, false); // if complete
    AUDIO_NULL_CHECK(TAG, context->r_stream, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context->partition, return OTA_SERV_ERR_REASON_NULL_POINTER);

    audio_element_info_t el_info = { 0 };
    audio_element_getinfo(context->r_stream, &el_info);
    int total_bytes = el_info.total_bytes;
    ESP_LOGI(TAG, "upgrade partition %s, total bytes: %d", context->partition->label, total_bytes);

    // featch package from server
    while ((r_size = audio_element_input(context->r_stream, context->read_buf, READER_BUF_LEN)) > 0) {
        // ESP_LOGI(TAG, "write %d, r_size %d", context->wrote_size, r_size);
        if(_http_upgrade_partition_write(context, r_size) != OTA_SERV_ERR_REASON_SUCCESS) {
            return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
        }
        _http_upgrade_download_event_cb(node->label, total_bytes, context->wrote_size);
    }
    if (r_size == AEL_IO_OK || r_size == AEL_IO_DONE) {
        if(total_bytes != context->wrote_size) {
            ESP_LOGE(TAG, "partition %s upgrade failed, incomplete image, write %d, total: %d", context->partition->label, context->wrote_size, total_bytes);
            return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
        }
        ESP_LOGI(TAG, "partition %s download successes", node->label);
        return OTA_SERV_ERR_REASON_SUCCESS;
    } else {
        return OTA_SERV_ERR_REASON_STREAM_RD_FAIL;
    }
}

// e.g. http://host/upgrade.bin?&filemd5=202ce7416061bc484d44f3667117826b
static esp_err_t _http_upgrade_get_url_md5(const char* url, char md5[33])
{
    if(url == NULL) {
        ESP_LOGE(TAG, "(%s) url is null", __FUNCTION__);
        return ESP_FAIL;
    }
    char* find = "filemd5=";
    char* sub = strstr(url, find);
    if(sub == NULL) {
        ESP_LOGE(TAG, "url %s not find file md5.", url);
        return ESP_FAIL;
    }
    if(strlen(sub) - strlen(find) != 32) {
        ESP_LOGE(TAG, "url %s md5 length error", url);
        return ESP_FAIL;
    }
    md5[32] = '\0';
    memcpy(md5, sub + strlen(find), 32);
    return ESP_OK;
}

static esp_err_t _http_upgrade_get_partition_md5(const esp_partition_t *partition, uint32_t offset, uint32_t size, char md5[33])
{
    unsigned char output[16];
    unsigned char buffer[512];
    ESP_LOGI(TAG, "get partition %s md5, offset: %d, size: %d", partition->label, offset, size);
    if(size < sizeof(buffer)) {
        return ESP_FAIL;
    }
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    int i = 0;
    for(i = 0; i < (size - sizeof(buffer)); i += sizeof(buffer)) {
        if(esp_partition_read(partition, offset + i, buffer, sizeof(buffer)) != ESP_OK) {
            ESP_LOGE(TAG, "(%s)(%d) partition %s read failed, offset: %d", __FUNCTION__, __LINE__, partition->label, offset + i);
            return ESP_FAIL;
        }
        mbedtls_md5_update(&ctx, buffer, sizeof(buffer));
    }
    ESP_LOGI(TAG, "partition %s md5, remain: %d", partition->label, size - i);
    if(esp_partition_read(partition, offset + i, buffer, size - i) != ESP_OK) {  // remain
        ESP_LOGE(TAG, "(%s)(%d) partition %s read failed, offset: %d", __FUNCTION__, __LINE__, partition->label, offset + i);
        return ESP_FAIL;
    }
    mbedtls_md5_update(&ctx, buffer, size - i);
    mbedtls_md5_finish(&ctx, output);
    for(int j = 0; j < sizeof(output); j++) {
        sprintf(&md5[j * 2], "%02x", output[j]);
    }
    return ESP_OK;
}

static ota_service_err_reason_t ota_data_partition_verify_md5(void *handle, ota_node_attr_t *node)
{
    http_upgrade_ctx_t *context = (http_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, node, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context->partition, return OTA_SERV_ERR_REASON_NULL_POINTER);

    char md5_1[33] = { 0 }, md5_2[33] = { 0 };
    if(_http_upgrade_get_url_md5(node->uri, md5_1) != ESP_OK) {
        ESP_LOGE(TAG, "url (%s), get file md5 failed!", node->uri);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }

    uint32_t offset = 0, size = context->wrote_size + context->resume_offset;
    if(_http_upgrade_get_partition_md5(context->partition, offset, size, md5_2) != ESP_OK) {
        ESP_LOGE(TAG, "get partition (%s) md5 failed!", context->partition->label);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }

    ESP_LOGI(TAG, "md5_1: %s, md5_2: %s", md5_1, md5_2);
    if(strcmp(md5_1, md5_2)) {
        ESP_LOGE(TAG, "partition %s md5 verify failed!", context->partition->label);
        _http_upgrade_md5_event_cb(node->label, 0);
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }
    ESP_LOGI(TAG, "partition %s md5 verify success!", context->partition->label);
    _http_upgrade_md5_event_cb(node->label, 1);
    
    if(node->type != ESP_PARTITION_TYPE_APP) {
        http_upgrade_resume_t resume = _http_upgrade_get_resume(node->label);
        resume.status |= HTTP_UPGRADE_STATUS_OTA_COMPLETE;
        _http_upgrade_set_resume(node->label, resume);
        ESP_LOGW(TAG, "partition %s upgrade complete.", context->partition->label);
    }
    return OTA_SERV_ERR_REASON_SUCCESS;
}

static ota_service_err_reason_t ota_data_partition_verify(void *handle, ota_node_attr_t *node)
{
    http_upgrade_ctx_t *context = (http_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, node, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context->partition, return OTA_SERV_ERR_REASON_NULL_POINTER);
    
    if(ota_data_partition_verify_md5(handle, node) != OTA_SERV_ERR_REASON_SUCCESS) {
        return OTA_SERV_ERR_REASON_UNKNOWN;
    }
    if(node->type != ESP_PARTITION_TYPE_APP) {
        return OTA_SERV_ERR_REASON_SUCCESS;
    }
    esp_image_metadata_t data;
    const esp_partition_pos_t part_pos = {
        .offset = context->partition->address,
        .size = context->partition->size,
    };
    if(esp_image_verify(ESP_IMAGE_VERIFY, &part_pos, &data) != ESP_OK) {
        ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
    }
    ESP_LOGI(TAG, "Image verification success.");
    if(esp_ota_set_boot_partition(context->partition) != ESP_OK) {
        ESP_LOGE(TAG, "Update boot partition failed!");
        return OTA_SERV_ERR_REASON_PARTITION_WT_FAIL;
    }
    ESP_LOGI(TAG, "Update success, wait reboot...");
    return OTA_SERV_ERR_REASON_SUCCESS;
}

static ota_service_err_reason_t ota_data_partition_finish(void *handle, ota_node_attr_t *node, ota_service_err_reason_t result)
{
    http_upgrade_ctx_t *context = (http_upgrade_ctx_t *)handle;
    AUDIO_NULL_CHECK(TAG, node, return OTA_SERV_ERR_REASON_NULL_POINTER);
    AUDIO_NULL_CHECK(TAG, context, return OTA_SERV_ERR_REASON_NULL_POINTER);
    HTTP_UPGRADE_OTA_CPMPLETE_CHECK(context, node, true); // if complete
    AUDIO_NULL_CHECK(TAG, context->r_stream, return OTA_SERV_ERR_REASON_NULL_POINTER);
    if (context->r_stream) {
        audio_element_process_deinit(context->r_stream);
        audio_element_deinit(context->r_stream);
    }
    http_upgrade_resume_t resume = { 0 };  // success
    resume.offset = context->wrote_size + context->resume_offset;
    if(result != OTA_SERV_ERR_REASON_SUCCESS && context->wrote_size) {
        resume.status = HTTP_UPGRADE_STATUS_NEED_RESUME;
        resume.offset = (resume.offset >> 12) << 12; // 4k align
        ESP_LOGW(TAG, "partition %s upgrade break, offset: %d, write: %d, resume: %d, enter resume mode.", node->label, context->resume_offset, context->wrote_size, resume.offset);
        _http_upgrade_resume_event_cb(node->label, resume.offset);
    }
    _http_upgrade_set_resume(node->label, resume);
    if(result == OTA_SERV_ERR_REASON_SUCCESS) {
        result = ota_data_partition_verify(context, node);
    }
    audio_free(handle);
    return result;
}

esp_err_t http_upgrade_get_default_proc(ota_upgrade_ops_t *ops)
{
    if(ops == NULL) {
        ESP_LOGE(TAG, "(%s) ops is null", __FUNCTION__);
        return ESP_FAIL;
    }
    ops->prepare = http_upgrade_partition_prepare;
    ops->need_upgrade = http_upgrade_partition_need_upgrade;
    ops->execute_upgrade = http_upgrade_partition_exec_upgrade;
    ops->finished_check = ota_data_partition_finish;
    return ESP_OK;
}

esp_err_t http_upgrade_init(http_upgrade_config_t config)
{
    s_upgrade_desc.read_resume = config.read_resume;
    s_upgrade_desc.write_resume = config.write_resume;
    s_upgrade_desc.read_md5 = config.read_md5;
    s_upgrade_desc.write_md5 = config.write_md5;
    s_upgrade_desc.event_cb = config.event_cb;
    return ESP_OK;
}

esp_err_t http_upgrade_cancel_resume(const char *label)
{
    esp_err_t ret = ESP_FAIL;
    if(label == NULL) {
        ESP_LOGE(TAG, "(%s) label is null", __FUNCTION__);
        return ret;
    }
    http_upgrade_resume_t resume = {0};
    ret = _http_upgrade_set_resume(label, resume);
    ESP_LOGW(TAG, "partition %s upgrade resume cancel.", label);
    return ret;
}

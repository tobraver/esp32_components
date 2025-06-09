#include "file_svr.h"
#include "string.h"
#include "stdint.h"
#include "esp_log.h"
#include "sys/time.h"

static const char *TAG = "file_svr";

// root dir
#define FILE_SVR_ROOT_DIR "/sdcard"
// file desc path
#define FILE_SVR_DESC_PATH "/log_desc"

esp_err_t file_svr_get_desc(file_svr_desc_t* desc)
{
    if(desc == NULL) {
        return ESP_FAIL;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s%s", FILE_SVR_ROOT_DIR, FILE_SVR_DESC_PATH);
    FILE* fp = fopen(path, "rb");
    if(fp == NULL) {
        ESP_LOGE(TAG, "open file %s failed.", path);
        return ESP_FAIL;
    }
    size_t r_size = fread(desc, 1, sizeof(file_svr_desc_t), fp);
    if(r_size != sizeof(file_svr_desc_t)) {
        fclose(fp);
        ESP_LOGE(TAG, "read desc failed, read size: %d.", r_size);
        return ESP_FAIL;
    }
    fclose(fp);
    return ESP_OK;
}

esp_err_t file_svr_set_desc(file_svr_desc_t* desc)
{
    if(desc == NULL) {
        return ESP_FAIL;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s%s", FILE_SVR_ROOT_DIR, FILE_SVR_DESC_PATH);
    FILE* fp = fopen(path, "wb");
    if(fp == NULL) {
        ESP_LOGE(TAG, "open file %s failed.", path);
        return ESP_FAIL;
    }
    fwrite(desc, 1, sizeof(file_svr_desc_t), fp);
    fclose(fp);
    ESP_LOGI(TAG, "set desc success");
    return ESP_OK;
}

uint64_t _file_svr_get_current_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

esp_err_t file_svr_open(file_svr_desc_t* desc, char* mode)
{
    if(desc == NULL || mode == NULL) {
        return ESP_FAIL;
    }
    char path[64];
    uint32_t index = desc->cur_index % FILE_SVR_PATH_MAX_NUM; // 0 ~ FILE_SVR_PATH_MAX_NUM
    snprintf(path, sizeof(path), "%s/log_%ld", FILE_SVR_ROOT_DIR, index);

    if(strstr(mode, "w")) {
        desc->m_modify_time[index] = _file_svr_get_current_time();
    }

    desc->m_fp = NULL;
    FILE* fp = fopen(path, mode);
    if(fp == NULL) {
        ESP_LOGE(TAG, "open file %s failed.", path);
        return ESP_FAIL;
    }
    desc->m_fp = fp;
    ESP_LOGI(TAG, "open file %s success", path);
    return ESP_OK;
}

esp_err_t file_svr_read(file_svr_desc_t* desc, uint8_t* buff, size_t* len)
{
    if(buff == NULL || len == NULL) {
        return ESP_FAIL;
    }

    size_t want_size = *len;
    *len = fread(buff, 1, want_size, desc->m_fp);
    if(feof(desc->m_fp) == 0) {
        return ESP_OK;
    }
    return ferror(desc->m_fp) ? ESP_FAIL : ESP_OK;
}

esp_err_t file_svr_write(file_svr_desc_t* desc, uint8_t* buff, size_t len)
{
    if(desc == NULL) {
        return ESP_FAIL;
    }
    if(desc->m_fp == NULL) {
        return ESP_FAIL;
    }
    fwrite(buff, 1, len, desc->m_fp);
    return ferror(desc->m_fp) ? ESP_FAIL : ESP_OK;
}

size_t file_svr_size(file_svr_desc_t* desc)
{
    size_t file_size = 0;
    if(desc == NULL) {
        return file_size;
    }
    if(desc->m_fp == NULL) {
        return file_size;
    }
    if(fseek(desc->m_fp, 0L, SEEK_END) < 0) {
        return file_size;
    }
    file_size = ftell(desc->m_fp);
    fseek(desc->m_fp, 0L, SEEK_SET);
    return file_size;
}

esp_err_t file_svr_close(file_svr_desc_t* desc)
{
    if(desc == NULL) {
        return ESP_FAIL;
    }
    if(desc->m_fp == NULL) {
        return ESP_FAIL;
    }
    fclose(desc->m_fp);
    desc->m_fp = NULL;
    return ESP_OK;
}

esp_err_t file_svr_begin(file_svr_desc_t* desc)
{
    if(desc == NULL) {
        return ESP_FAIL;
    }
    desc->cur_index = 0;
    return ESP_OK;
}

static uint32_t file_svr_get_index(file_svr_desc_t* desc) {
    uint32_t min_index = 0;
    uint64_t modify_time[FILE_SVR_PATH_MAX_NUM] = { 0 };
    for(int i = 0; i < FILE_SVR_PATH_MAX_NUM; i++) {
        modify_time[i] = desc->m_modify_time[i];
    }
    // bubble sort
    for (int i = FILE_SVR_PATH_MAX_NUM - 1; i > 0; i--) {
        for (int j = 0; j < i; j++) {
            if (modify_time[j] > modify_time[j + 1]) {
                uint64_t temp = modify_time[j];
                modify_time[j] = modify_time[j + 1];
                modify_time[j + 1] = temp;
            }
        }
    }
    uint64_t min_modify_time = modify_time[0];
    for(int i = 0; i < FILE_SVR_PATH_MAX_NUM; i++) {
        if(desc->m_modify_time[i] == min_modify_time) {
            min_index = i;
        }
    }
    return min_index;
}

esp_err_t file_svr_next(file_svr_desc_t* desc)
{
    if(desc == NULL) {
        return ESP_FAIL;
    }
    desc->cur_index = file_svr_get_index(desc);
    return ESP_OK;
}

esp_err_t file_svr_update(file_svr_desc_t* desc, uint32_t index)
{
    if(desc == NULL) {
        return ESP_FAIL;
    }
    desc->cur_index = index;
    return ESP_OK;
}



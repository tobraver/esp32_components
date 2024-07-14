#include "prefs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "string.h"

#if CONFIG_ESP32S3_SPIRAM_SUPPORT
#include "esp_dispatcher.h"
#include "nvs_action.h"

static esp_dispatcher_handle_t s_dispatcher = NULL;
#endif

#if !CONFIG_ESP32S3_SPIRAM_SUPPORT
bool prefs_init(prefs_t hprefs)
{
    const char* TAG = "prefs_init";
    esp_err_t error = nvs_flash_init_partition(hprefs.part_name);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem %s init failed, error %s", hprefs.part_name, esp_err_to_name(error));
        return false;
    }
    ESP_LOGI(TAG, "mem %s init success!", hprefs.part_name);
    return true;
}

bool prefs_get_stats(prefs_t hprefs, uint32_t* used, uint32_t* total)
{
    const char* TAG = "prefs_get_stats";
    nvs_stats_t nvs_stats;
    esp_err_t error = nvs_get_stats(hprefs.part_name, &nvs_stats);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "get stats failed, error %s", esp_err_to_name(error));
        return false;
    }
    *used = nvs_stats.used_entries;
    *total = nvs_stats.total_entries;
    return true;
}

bool prefs_erase_partition(prefs_t hprefs)
{
    const char* TAG = "prefs_erase_partition";
    esp_err_t error = nvs_flash_erase_partition(hprefs.part_name);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem erase failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_erase_namespace(prefs_t hprefs)
{
    const char* TAG = "prefs_erase_namespace";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READWRITE, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem erase namespace open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem erase namespace failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_erase_key(prefs_t hprefs, char* key)
{
    const char* TAG = "prefs_erase_key";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READWRITE, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem erase key open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_erase_key(handle, key);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem erase key failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_write_u32(prefs_t hprefs, char* key, uint32_t value)
{
    const char* TAG = "prefs_write_u32";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READWRITE, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_u32(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_u32(prefs_t hprefs, char* key, uint32_t* value)
{
    const char* TAG = "prefs_read_u32";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READONLY, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_u32(handle, key, value);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_write_u64(prefs_t hprefs, char* key, uint64_t value)
{
    const char* TAG = "prefs_write_u64";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READWRITE, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_u64(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_u64(prefs_t hprefs, char* key, uint64_t* value)
{
    const char* TAG = "prefs_read_u64";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READONLY, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_u64(handle, key, value);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_write_block(prefs_t hprefs, char* key, void* buff, uint32_t size)
{
    const char* TAG = "prefs_write_block";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READWRITE, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_blob(handle, key, buff, size);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_block(prefs_t hprefs, char* key, void* buff, uint32_t size)
{
    const char* TAG = "prefs_read_block";
    nvs_handle_t handle = 0;
    size_t length = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READONLY, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_blob(handle, key, NULL, &length);
    if((error == ESP_OK) && (length == size)) {
        error = nvs_get_blob(handle, key, buff, &length);
    } else {
        error = ESP_FAIL;
    }
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get [%s] failed, error %s", key, esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_write_string(prefs_t hprefs, char* key, char* buff)
{
    const char* TAG = "prefs_write_string";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READWRITE, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_str(handle, key, buff);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_string(prefs_t hprefs, char* key, char* buff, uint32_t size)
{
    const char* TAG = "prefs_read_string";
    nvs_handle_t handle = 0;
    size_t length = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READONLY, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_str(handle, key, NULL, &length);
    if(error == ESP_OK) {
        if(length < size) {
            error = nvs_get_str(handle, key, buff, &length);
        } else {
            ESP_LOGE(TAG, "mem read get [%s] failed, buff is too short!", key);
        }
    } else {
        error = ESP_FAIL;
    }
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get [%s] failed, error %s", key, esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_get_string_size(prefs_t hprefs, char* key, uint32_t* length)
{
    const char* TAG = "prefs_get_string_size";
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(hprefs.part_name, hprefs.namespace, NVS_READONLY, &handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_str(handle, key, NULL, (size_t*)length);
    nvs_close(handle);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get [%s] failed, error %s", key, esp_err_to_name(error));
        return false;
    }
    return true;
}
#else

/**
 * @note must call in xTaskCreat task.
 */
bool prefs_init(prefs_t hprefs)
{
    const char* TAG = "prefs_init";
    if(s_dispatcher == NULL) {
        esp_dispatcher_config_t d_cfg = ESP_DISPATCHER_CONFIG_DEFAULT();
        d_cfg.stack_in_ext = false; // Need flash operation.
        s_dispatcher = esp_dispatcher_create(&d_cfg);
        ESP_LOGI(TAG, "dispatcher create success!");
    }
    esp_err_t error = nvs_flash_init_partition(hprefs.part_name);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem %s init failed, error %s", hprefs.part_name, esp_err_to_name(error));
        return false;
    }
    ESP_LOGI(TAG, "mem %s init success!", hprefs.part_name);
    return true;
}

bool prefs_write_u32(prefs_t hprefs, char* key, uint32_t value)
{
    const char* TAG = "prefs_write_u32";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_set_args_t set = { .key = key, .type = NVS_TYPE_U32, .value.u32 = value, .len = sizeof(uint32_t), };
    action_arg_t set_arg = { .data = &set, .len = sizeof(nvs_action_set_args_t),};
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_set, (void *)handle, &set_arg, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_commit, (void *)handle, NULL, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_u32(prefs_t hprefs, char* key, uint32_t* value)
{
    const char* TAG = "prefs_read_u32";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_get_args_t get = { .key = key, .type = NVS_TYPE_U32, .wanted_size = sizeof(uint32_t), };
    action_arg_t get_arg = { .data = &get, .len = sizeof(nvs_action_get_args_t), };
    memset(&result, 0x00, sizeof(action_result_t));
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_get, (void *)handle, &get_arg, &result);
    if(error == ESP_OK && result.data) {
        *value = *(uint32_t*)result.data;
        free(result.data);
    }
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_write_u64(prefs_t hprefs, char* key, uint64_t value)
{
    const char* TAG = "prefs_write_u64";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_set_args_t set = { .key = key, .type = NVS_TYPE_U64, .value.u64 = value, .len = sizeof(uint64_t), };
    action_arg_t set_arg = { .data = &set, .len = sizeof(nvs_action_set_args_t),};
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_set, (void *)handle, &set_arg, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_commit, (void *)handle, NULL, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_u64(prefs_t hprefs, char* key, uint64_t* value)
{
    const char* TAG = "prefs_read_u64";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_get_args_t get = { .key = key, .type = NVS_TYPE_U64, .wanted_size = sizeof(uint64_t), };
    action_arg_t get_arg = { .data = &get, .len = sizeof(nvs_action_get_args_t), };
    memset(&result, 0x00, sizeof(action_result_t));
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_get, (void *)handle, &get_arg, &result);
    if(error == ESP_OK && result.data) {
        *value = *(uint64_t*)result.data;
        free(result.data);
    }
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_write_block(prefs_t hprefs, char* key, void* buff, uint32_t size)
{
    const char* TAG = "prefs_write_block";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_set_args_t set = { .key = key, .type = NVS_TYPE_BLOB, .value.blob = buff, .len = size, };
    action_arg_t set_arg = { .data = &set, .len = sizeof(nvs_action_set_args_t),};
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_set, (void *)handle, &set_arg, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_commit, (void *)handle, NULL, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_block(prefs_t hprefs, char* key, void* buff, uint32_t size)
{
    const char* TAG = "prefs_read_block";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_get_args_t get = { .key = key, .type = NVS_TYPE_BLOB, .wanted_size = -1, };
    action_arg_t get_arg = { .data = &get, .len = sizeof(nvs_action_get_args_t), };
    memset(&result, 0x00, sizeof(action_result_t));
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_get, (void *)handle, &get_arg, &result);
    if(error == ESP_OK && result.data == NULL) {
        get.wanted_size = result.len;
        memset(&result, 0x00, sizeof(action_result_t));
        error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_get, (void *)handle, &get_arg, &result);
        if(error == ESP_OK && result.data && size == result.len) {
            memcpy(buff, result.data, result.len);
        }
        if(result.data) {
            free(result.data);
        }
    }
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_write_string(prefs_t hprefs, char* key, char* buff)
{
    const char* TAG = "prefs_write_block";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_set_args_t set = { .key = key, .type = NVS_TYPE_STR, .value.blob = buff, .len = strlen(buff), };
    action_arg_t set_arg = { .data = &set, .len = sizeof(nvs_action_set_args_t),};
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_set, (void *)handle, &set_arg, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_commit, (void *)handle, NULL, &result);
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem write save failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_read_string(prefs_t hprefs, char* key, char* buff, uint32_t size)
{
    const char* TAG = "prefs_read_string";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_get_args_t get = { .key = key, .type = NVS_TYPE_STR, .wanted_size = -1, };
    action_arg_t get_arg = { .data = &get, .len = sizeof(nvs_action_get_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_get, (void *)handle, &get_arg, &result);
    if(error == ESP_OK && result.data == NULL) {
        get.wanted_size = result.len;
        memset(&result, 0x00, sizeof(action_result_t));
        error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_get, (void *)handle, &get_arg, &result);
        if(error == ESP_OK && result.data && size >= result.len) {
            memcpy(buff, result.data, result.len);
        }
        if(result.data) {
            free(result.data);
        }
    }
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool prefs_get_string_size(prefs_t hprefs, char* key, uint32_t* size)
{
    const char* TAG = "prefs_get_string_size";
    if(s_dispatcher == NULL) {
        ESP_LOGE(TAG, "dispatcher is null!");
        return false;
    }
    nvs_handle_t handle = 0;
    action_result_t result = { 0 };
    esp_err_t error = ESP_OK;
    nvs_action_open_partition_args_t open = { .partition = hprefs.part_name, .name = hprefs.namespace, .open_mode = NVS_READWRITE, };
    action_arg_t open_arg = { .data = &open, .len = sizeof(nvs_action_open_partition_args_t), };
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_open_from_partion, NULL, &open_arg, &result);
    if (error != ESP_OK || result.data == NULL) {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(result.err));
        return false;
    } else {
        handle = *(nvs_handle *)result.data;
        free(result.data);
    }
    nvs_action_get_args_t get = { .key = key, .type = NVS_TYPE_STR, .wanted_size = -1, };
    action_arg_t get_arg = { .data = &get, .len = sizeof(nvs_action_get_args_t), };
    memset(&result, 0x00, sizeof(action_result_t));
    error = esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_get, (void *)handle, &get_arg, &result);
    if(error == ESP_OK && result.data == NULL) {
        *size = result.len;
    }
    esp_dispatcher_execute_with_func(s_dispatcher, nvs_action_close, (void *)handle, NULL, &result);
    if(error != ESP_OK) {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

#endif


#include "prefs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

bool prefs_init(prefs_t hprefs)
{
    const char* TAG = "prefs_init";
    esp_err_t error = nvs_flash_init_partition(hprefs.part_name);
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem %s init failed, error %s", hprefs.part_name, esp_err_to_name(error));
        return false;
    }
    ESP_LOGI(TAG, "mem %s init success!", hprefs.part_name);
    return true;
}

bool prefs_erase_partition(prefs_t hprefs)
{
    const char* TAG = "prefs_erase_partition";
    esp_err_t error = nvs_flash_erase_partition(hprefs.part_name);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem erase namespace open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem erase key open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_erase_key(handle, key);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_u32(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_u32(handle, key, value);
    nvs_close(handle);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_u64(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_u64(handle, key, value);
    nvs_close(handle);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem write open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_set_blob(handle, key, buff, size);
    nvs_commit(handle);
    nvs_close(handle);
    if(error != ESP_OK)
    {
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
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem read open failed, error %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_get_blob(handle, key, NULL, &length);
    if((error == ESP_OK) && (length == size))
    {
        error = nvs_get_blob(handle, key, buff, &length);
    }
    else
    {
        error = ESP_FAIL;
    }
    nvs_close(handle);
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "mem read get failed, error %s", esp_err_to_name(error));
        return false;
    }
    return true;
}






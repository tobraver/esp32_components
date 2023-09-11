#include "update.h"
#include "esp_log.h"
#include "esp_err.h"

bool update_begin(update_t* hupdate)
{
    const char* TAG = "update_begin";

    hupdate->handle = 0;

    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if(update_partition == NULL) 
    {
        ESP_LOGE(TAG, "System can't find next update partition, please check custom partition table!");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x", update_partition->subtype, update_partition->address);

    esp_ota_handle_t update_handle = 0;
    esp_err_t error = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (error != ESP_OK) 
    {
        ESP_LOGE(TAG, "Update esp ota begin failed, error %s", esp_err_to_name(error));
        return false;
    }
    
    hupdate->partition = update_partition;
    hupdate->handle = update_handle;
    return true;
}

bool update_end(update_t* hupdate)
{
    const char* TAG = "update_end";

    if(hupdate->handle == 0)
    {
        ESP_LOGE(TAG, "Update handle is 0!");
        return false;
    }

    esp_err_t error = esp_ota_end(hupdate->handle);
    if(error != ESP_OK)
    {
        update_abort(hupdate);
        ESP_LOGE(TAG, "OTA end failed, error %s!", esp_err_to_name(error));
        return false;
    }

    error = esp_ota_set_boot_partition(hupdate->partition);
    if (error != ESP_OK) 
    {
        update_abort(hupdate);
        ESP_LOGE(TAG, "OTA set boot failed, error %s!", esp_err_to_name(error));
        return false;
    }

    update_abort(hupdate);
    return true;
}

bool update_abort(update_t* hupdate)
{
    const char* TAG = "update_abort";

    if(hupdate->handle == 0)
    {
        ESP_LOGE(TAG, "Update handle is 0!");
        return false;
    }

    esp_err_t error = esp_ota_abort(hupdate->handle);
    if(error != ESP_OK)
    {
        ESP_LOGE(TAG, "OTA abort failed, error (%s)", esp_err_to_name(error));
        return false;
    }

    hupdate->handle = 0;
    return true;
}

bool update_appened(update_t* hupdate, uint8_t* buff, uint32_t size)
{
    const char* TAG = "update_appened";

    if(hupdate->handle == 0)
    {
        ESP_LOGE(TAG, "Update handle is 0!");
        return false;
    }

    esp_err_t error = esp_ota_write(hupdate->handle, (const void *)buff, size);
    if(error != ESP_OK)
    {
        update_abort(hupdate);
        ESP_LOGE(TAG, "OTA write failed, error (%s)", esp_err_to_name(error));
        return false;
    }

    return true;
}


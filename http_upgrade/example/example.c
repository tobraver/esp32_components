#include "http_upgrade.h"

static const char *TAG = "main";

esp_err_t http_upgrade_set_resume(const char* label, uint64_t value)
{
    FILE* fp = fopen("/sdcard/resume", "wb+");
    if(fp == NULL) {
        return ESP_FAIL;
    }

    fwrite(&value, 1, sizeof(value), fp);
    fclose(fp);
    return ESP_OK;
}

esp_err_t http_upgrade_get_resume(const char* label, uint64_t* value)
{
    FILE* fp = fopen("/sdcard/resume", "rb");
    if(fp == NULL) {
        return ESP_FAIL;
    }
    if(fread(value, 1, sizeof(*value), fp) != sizeof(*value)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_main()
{
    // mount sdcard

    // connect to netif
    
    http_upgrade_config_t cfg = {
        .set_resume = http_upgrade_set_resume,
        .get_resume = http_upgrade_get_resume,
    };
    http_upgrade_init(cfg);

    ESP_LOGI(TAG, "Create OTA service");
    ota_service_config_t ota_service_cfg = OTA_SERVICE_DEFAULT_CONFIG();
    ota_service_cfg.task_stack = 8 * 1024;
    ota_service_cfg.evt_cb = ota_service_cb;
    ota_service_cfg.cb_ctx = NULL;
    periph_service_handle_t ota_service = ota_service_create(&ota_service_cfg);
    events = xEventGroupCreate();

    ESP_LOGI(TAG, "Set upgrade list");
    ota_upgrade_ops_t upgrade_list[] = {
        {
            {
                ESP_PARTITION_TYPE_APP,
                "app",
                "http://20.ss360.org/upload/localpath/usercenter/20241120/admin/ota_example.zip?&filemd5=2667ac2c59970878c7fc10f2bcd62f9e",
                NULL
            },
            NULL,
            NULL,
            NULL,
            NULL,
            true,
            false
        }
    };

    // ota_app_get_default_proc(&upgrade_list[1]);
    http_upgrade_get_default_proc(&upgrade_list[0]);

    ota_service_set_upgrade_param(ota_service, upgrade_list, sizeof(upgrade_list) / sizeof(ota_upgrade_ops_t));

    ESP_LOGI(TAG, "Start OTA service");
    AUDIO_MEM_SHOW(TAG);
    periph_service_start(ota_service);

    EventBits_t bits = xEventGroupWaitBits(events, OTA_FINISH, true, false, portMAX_DELAY);
    if (bits & OTA_FINISH) {
        ESP_LOGI(TAG, "Finish OTA service");
    }
    ESP_LOGI(TAG, "Clear OTA service");
    periph_service_destroy(ota_service);
    vEventGroupDelete(events);
}

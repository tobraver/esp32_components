#include "esp_context.h"
#include "esp_timer.h"

esp_err_t esp_context_enter(esp_context_t* ctx, uint32_t deadline, esp_context_cb_t callback)
{
    esp_timer_create_args_t conf = {
        .name = "context",
        .callback = callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_err_t ret = ESP_FAIL;
    esp_timer_handle_t handle = NULL;
    ret |= esp_timer_create(&conf, &handle);
    if(ret == ESP_OK) {
        ret |= esp_timer_start_once(handle, deadline * 1000);
    }
    *ctx = handle;
    return ret;
}

esp_err_t esp_context_exit(void* ctx)
{
    esp_err_t ret = ESP_OK;
    esp_timer_handle_t handle = (esp_timer_handle_t)ctx;
    if(ctx) {
        ret |= esp_timer_stop(handle);
        ret |= esp_timer_delete(handle);
    }
    return ret;
}

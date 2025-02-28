#include "esp_context.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"

typedef enum {
    FL_ISR_DISPATCH_METHOD   = (1 << 0),
    FL_SKIP_UNHANDLED_EVENTS = (1 << 1),
} flags_t;

struct esp_timer {
    uint64_t alarm;
    uint64_t period:56;
    flags_t flags:8;
    union {
        esp_timer_cb_t callback;
        uint32_t event_id;
    };
    void* arg;
};

static const char* TAG = "esp_context";

esp_err_t esp_context_enter(esp_context_t* ctx, uint32_t deadline, esp_context_cb_t callback)
{
    uint32_t now = esp_timer_get_time() / 1000;
    esp_timer_create_args_t conf = {
        .name = "context",
        .callback = callback,
        .arg = (void*)now,
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_err_t ret = ESP_OK;
    esp_timer_handle_t handle = NULL;
    ret |= esp_timer_create(&conf, &handle);
    if(ret == ESP_OK) {
        ret |= esp_timer_start_once(handle, deadline * 1000);
    }
    if(ctx) {
        *ctx = handle;
    }
    ESP_LOGI(TAG, "enter context instance: %p", handle);
    return ret;
}

esp_err_t esp_context_exit(esp_context_t ctx)
{
    esp_err_t ret = ESP_OK;
    esp_timer_handle_t handle = (esp_timer_handle_t)ctx;
    if(ctx) {
        ret |= esp_timer_stop(handle);
        ret |= esp_timer_delete(handle);
    }
    ESP_LOGI(TAG, "exit context instance: %p", handle);
    return ret;
}

uint32_t esp_context_get_elapsed(esp_context_t ctx)
{
    uint32_t elapse = 0, begin = 0;
    esp_timer_handle_t handle = (esp_timer_handle_t)ctx;
    if(ctx) {
        begin = (uint32_t)handle->arg;
        elapse = esp_timer_get_time() / 1000 - begin;
    }
    return elapse;
}

static void esp_context_reboot_cb(void* arg)
{
    ESP_LOGI(TAG, "context reboot callback");
    esp_restart();
}

esp_err_t esp_context_delay_reboot(uint32_t delay)
{
    return esp_context_enter(NULL, delay, esp_context_reboot_cb);
}


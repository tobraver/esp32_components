#ifndef __ESP_CONTEXT_H__
#define __ESP_CONTEXT_H__

#include "stdio.h"
#include "stdint.h"
#include "esp_err.h"

typedef void* esp_context_t;
typedef void (*esp_context_cb_t)(void* arg);

esp_err_t esp_context_enter(esp_context_t* ctx, uint32_t deadline, esp_context_cb_t callback);
esp_err_t esp_context_exit(void* ctx);

#endif // __ESP_CONTEXT_H__

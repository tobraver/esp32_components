#ifndef __UPDATE_H__
#define __UPDATE_H__

#include "stdint.h"
#include "stdbool.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"

typedef struct 
{
    const esp_partition_t * partition;
    esp_ota_handle_t        handle;
} update_t;

bool update_begin(update_t* hupdate);
bool update_end(update_t* hupdate);
bool update_abort(update_t* hupdate);
bool update_appened(update_t* hupdate, uint8_t* buff, uint32_t size);

#endif // !__UPDATE_H__

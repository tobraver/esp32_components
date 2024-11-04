#include "stdio.h"
#include "string.h"
#include "esp_log.h"
#include "gzip_deflate.h"

esp_err_t stream_out_func(uint8_t* data, uint32_t len)
{
    printf("len: %d\n", len);
    ESP_LOG_BUFFER_HEX("main", data, len);
    return ESP_OK;
}

void app_main(void)
{
    char buff[1024];
    gzip_deflate_handle_t handle = gzip_deflate_create(stream_out_func);
    
    for(int i = 0; i < 10; i++) {
        sprintf(buff, "[%d]hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello world hello\n", i);
        gzip_deflate_write(handle, (uint8_t*)buff, strlen(buff), 0);
    }
    gzip_deflate_write(handle, (uint8_t*)"finish", strlen("finish"), 1);
    gzip_deflate_destroy(handle);
}
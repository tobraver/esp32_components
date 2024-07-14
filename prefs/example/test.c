#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_event.h"
#include "string.h"
#include "prefs.h"
#include "sys.h"
#include "audio_thread.h"

prefs_t hprefs = {
    .part_name = "nvs",
    .namespace = "config"
};

void prefs_task(void *arg)
{
    uint32_t u32_val = 0;
    prefs_write_u32(hprefs, "u32_val", 32);
    prefs_read_u32(hprefs, "u32_val", &u32_val);
    printf("u32 read value: %ld\n", u32_val);
    prefs_read_u32(hprefs, "u32_err", &u32_val);

    uint64_t u64_val = 0;
    prefs_write_u64(hprefs, "u64_val", 64);
    prefs_read_u64(hprefs, "u64_val", &u64_val);
    printf("u64 read value: %lld\n", u64_val);
    prefs_read_u64(hprefs, "u64_err", &u64_val);

    uint8_t block[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    prefs_write_block(hprefs, "bolck_val", block, 12);
    memset(block, 0, 12);
    prefs_read_block(hprefs, "bolck_val", block, 12);
    for(int i=0; i<12; i++) {
        printf("block read value: %d\n", block[i]);
    }
    prefs_read_block(hprefs, "bolck_err", block, 12);

    char *str_val = "hello world";
    char str_buf[64] = {0};
    prefs_write_string(hprefs, "str_val", str_val);
    prefs_read_string(hprefs, "str_val", str_buf, 64);
    uint32_t str_size = 0;
    prefs_get_string_size(hprefs, "str_val", &str_size);
    printf("str read value: %s, size: %ld\n", str_buf, str_size);

    prefs_read_string(hprefs, "str_err", str_buf, 64);
    prefs_get_string_size(hprefs, "str_err", &str_size);

    vTaskDelete(NULL);
}

void app_main(void)
{
    prefs_init(hprefs);
    audio_thread_create(NULL, "prefs", prefs_task, NULL, 4*1024, 5, true, 0);
}

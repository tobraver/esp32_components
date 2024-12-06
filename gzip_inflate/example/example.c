#include <stdio.h>
#include "string.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "mbedtls/base64.h"
#include "gzip_inflate.h"

void app_main(void)
{
    printf("Hello world!\n");

    char* msg = "H4sIAAAAAAAAA4tWMk62MDEyTzNIMk40UNJRQOYbovGNRhw/FgBSPxuXIAEAAA==";
    uint8_t out1[1024], out2[1024] = { 0 };
    int len1 = 0, len2 = sizeof(out2) - 1;
    if(mbedtls_base64_decode(out1, sizeof(out1), (size_t*)&len1, (uint8_t*)msg, strlen(msg)) < 0) {
        printf("base64_decode failed\n");
        return;
    }

    if(gzip_inflate(out1, len1, out2, &len2) != ESP_OK) {
        printf("gzip_inflate failed\n");
        return;
    }
    printf("gzip inflate success\n");
    printf("%s\n", out2);
}

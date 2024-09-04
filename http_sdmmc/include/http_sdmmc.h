#ifndef __HTTP_SDMMC_H__
#define __HTTP_SDMMC_H__

#include "stdio.h"
#include "stdint.h"
#include "esp_err.h"
#include "driver/gpio.h"

// sdmmc line1 mode, gpio config
#define HTTP_SDMMC_PIN_CLK  GPIO_NUM_15
#define HTTP_SDMMC_PIN_CMD  GPIO_NUM_7
#define HTTP_SDMMC_PIN_D0   GPIO_NUM_4

// sdmmc mount path
#define HTTP_SDMMC_MOUNT_PATH       "/sdcard"
// sdmmc format if mount failed
#define HTTP_SDMMC_FORMAT_ENABLE    1
// sdmmc mount retry times
#define HTTP_SDMMC_MOUNT_RETRY      3

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t http_sdmmc_init(void);
esp_err_t http_sdmmc_deinit(void);
esp_err_t http_sdmmc_ready(void);
esp_err_t http_sdmmc_download(char* url, char* file);

#ifdef __cplusplus
}
#endif
#endif // !__HTTP_SDMMC_H__

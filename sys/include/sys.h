#ifndef __SYS_H__
#define __SYS_H__

#include "stdio.h"
#include "stdint.h"
#include "esp_log.h"

#define SYS_LOG_TARGET      ""
#define LOGI(format, ...)   ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, SYS_LOG_TARGET, format, ##__VA_ARGS__)
#define LOGW(format, ...)   ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN, SYS_LOG_TARGET, format, ##__VA_ARGS__)
#define LOGE(format, ...)   ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, SYS_LOG_TARGET, format, ##__VA_ARGS__)

#if __cplusplus
extern "C" {
#endif

void delay_ms(uint32_t ms);
void delay_us(uint32_t us);

char* sys_sn(void);
void sys_mac(uint8_t mac[6]);

uint32_t sys_ticks(void);
uint64_t sys_ticks_us(void);

void sys_restart(void);
uint32_t sys_heap_size(void);

int sys_str2hex(char *src, int slen, uint8_t *dst);
int sys_hex2str(char *src, uint32_t slen, char *dst, int lowercase);
int sys_str2uint(char *src, uint32_t slen, uint32_t *dst);
int sys_uint2str(uint32_t src, char *dst, uint32_t *dlen);

uint16_t sys_swap16(uint16_t value);
uint32_t sys_swap32(uint32_t value);

#if __cplusplus
}
#endif
#endif // !__SYS_H__

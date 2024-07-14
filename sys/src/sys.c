#include "sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_timer.h"

#include "esp_random.h"
#include "esp_mac.h"

#define NOP() asm volatile ("nop")

void delay_ms(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

void delay_us(uint32_t us)
{
    uint64_t m = (uint64_t)esp_timer_get_time();
    if(us){
        uint64_t e = (m + us);
        if(m > e){ //overflow
            while((uint64_t)esp_timer_get_time() > e){
                NOP();
            }
        }
        while((uint64_t)esp_timer_get_time() < e){
            NOP();
        }
    }
}

char* sys_get_sn(void)
{
    uint8_t mac[6] = {0};
    static char buf[30] = {0};
    esp_efuse_mac_get_default(mac);
    snprintf(buf, sizeof(buf),"%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return buf;
}

void sys_get_mac(uint8_t mac[6])
{
    esp_efuse_mac_get_default(mac);
}

uint32_t sys_random(void)
{
    return esp_random();
}

uint32_t sys_ticks(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000ULL);
}

uint64_t sys_ticks_us(void)
{
    return (uint64_t) (esp_timer_get_time());
}

void sys_restart(void)
{
    esp_restart();
}

uint32_t sys_heap_size(void)
{
    return esp_get_free_heap_size();
}
int sys_str2hex(char *src, int slen, uint8_t *dst)
{
    int idx = 0;

    if (slen % 2 != 0) {
        return -1;
    }

    for (idx = 0; idx < slen; idx += 2) {
        if (src[idx] >= '0' && src[idx] <= '9') {
            dst[idx / 2] = (src[idx] - '0') << 4;
        } else if (src[idx] >= 'A' && src[idx] <= 'F') {
            dst[idx / 2] = (src[idx] - 'A' + 0x0A) << 4;
        } else if (src[idx] >= 'a' && src[idx] <= 'f') {
            dst[idx / 2] = (src[idx] - 'a' + 0x0A) << 4;
        }
        if (src[idx + 1] >= '0' && src[idx + 1] <= '9') {
            dst[idx / 2] |= (src[idx + 1] - '0');
        } else if (src[idx + 1] >= 'A' && src[idx + 1] <= 'F') {
            dst[idx / 2] |= (src[idx + 1] - 'A' + 0x0A);
        } else if (src[idx + 1] >= 'a' && src[idx + 1] <= 'f') {
            dst[idx / 2] |= (src[idx + 1] - 'a' + 0x0A);
        }
    }
	return 0;
}

int sys_hex2str(char *src, uint32_t slen, char *dst, int lowercase)
{
    char *upper = "0123456789ABCDEF";
    char *lower = "0123456789abcdef";
    char *encode = upper;
    int i = 0, j = 0;

    if (lowercase) {
        encode = lower;
    }

    for (i = 0; i < slen; i++) {
        dst[j++] = encode[(src[i] >> 4) & 0xf];
        dst[j++] = encode[(src[i]) & 0xf];
    }

    return 0;
}

int sys_str2uint(char *src, uint32_t slen, uint32_t *dst)
{
    uint32_t index = 0;
    uint32_t temp = 0;

    for (index = 0; index < slen; index++) {
        if (src[index] < '0' || src[index] > '9') {
            return -1;
        }
        temp = temp * 10 + src[index] - '0';
    }
    *dst = temp;

    return 0;
}

int sys_uint2str(uint32_t src, char *dst, uint32_t *dlen)
{
    uint32_t i = 0, j = 0;
    char temp[10] = {0};

    do {
        temp[i++] = src % 10 + '0';
    } while ((src /= 10) > 0);

    do {
        dst[--i] = temp[j++];
    } while (i > 0);

    if (dlen) {
        *dlen = j;
    }

    return 0;
}

uint16_t sys_swap16(uint16_t value)
{
    return (uint16_t)((((value) & (uint16_t)0x00ffU) << 8) | (((value) & (uint16_t)0xff00U) >> 8));
}

uint32_t sys_swap32(uint32_t value)
{
    return  ((((value) & (uint32_t)0x000000ffUL) << 24) | \
            (((value) & (uint32_t)0x0000ff00UL) <<  8)  | \
            (((value) & (uint32_t)0x00ff0000UL) >>  8)  | \
            (((value) & (uint32_t)0xff000000UL) >> 24));
}
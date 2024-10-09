#include "rb_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "esp_log.h"

// cache size with stack memory
#define RB_LOG_CACHE_SIZE          256
// ring buffer read timeout
#define RB_LOG_BUFF_RD_TIMEOUT     pdMS_TO_TICKS(50)
// ring buffer write timeout
#define RB_LOG_BUFF_WR_TIMEOUT     pdMS_TO_TICKS(50)

static RingbufHandle_t s_rb_log = NULL;

static int rb_log_prepare_buff(int size)
{
    int ret = -1;
    int retry = 30;
    if((s_rb_log == NULL) || (size < 0)) {
        return ret;
    }
    do {
        size_t free_size = xRingbufferGetCurFreeSize(s_rb_log);
        if(free_size > size) {
            ret = 0;
            break;
        } else {
            void* item = xRingbufferReceive(s_rb_log, NULL, RB_LOG_BUFF_RD_TIMEOUT);
            if(item) {
                vRingbufferReturnItem(s_rb_log, item);
            }
        }
    } while (retry--);
    return ret;
}

static int rb_log_vprintf(const char *fmt, va_list args)
{
    int size = 0;
#if RB_LOG_BUFF_ENABLE
    char cache[RB_LOG_CACHE_SIZE] = { 0 };
    size = vsnprintf(cache, sizeof(cache) - 1, fmt, args);
    if(s_rb_log && (size > 0) && (size < RB_LOG_CACHE_SIZE)) {
        if(rb_log_prepare_buff(size) < 0) {
#if RB_LOG_DEBUG_ENABLE
            printf("## log server preare buffer failed!\n");
#endif
        } else {
            xRingbufferSend(s_rb_log, cache, size + 1, RB_LOG_BUFF_WR_TIMEOUT);
        }
    }
#endif // RB_LOG_BUFF_ENABLE
#if RB_LOG_UART_ENABLE
    size = vprintf(fmt, args);
#endif // RB_LOG_UART_ENABLE
    return size;
}

int rb_log_init(void)
{
    int ret = 0;
#if RB_LOG_BUFF_ENABLE
    s_rb_log = xRingbufferCreate(RB_LOG_BUFF_SIZE, RINGBUF_TYPE_NOSPLIT);
    if(s_rb_log == NULL) {
        ret = -1;
        printf("## log server ring buffer create failed!");
    }
#endif
#if RB_LOG_BUFF_ENABLE || RB_LOG_UART_ENABLE
    esp_log_set_vprintf(rb_log_vprintf);
#endif
    return ret;
}

int rb_log_get_msg_num(void)
{
    UBaseType_t msg_num = 0;
    vRingbufferGetInfo(s_rb_log, NULL, NULL, NULL, NULL, &msg_num);
    return msg_num;
}

char* rb_log_get_msg(void)
{
    char* msg = NULL;
#if RB_LOG_BUFF_ENABLE
    size_t msg_len = 0;
    if(s_rb_log == NULL) {
        return msg;
    }
    UBaseType_t msg_num = 0;
    vRingbufferGetInfo(s_rb_log, NULL, NULL, NULL, NULL, &msg_num);
    if(msg_num == 0) {
#if RB_LOG_DEBUG_ENABLE
        printf("## log server message is empty!\n");
#endif
        return msg;
    }
    msg = (char*)xRingbufferReceive(s_rb_log, &msg_len, RB_LOG_BUFF_RD_TIMEOUT);
#endif
    return msg;
}

int rb_log_free_msg(char* msg)
{
    int ret = 0;
#if RB_LOG_BUFF_ENABLE
    if((s_rb_log == NULL) || (msg == NULL)) {
        ret = -1;
        return ret;
    }
    vRingbufferReturnItem(s_rb_log, (void*)msg);
#endif
    return ret;
}


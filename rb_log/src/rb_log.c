#include "rb_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

// cache size with stack memory
#define RB_LOG_CACHE_SIZE          256
// ring buffer read timeout
#define RB_LOG_BUFF_RD_TIMEOUT     pdMS_TO_TICKS(50)
// ring buffer write timeout
#define RB_LOG_BUFF_WR_TIMEOUT     pdMS_TO_TICKS(50)

static RingbufHandle_t s_rb_log = NULL;

static int _rb_log_prepare_buff(int size)
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

static int _rb_log_vprintf(const char *fmt, va_list args)
{
    int size = 0;
#if RB_LOG_BUFF_ENABLE
    int is_heap = 0;
    char* heap_cache = NULL;
    char stack_cache[RB_LOG_CACHE_SIZE] = { 0 };
    int cache_size = RB_LOG_CACHE_SIZE;
    size = vsnprintf(NULL, 0, fmt, args);
    if(size > cache_size - 1) {
        is_heap = 1;
        cache_size = size + 1;
    }
    if(is_heap) {
#if RB_LOG_BUFF_PSRAM
        heap_cache = (char*)heap_caps_malloc(cache_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        heap_cache = (char*)malloc(cache_size);
#endif // RB_LOG_BUFF_PSRAM
        if(heap_cache == NULL) {
#if RB_LOG_DEBUG_ENABLE
            printf("## rb log heap cache malloc failed!");
#endif // RB_LOG_DEBUG_ENABLE
        } else {
            size = vsprintf(heap_cache, fmt, args);
        }
    } else {
        size = vsprintf(stack_cache, fmt, args);
    }
    if(s_rb_log && (size > 0) && (size < cache_size)) {
        if(_rb_log_prepare_buff(size) < 0) {
#if RB_LOG_DEBUG_ENABLE
            printf("## rb log preare buffer failed!\n");
#endif // RB_LOG_DEBUG_ENABLE
        } else {
            if(is_heap) {
                if(heap_cache) {
                    heap_cache[size] = '\0'; // to string
                    xRingbufferSend(s_rb_log, heap_cache, size + 1, RB_LOG_BUFF_WR_TIMEOUT);
                }
            } else {
                stack_cache[size] = '\0'; // to string
                xRingbufferSend(s_rb_log, stack_cache, size + 1, RB_LOG_BUFF_WR_TIMEOUT);
            }
        }
        if(is_heap && heap_cache) {
            free(heap_cache);
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
    int ret = -1;
#if RB_LOG_BUFF_ENABLE
#if RB_LOG_BUFF_PSRAM
    StaticRingbuffer_t *buff_struct = (StaticRingbuffer_t *)heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buff_storage = (uint8_t *)heap_caps_malloc(RB_LOG_BUFF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rb_log = xRingbufferCreateStatic(RB_LOG_BUFF_SIZE, RINGBUF_TYPE_NOSPLIT, buff_storage, buff_struct);
#else
    StaticRingbuffer_t *buff_struct = (StaticRingbuffer_t *)malloc(sizeof(StaticRingbuffer_t));
    uint8_t *buff_storage = (uint8_t *)malloc(RB_LOG_BUFF_SIZE);
    s_rb_log = xRingbufferCreateStatic(RB_LOG_BUFF_SIZE, RINGBUF_TYPE_NOSPLIT, buff_storage, buff_struct);
#endif // RB_LOG_BUFF_PSRAM
    if(s_rb_log == NULL) {
        printf("## rb log ring buffer create failed!");
    }
#endif // RB_LOG_BUFF_ENABLE
#if RB_LOG_BUFF_ENABLE || RB_LOG_UART_ENABLE
    esp_log_set_vprintf(_rb_log_vprintf);
#endif // RB_LOG_BUFF_ENABLE || RB_LOG_UART_ENABLE
    ret = 0;
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
        printf("## rb log message is empty!\n");
#endif // RB_LOG_DEBUG_ENABLE
        return msg;
    }
    msg = (char*)xRingbufferReceive(s_rb_log, &msg_len, RB_LOG_BUFF_RD_TIMEOUT);
#endif // RB_LOG_BUFF_ENABLE
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
#endif // RB_LOG_BUFF_ENABLE
    return ret;
}


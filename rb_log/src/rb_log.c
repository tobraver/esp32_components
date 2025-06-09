#include "rb_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/uart.h"

// memory free
#define RB_LOG_MEM_FREE(addr)   if(addr) { \
                                    free(addr); \
                                }
// error exit
static bool s_debuge_enable = RB_LOG_DEBUG_ENABLE;
#define RB_LOG_ERROR_EXIT(a, msg)   if(a) { \
                                        RB_LOG_MEM_FREE(heap_cache); \
                                        if(s_debuge_enable && msg) { \
                                            printf("%s\n", msg); \
                                        } \
                                        return vprintf(fmt, args); \
                                    }

// cache size with stack memory
#define RB_LOG_CACHE_SIZE          256
// ring buffer read timeout
#define RB_LOG_BUFF_RD_TIMEOUT     pdMS_TO_TICKS(50)
// ring buffer write timeout
#define RB_LOG_BUFF_WR_TIMEOUT     pdMS_TO_TICKS(50)

static RingbufHandle_t s_rb_log = NULL;

// static int _rb_log_prepare_buff(int size)
// {
//     int ret = -1;
//     int retry = 30;
//     if((s_rb_log == NULL) || (size <= 0)) {
//         return ret;
//     }
//     do {
//         size_t free_size = xRingbufferGetCurFreeSize(s_rb_log);
//         if(free_size > size) {
//             ret = 0;
//             break;
//         } else {
//             void* item = xRingbufferReceive(s_rb_log, NULL, RB_LOG_BUFF_RD_TIMEOUT);
//             if(item) {
//                 vRingbufferReturnItem(s_rb_log, item);
//             }
//         }
//     } while (retry--);
//     return ret;
// }

static int _rb_log_vprintf(const char *fmt, va_list args)
{
    int size = 0;
    int is_heap = 0;
    char* heap_cache = NULL;
    char stack_cache[RB_LOG_CACHE_SIZE] = { 0 };
    int cache_size = RB_LOG_CACHE_SIZE;
    RB_LOG_ERROR_EXIT(s_rb_log == NULL, \
        "## rb log ring buffer is null!");

    // get format string size
    size = vsnprintf(NULL, 0, fmt, args);
    RB_LOG_ERROR_EXIT(size <= 0, \
        "## rb log format failed!");
    
    // if size is larger than RB_LOG_CACHE_SIZE - 1, use malloc memory
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
        RB_LOG_ERROR_EXIT(heap_cache == NULL, \
            "## rb log heap cache malloc failed!");
    }
    char* cache = is_heap ? heap_cache : stack_cache;
    size = vsprintf(cache, fmt, args);

    RB_LOG_ERROR_EXIT((size <= 0) || (size >= cache_size), \
            "## rb log format to cache failed!");

    // ring buffer prepare
    // RB_LOG_ERROR_EXIT(_rb_log_prepare_buff(size) < 0,
    //     "## rb log preare buffer failed!");
    RB_LOG_ERROR_EXIT((rb_log_get_free_size() + 8) < size, \
            "## rb log is full!");

    // write to ring buffer
    xRingbufferSend(s_rb_log, cache, size + 1, RB_LOG_BUFF_WR_TIMEOUT);

    // write to uart [performance]
    // vprintf(fmt, args);
    fwrite(cache, size, 1, stdout);

    RB_LOG_MEM_FREE(heap_cache);
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
    esp_log_set_vprintf(_rb_log_vprintf);
#endif // RB_LOG_BUFF_ENABLE
    ret = 0;
    return ret;
}

int rb_log_get_msg_num(void)
{
    UBaseType_t msg_num = 0;
    if(s_rb_log == NULL) {
        return msg_num;
    }
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

//Ring buffer flags
#define rbALLOW_SPLIT_FLAG          ( ( UBaseType_t ) 1 )   //The ring buffer allows items to be split
#define rbBYTE_BUFFER_FLAG          ( ( UBaseType_t ) 2 )   //The ring buffer is a byte buffer
#define rbBUFFER_FULL_FLAG          ( ( UBaseType_t ) 4 )   //The ring buffer is currently full (write pointer == free pointer)
#define rbBUFFER_STATIC_FLAG        ( ( UBaseType_t ) 8 )   //The ring buffer is statically allocated

typedef struct RingbufferDefinition Ringbuffer_t;
typedef BaseType_t (*CheckItemFitsFunction_t)(Ringbuffer_t *pxRingbuffer, size_t xItemSize);
typedef void (*CopyItemFunction_t)(Ringbuffer_t *pxRingbuffer, const uint8_t *pcItem, size_t xItemSize);
typedef BaseType_t (*CheckItemAvailFunction_t) (Ringbuffer_t *pxRingbuffer);
typedef void *(*GetItemFunction_t)(Ringbuffer_t *pxRingbuffer, BaseType_t *pxIsSplit, size_t xMaxSize, size_t *pxItemSize);
typedef void (*ReturnItemFunction_t)(Ringbuffer_t *pxRingbuffer, uint8_t *pvItem);
typedef size_t (*GetCurMaxSizeFunction_t)(Ringbuffer_t *pxRingbuffer);

typedef struct RingbufferDefinition {
    size_t xSize;                               //Size of the data storage
    size_t xMaxItemSize;                        //Maximum item size
    UBaseType_t uxRingbufferFlags;              //Flags to indicate the type and status of ring buffer

    CheckItemFitsFunction_t xCheckItemFits;     //Function to check if item can currently fit in ring buffer
    CopyItemFunction_t vCopyItem;               //Function to copy item to ring buffer
    GetItemFunction_t pvGetItem;                //Function to get item from ring buffer
    ReturnItemFunction_t vReturnItem;           //Function to return item to ring buffer
    GetCurMaxSizeFunction_t xGetCurMaxSize;     //Function to get current free size

    uint8_t *pucAcquire;                        //Acquire Pointer. Points to where the next item should be acquired.
    uint8_t *pucWrite;                          //Write Pointer. Points to where the next item should be written
    uint8_t *pucRead;                           //Read Pointer. Points to where the next item should be read from
    uint8_t *pucFree;                           //Free Pointer. Points to the last item that has yet to be returned to the ring buffer
    uint8_t *pucHead;                           //Pointer to the start of the ring buffer storage area
    uint8_t *pucTail;                           //Pointer to the end of the ring buffer storage area

    BaseType_t xItemsWaiting;                   //Number of items/bytes(for byte buffers) currently in ring buffer that have not yet been read
    /*
     * TransSem: Binary semaphore used to indicate to a blocked transmitting tasks
     *           that more free space has become available or that the block has
     *           timed out.
     *
     * RecvSem: Binary semaphore used to indicate to a blocked receiving task that
     *          new data/item has been written to the ring buffer.
     *
     * Note - When static allocation is enabled, the two semaphores are always
     *        statically stored in the ring buffer's control structure
     *        regardless of whether the ring buffer is allocated dynamically or
     *        statically. When static allocation is disabled, the two semaphores
     *        are allocated dynamically and their handles stored instead, thus
     *        making the ring buffer's control structure slightly smaller when
     *        static allocation is disabled.
     */
#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    StaticSemaphore_t xTransSemStatic;
    StaticSemaphore_t xRecvSemStatic;
#else
    SemaphoreHandle_t xTransSemHandle;
    SemaphoreHandle_t xRecvSemHandle;
#endif
    portMUX_TYPE mux;                           //Spinlock required for SMP
} Ringbuffer_t;

static size_t prvGetFreeSize(Ringbuffer_t *pxRingbuffer)
{
    size_t xReturn;
    if (pxRingbuffer->uxRingbufferFlags & rbBUFFER_FULL_FLAG) {
        xReturn =  0;
    } else {
        BaseType_t xFreeSize = pxRingbuffer->pucFree - pxRingbuffer->pucAcquire;
        //Check if xFreeSize has underflowed
        if (xFreeSize <= 0) {
            xFreeSize += pxRingbuffer->xSize;
        }
        xReturn = xFreeSize;
    }
    configASSERT(xReturn <= pxRingbuffer->xSize);
    return xReturn;
}

int rb_log_get_free_size(void)
{
    Ringbuffer_t *pxRingbuffer = (Ringbuffer_t *)s_rb_log;
    configASSERT(pxRingbuffer);
    int free_size = prvGetFreeSize(pxRingbuffer);
    return free_size;
}
#ifndef __UDP_STREAM_H__
#define __UDP_STREAM_H__

#include "audio_error.h"
#include "audio_element.h"
#include "esp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UDP_STREAM_STATE_NONE,
    UDP_STREAM_STATE_OPEN,
    UDP_STREAM_STATE_TIMEOUT,
    UDP_STREAM_STATE_ERROR,
    UDP_STREAM_STATE_CLOSE,
} udp_stream_status_t;

/**
 * @brief   UDP Stream massage configuration
 */
typedef struct udp_stream_event_msg {
    void                          *source;          /*!< Element handle */
    void                          *data;            /*!< Data of input/output */
    int                           data_len;         /*!< Data length of input/output */
    int                           sock_fd;          /*!< handle of socket*/
} udp_stream_event_msg_t;

typedef esp_err_t (*udp_stream_event_handle_cb)(udp_stream_event_msg_t *msg, udp_stream_status_t state, void *event_ctx);

/**
 * @brief   UDP Stream configuration, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    audio_stream_type_t         type;               /*!< Type of stream */
    int                         timeout_ms;         /*!< time timeout for read/write*/
    int                         port;               /*!< UDP port> */
    char                        *host;              /*!< UDP host> */
    bool                        use_mreq;           /*!< UDP multicast request */
    int                         out_rb_size;        /*!< UDP out ring buffer size */
    int                         task_stack;         /*!< Task stack size */
    int                         task_core;          /*!< Task running in core (0 or 1) */
    int                         task_prio;          /*!< Task priority (based on freeRTOS priority) */
    bool                        ext_stack;          /*!< Allocate stack on extern ram */
    udp_stream_event_handle_cb  event_handler;      /*!< UDP stream event callback*/
    void                        *event_ctx;         /*!< User context*/
} udp_stream_cfg_t;

#define UDP_STREAM_DEFAULT_PORT             (8080)

#define UDP_STREAM_TASK_STACK               (3072)
#define UDP_STREAM_BUF_SIZE                 (4096)
#define UDP_STREAM_TASK_PRIO                (5)
#define UDP_STREAM_TASK_CORE                (0)

#define UDP_SERVER_DEFAULT_RESPONSE_LENGTH  (512)

#define UDP_STREAM_CFG_DEFAULT() {              \
    .type          = AUDIO_STREAM_READER,       \
    .timeout_ms    = 30 * 1000,                 \
    .port          = UDP_STREAM_DEFAULT_PORT,   \
    .host          = NULL,                      \
    .use_mreq      = false,                     \
    .out_rb_size   = 30 * 1024,                 \
    .task_stack    = UDP_STREAM_TASK_STACK,     \
    .task_core     = UDP_STREAM_TASK_CORE,      \
    .task_prio     = UDP_STREAM_TASK_PRIO,      \
    .ext_stack     = true,                      \
    .event_handler = NULL,                      \
    .event_ctx     = NULL,                      \
}

audio_element_handle_t udp_stream_init(udp_stream_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif // __UDP_STREAM_H__

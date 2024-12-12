#ifndef __UDP_MUSIC_H__
#define __UDP_MUSIC_H__

#include "stdio.h"
#include "stdint.h"
#include "esp_err.h"

// udp music message buffer PSRAM
#define UDP_MUSIC_MSG_BUF_PSRAM         0
// udp music message buffer size
#define UDP_MUSIC_MSG_BUF_SIZE          1024 * 20
// udp music message recv timeout [ms]
#define UDP_MUSIC_MSG_BUF_RECV_TIMEOUT   50
// udp music message send timeout [ms]
#define UDP_MUSIC_MSG_BUF_SEND_TIMEOUT   0
// udp music base64 buffer size
#define UDP_MUSIC_BASE64_BUF_SIZE        1024 * 2
// udp music gzip buffer size
#define UDP_MUSIC_GZIP_BUF_SIZE          1024 * 20
// udp music player timeout [ms]
#define UDP_MUSIC_PLAYER_TIMEOUT         3000
// udp music player idle buffer size
#define UDP_MUSIC_PLAYER_IDLE_BUF_SIZE   20 * 1024
// udp music player max buffer size
#define UDP_MUSIC_PLAYER_MAX_BUF_SIZE    320 * 1024
// udp music player max bit rate
#define UDP_MUSIC_PLAYER_MAX_BIT_RATE    512000

typedef enum {
    UDP_MUSIC_EVENT_TYPE_START,
    UDP_MUSIC_EVENT_TYPE_STOP,
} udp_music_event_type_t;

typedef struct {
    udp_music_event_type_t type;
    void* data;
} udp_music_event_t;

typedef char* (*udp_music_get_mac_t)(void);
typedef int (*udp_music_get_volume_t)(void);
typedef char* (*udp_music_event_cb_t)(udp_music_event_t* event);

typedef struct {
    struct {
        char* ip;
        uint16_t port;
    } recv;
    udp_music_get_mac_t get_mac;
    udp_music_get_volume_t get_volume;
    udp_music_event_cb_t event_cb;
} udp_music_conf_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t udp_music_init(udp_music_conf_t* conf);

#ifdef __cplusplus
}
#endif
#endif // __UDP_MUSIC_H__

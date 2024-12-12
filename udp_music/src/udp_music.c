#include "udp_music.h"
#include "string.h"
#include "stdint.h"

#include "FreeRTOS/FreeRTOS.h"
#include "FreeRTOS/task.h"
#include "FreeRTOS/semphr.h"
#include "FreeRTOS/event_groups.h"
#include "freertos/ringbuf.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_random.h"
#include "sys/types.h"
#include "sys/socket.h"

#include "mbedtls/base64.h"
#include "gzip_inflate.h"
#include "udp_stream.h"

#include "audio_common.h"
#include "board.h"
#include "esp_peripherals.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "wav_decoder.h"
#include "pcm_decoder.h"
#include "audio_pipeline.h"

static const char *TAG = "udp_music";

#define UDP_MUSIC_JSON_DELETE(object)   if(object) { \
                                            cJSON_Delete(object); \
                                        }

static void udp_recv_task(void* arg);
static void udp_msg_task(void* arg);
static void udp_player_task(void* arg);
static void udp_status_task(void* arg);
static esp_err_t udp_music_proto_handler(char* msg);
static esp_err_t udp_music_proto_search_handler(cJSON* root);
static esp_err_t udp_music_proto_heartbeat_handler(cJSON* root);
static esp_err_t udp_music_proto_start_handler(cJSON* root);
static esp_err_t udp_music_proto_stop_handler(cJSON* root);
static esp_err_t udp_music_proto_status_handler(cJSON* root);
static esp_err_t _udp_music_proto_notify_status(int code, char* msg);

// music player status
#define UDP_MUSIC_STATUS_START  (BIT0)
#define UDP_MUSIC_STATUS_STOP   (BIT1)
#define UDP_MUSIC_STATUS_BUSY   (BIT2)
#define UDP_MUSIC_STATUS_SYNC   (BIT3)

typedef struct {
    char        ip[16];
    uint16_t    port;
} udp_music_addr_t;

typedef struct {
    int       format; // 0:mp3, 1:wav, 2:pcm
    int       rate; // 8000, 16000, 24000, 32000, 44100, 48000
    int       channel; // 1, 2
    int       bits; // 8, 16, 24, 32
    int       bit_rate;
    int       buff_size;
} udp_music_play_t;

typedef struct {
    udp_music_addr_t    send;
    udp_music_addr_t    recv;
    udp_music_addr_t    music;
    char*               task_id;
    udp_music_play_t    play;
    SemaphoreHandle_t   mutex;
    RingbufHandle_t     msg_buf;
    udp_music_get_mac_t get_mac;
    udp_music_get_volume_t get_volume;
    udp_music_event_cb_t event_cb;
    EventGroupHandle_t  status;
} udp_music_desc_t;

static udp_music_desc_t s_desc;

typedef enum {
    UDP_MUSIC_PROTO_SEARCH,
    UDP_MUSIC_PROTO_HEARTBEAT,
    UDP_MUSIC_PROTO_START,
    UDP_MUSIC_PROTO_STOP,
    UDP_MUSIC_PROTO_STATUS,
    UDP_MUSIC_PROTO_MAX,
} udp_music_proto_t;

static char* s_proto_list[] = {
    [UDP_MUSIC_PROTO_SEARCH] = "search",
    [UDP_MUSIC_PROTO_HEARTBEAT] = "heartbeat",
    [UDP_MUSIC_PROTO_START] = "start",
    [UDP_MUSIC_PROTO_STOP] = "stop",
    [UDP_MUSIC_PROTO_STATUS] = "status"
};

static udp_music_proto_t _udp_music_get_proto(char* method)
{
    udp_music_proto_t proto = UDP_MUSIC_PROTO_MAX;
    if(method == NULL) {
        return proto;
    }
    for(int i = 0; i < UDP_MUSIC_PROTO_MAX; i++) {
        if(strcmp(method, s_proto_list[i]) == 0) {
            proto = (udp_music_proto_t)i;
            break;
        }
    }
    return proto;
}

static char* _udp_music_get_proto_str(udp_music_proto_t proto)
{
    if(proto >= UDP_MUSIC_PROTO_MAX) {
        return "unknown";
    }
    return s_proto_list[proto];
}

static void _udp_music_lock(void)
{
    if(s_desc.mutex) {
        xSemaphoreTake(s_desc.mutex, portMAX_DELAY);
    }
}

static void _udp_music_unlock(void)
{
    if(s_desc.mutex) {
        xSemaphoreGive(s_desc.mutex);
    }
}

static esp_err_t _udp_music_ip_valid(char* addr)
{
    esp_ip4_addr_t ip4_addr;
    return esp_netif_str_to_ip4(addr, &ip4_addr);
}

static esp_err_t _udp_music_port_valid(uint16_t port)
{
    return (port > 0) && (port < 65535) ? ESP_OK : ESP_FAIL;
}

static esp_err_t _udp_music_format_valid(int format)
{
    static int format_list[] = { 0, 1, 2 };
    for(int i = 0; i < sizeof(format_list) / sizeof(format_list[0]); i++) {
        if(format == format_list[i]) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t _udp_music_rate_valid(int rate)
{
    static int rate_list[] = { 8000, 16000, 32000, 44100, 48000 };
    for(int i = 0; i < sizeof(rate_list) / sizeof(rate_list[0]); i++) {
        if(rate == rate_list[i]) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t _udp_music_channel_valid(int channel)
{
    static int channel_list[] = { 1, 2 };
    for(int i = 0; i < sizeof(channel_list) / sizeof(channel_list[0]); i++) {
        if(channel == channel_list[i]) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t _udp_music_bits_valid(int bits)
{
    static int bits_list[] = { 16, 32 };
    for(int i = 0; i < sizeof(bits_list) / sizeof(bits_list[0]); i++) {
        if(bits == bits_list[i]) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static esp_err_t _udp_music_is_multicast_ip(char* addr)
{
    esp_ip4_addr_t ip4_addr;
    if(esp_netif_str_to_ip4(addr, &ip4_addr) != ESP_OK) {
        return ESP_FAIL;
    }
    if((ip4_addr.addr & 0xE0000000) == 0xE0000000) { // D-class
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t _udp_music_set_send_addr(char* ip, uint16_t port)
{
    if((ip == NULL) || (port == 0)) {
        ESP_LOGE(TAG, "set send addr, ip null or port zero!");
        return ESP_FAIL;
    }
    if(_udp_music_ip_valid(ip) != ESP_OK) {
        ESP_LOGE(TAG, "set send addr, ip invalid, ip=%s", ip);
        return ESP_FAIL;
    }
    _udp_music_lock();
    snprintf(s_desc.send.ip, sizeof(s_desc.send.ip), "%s", ip);
    s_desc.send.port = port;
    _udp_music_unlock();
    ESP_LOGI(TAG, "udp music send addr, ip=%s, port=%d", s_desc.send.ip, s_desc.send.port);
    return ESP_OK;
}

static esp_err_t _udp_music_get_send_addr(char* ip, uint16_t* port)
{
    if((ip == NULL) || (port == NULL)) {
        return ESP_FAIL;
    }
    _udp_music_lock();
    memcpy(ip, s_desc.send.ip, sizeof(s_desc.send.ip));
    *port = s_desc.send.port;
    _udp_music_unlock();
    return ESP_OK;
}

static esp_err_t _udp_music_set_recv_addr(char* ip, uint16_t port)
{
    if((ip == NULL) || (port == 0)) {
        ESP_LOGE(TAG, "set recv addr, ip null or port zero!");
        return ESP_FAIL;
    }
    if(_udp_music_ip_valid(ip) != ESP_OK) {
        ESP_LOGE(TAG, "set recv addr, ip invalid, ip=%s", ip);
        return ESP_FAIL;
    }
    _udp_music_lock();
    snprintf(s_desc.recv.ip, sizeof(s_desc.recv.ip), "%s", ip);
    s_desc.recv.port = port;
    _udp_music_unlock();
    ESP_LOGI(TAG, "udp music recv addr, ip=%s, port=%d", s_desc.recv.ip, s_desc.recv.port);
    return ESP_OK;
}

static esp_err_t _udp_music_get_recv_addr(char* ip, uint16_t* port)
{
    if((ip == NULL) || (port == NULL)) {
        return ESP_FAIL;
    }
    _udp_music_lock();
    memcpy(ip, s_desc.recv.ip, sizeof(s_desc.recv.ip));
    *port = s_desc.recv.port;
    _udp_music_unlock();
    return ESP_OK;
}

static esp_err_t _udp_music_set_music_addr(char* ip, uint16_t port)
{
    if((ip == NULL) || (port == 0)) {
        ESP_LOGE(TAG, "set music addr, ip null or port zero!");
        return ESP_FAIL;
    }
    if(_udp_music_ip_valid(ip) != ESP_OK) {
        ESP_LOGE(TAG, "set music addr, ip invalid, ip=%s", ip);
        return ESP_FAIL;
    }
    _udp_music_lock();
    snprintf(s_desc.music.ip, sizeof(s_desc.music.ip), "%s", ip);
    s_desc.music.port = port;
    _udp_music_unlock();
    ESP_LOGI(TAG, "udp music music addr, ip=%s, port=%d", s_desc.music.ip, s_desc.music.port);
    return ESP_OK;
}

static esp_err_t _udp_music_get_music_addr(char* ip, uint16_t* port)
{
    if((ip == NULL) || (port == NULL)) {
        return ESP_FAIL;
    }
    memcpy(ip, s_desc.music.ip, sizeof(s_desc.music.ip));
    *port = s_desc.music.port;
    return ESP_OK;
}

static esp_err_t _udp_music_set_status(uint32_t status, bool enable)
{
    if(s_desc.status == NULL) {
        return ESP_FAIL;
    }
    EventBits_t bits = status;
    if(enable) {
        xEventGroupSetBits(s_desc.status, bits);
    } else {
        xEventGroupClearBits(s_desc.status, bits);
    }
    return ESP_OK;
}

static esp_err_t _udp_music_set_play_info(udp_music_play_t play)
{
    _udp_music_lock();
    s_desc.play = play;
    _udp_music_unlock();
    return ESP_OK;
}

static udp_music_play_t _udp_music_get_play_info(void)
{
    udp_music_play_t play = { 0 };
    _udp_music_lock();
    play = s_desc.play;
    _udp_music_unlock();
    return play;
}

static esp_err_t _udp_music_get_status(uint32_t status, uint32_t timeout)
{
    if(s_desc.status == NULL) {
        return ESP_FAIL;
    }
    EventBits_t bits = status;
    if(xEventGroupWaitBits(s_desc.status, bits, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout)) & bits){
        return ESP_OK;
    }
    return ESP_FAIL;
}

static char* _udp_music_get_task_id(void)
{
    char* task_id = NULL;
    _udp_music_lock();
    task_id = s_desc.task_id;
    _udp_music_unlock();
    return task_id;
}

static esp_err_t _udp_music_set_task_id(char* task_id)
{
    if(task_id == NULL) {
        return ESP_FAIL;
    }
    _udp_music_lock();
    if(s_desc.task_id) {
        free(s_desc.task_id); // free old task id
    }
    s_desc.task_id = strdup(task_id);
    _udp_music_unlock();
    if(s_desc.task_id) {
        ESP_LOGI(TAG, "udp music task id set success, task_id=%s", s_desc.task_id);
    }
    return ESP_OK;
}

static char* _udp_music_get_mac(void)
{
    char* mac = NULL;
    if(s_desc.get_mac) {
        mac = s_desc.get_mac();
    }
    if(mac == NULL) {
        mac = "";
    }
    return mac;
}

static int _udp_music_get_volume(void)
{
    int volume = 80; // default
    if(s_desc.get_volume) {
        volume = s_desc.get_volume();
    }
    return volume;
}

static char* _udp_music_event_cb(udp_music_event_type_t type, void* data)
{
    char* msg = NULL;
    udp_music_event_t event = {
        .type = type,
        .data = data,
    };
    if(s_desc.event_cb) {
        msg = s_desc.event_cb(&event);
    }
    return msg;
}

static esp_err_t _udp_msg_buf_send(char* msg)
{
    if((msg == NULL) || (s_desc.msg_buf == NULL)) {
        return ESP_FAIL;
    }
    if(xRingbufferSend(s_desc.msg_buf, msg, strlen(msg) + 1, UDP_MUSIC_MSG_BUF_SEND_TIMEOUT) == pdTRUE) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "udp msg buf push failed!");
    return ESP_FAIL;
}

static char* _udp_msg_buf_recv(void)
{
    if(s_desc.msg_buf == NULL) {
        return NULL;
    }
    return (char*)xRingbufferReceive(s_desc.msg_buf, NULL, UDP_MUSIC_MSG_BUF_RECV_TIMEOUT);
}

static esp_err_t _udp_msg_buf_free(char* msg)
{
    if((msg == NULL) || (s_desc.msg_buf == NULL)) {
        return ESP_FAIL;
    }
    vRingbufferReturnItem(s_desc.msg_buf, (void*)msg);
    return ESP_OK;
}

/**
 * @brief udp music init
 * 
 * @param ip : ip for recv msg
 * @param port : port for recv msg
 * @return esp_err_t 
 */
esp_err_t udp_music_init(udp_music_conf_t* conf)
{
    if(conf == NULL) {
        ESP_LOGE(TAG, "udp music conf is null!");
        return ESP_FAIL;
    }
    s_desc.get_mac = conf->get_mac;
    s_desc.get_volume = conf->get_volume;
    s_desc.event_cb = conf->event_cb;
    if(_udp_music_set_recv_addr(conf->recv.ip, conf->recv.port) != ESP_OK) {
        ESP_LOGE(TAG, "udp music ip or port invalid, ip=%s, port=%d", conf->recv.ip, conf->recv.port);
        return ESP_FAIL;
    }
    s_desc.mutex = xSemaphoreCreateMutex();
    if(s_desc.mutex == NULL) {
        ESP_LOGE(TAG, "mutex create failed!");
        return ESP_FAIL;
    }
    s_desc.status = xEventGroupCreate();
    if(s_desc.status == NULL) {
        ESP_LOGE(TAG, "status create failed!");
        return ESP_FAIL;
    }
#if UDP_MUSIC_MSG_BUF_PSRAM
    StaticRingbuffer_t *buff_struct = (StaticRingbuffer_t *)heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buff_storage = (uint8_t *)heap_caps_malloc(UDP_MUSIC_MSG_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_desc.msg_buf = xRingbufferCreateStatic(UDP_MUSIC_MSG_BUF_SIZE, RINGBUF_TYPE_NOSPLIT, buff_storage, buff_struct);
#else
    StaticRingbuffer_t *buff_struct = (StaticRingbuffer_t *)malloc(sizeof(StaticRingbuffer_t));
    uint8_t *buff_storage = (uint8_t *)malloc(UDP_MUSIC_MSG_BUF_SIZE);
    s_desc.msg_buf = xRingbufferCreateStatic(UDP_MUSIC_MSG_BUF_SIZE, RINGBUF_TYPE_NOSPLIT, buff_storage, buff_struct);
#endif // UDP_MUSIC_MSG_BUF_PSRAM
    xTaskCreate(udp_recv_task, "udp_recv", 4096, NULL, 4, NULL);
    xTaskCreate(udp_msg_task, "udp_msg", 4096, NULL, 5, NULL);
    xTaskCreate(udp_player_task, "udp_player", 4096, NULL, 6, NULL);
    xTaskCreate(udp_status_task, "udp_status", 4096, NULL, 4, NULL);
    return ESP_OK;
}

static int _udp_music_recv_socket_create(char* ip, uint16_t port, bool is_mreq)
{
    if((ip == NULL) || (port == 0)) {
        ESP_LOGE(TAG, "udp music recv ip null or port zero!");
        return -1;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0) {
        ESP_LOGE(TAG, "udp music recv socket create failed!");
        goto create_failed;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "udp music recv socket bind failed!");
        goto create_failed;
    }
    int opt = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(TAG, "udp music recv socket set reuse addr failed!");
        goto create_failed;
    }
    if(is_mreq) {
        struct ip_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = inet_addr(ip);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if(setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            ESP_LOGE(TAG, "udp music recv socket set multicast membership failed!");
            goto create_failed;
        }
    }
    ESP_LOGI(TAG, "udp music recv socket create success, ip=%s, port=%d", ip, port);
    return sock;
create_failed:
    if(sock > 0) {
        close(sock);
    }
    return -1;
}

static int _udp_music_recv_socket_destroy(int sock)
{
    if(sock > 0) {
        close(sock);
    }
    return 0;
}

/**
 * @brief udp recv task
 * 
 * @note udp msg max length is 1472
 */
static void udp_recv_task(void* arg)
{
    udp_music_addr_t addr = { 0 };
    _udp_music_get_recv_addr(addr.ip, &addr.port);
    bool is_mreq = _udp_music_is_multicast_ip(addr.ip) == ESP_OK;
    size_t length = 1024 * 2;
#if UDP_MUSIC_MSG_BUF_PSRAM
    char* buffer = (char*)heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    char* buffer = (char*)malloc(length);
#endif // UDP_MUSIC_MSG_BUF_PSRAM
    ESP_LOGI(TAG, "udp music recv task start, ip=%s, port=%d, mreq=%d", addr.ip, addr.port, is_mreq);
    while (1)
    {
        // create socket
        int sock = _udp_music_recv_socket_create(addr.ip, addr.port, is_mreq);
        if(sock < 0) {
            vTaskDelay(1000);
            continue;
        }
        // recv msg
        while(1) {
            int r_len = recv(sock, buffer, length - 1, 0);
            if(r_len < 0) {
                if(errno == EAGAIN || errno == EWOULDBLOCK) {
                    ESP_LOGW(TAG, "udp music recv socket recv timeout!");
                    continue;
                }
                ESP_LOGE(TAG, "udp music recv socket recv error, errno=%d, reason=%s!", errno, strerror(errno));
                break;
            }
            if(r_len > 0) {
                buffer[r_len] = '\0'; // make string
                _udp_msg_buf_send(buffer);
            }
        }
        _udp_music_recv_socket_destroy(sock);
    }
    free(buffer);
    ESP_LOGW(TAG, "udp music recv task exit!");
    vTaskDelete(NULL);
}

static void udp_msg_task(void* arg)
{
    ESP_LOGI(TAG, "udp music msg task start!");
    while (1)
    {
        char* msg = _udp_msg_buf_recv();
        if(msg == NULL) {
            continue;
        }
        ESP_LOGI(TAG, "udp msg [%d]: %s", strlen(msg), msg);
        udp_music_proto_handler(msg);
        _udp_msg_buf_free(msg);
    }
    ESP_LOGW(TAG, "udp music msg task exit!");
    vTaskDelete(NULL);
}

static void udp_player_task(void* arg)
{
    ESP_LOGI(TAG, "udp music player task start!");
    while(1) {
        _udp_music_set_status(UDP_MUSIC_STATUS_BUSY, false);
        if(_udp_music_get_status(UDP_MUSIC_STATUS_START, portMAX_DELAY) != ESP_OK) {
            ESP_LOGE(TAG, "udp music get start failed!");
            continue;
        }
        _udp_music_set_status(UDP_MUSIC_STATUS_BUSY, true);
        _udp_music_set_status(UDP_MUSIC_STATUS_STOP, false);
        _udp_music_set_status(UDP_MUSIC_STATUS_START, false);
        udp_music_addr_t addr = { 0 };
        _udp_music_get_music_addr(addr.ip, &addr.port);
        if(_udp_music_ip_valid(addr.ip) != ESP_OK || _udp_music_port_valid(addr.port) != ESP_OK) {
            ESP_LOGE(TAG, "udp music music addr invalid, ip=%s, port=%d", addr.ip, addr.port);
            _udp_music_proto_notify_status(1, "播放地址有误");
            continue;
        }
        udp_music_play_t play = _udp_music_get_play_info();
        if((_udp_music_format_valid(play.format) != ESP_OK) || (_udp_music_rate_valid(play.rate) != ESP_OK) || \
            (_udp_music_channel_valid(play.channel) != ESP_OK) || (_udp_music_bits_valid(play.bits) != ESP_OK) || \
            (play.buff_size > (UDP_MUSIC_PLAYER_MAX_BUF_SIZE + UDP_MUSIC_PLAYER_IDLE_BUF_SIZE))) {
            ESP_LOGE(TAG, "udp music play info invalid, format=%d, rate=%d, channel=%d, bits=%d, bit_rate=%d, buff_size=%d", play.format, play.rate, play.channel, play.bits, play.bit_rate, play.buff_size);
            _udp_music_proto_notify_status(1, "播放信息有误");
            continue;
        }
        ESP_LOGW(TAG, "udp music play info, format=%d(0:mp3, 1:wav, 2:pcm), rate=%d, channel=%d, bits=%d, bit_rate=%d, buff_size=%d", play.format, play.rate, play.channel, play.bits, play.bit_rate, play.buff_size);

        char* res = _udp_music_event_cb(UDP_MUSIC_EVENT_TYPE_START, NULL);
        if(res && strlen(res)) {
            _udp_music_proto_notify_status(1, res);
            continue;
        }
        
        _udp_music_proto_notify_status(0, "资源初始化");

        audio_element_handle_t udp_stream = NULL, decoder_stream = NULL, i2s_stream = NULL;
        audio_pipeline_handle_t pipeline = NULL;
        audio_board_handle_t board_handle = audio_board_init();
        if(board_handle == NULL) {
            ESP_LOGE(TAG, "(%s) audio board init failed!", __func__);
            _udp_music_proto_notify_status(1, "播放器初始化失败");
            goto _finish;
        }
        audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
        audio_hal_enable_pa(board_handle->audio_hal, false);
        audio_hal_set_volume(board_handle->audio_hal, _udp_music_get_volume());

        udp_stream_cfg_t udp_cfg = UDP_STREAM_CFG_DEFAULT();
        udp_cfg.host = addr.ip;
        udp_cfg.port = addr.port;
        udp_cfg.use_mreq = _udp_music_is_multicast_ip(addr.ip) == ESP_OK;
        udp_cfg.timeout_ms = UDP_MUSIC_PLAYER_TIMEOUT;
        udp_cfg.out_rb_size = play.buff_size;
        udp_stream = udp_stream_init(&udp_cfg);
        if(udp_stream == NULL) {
            ESP_LOGE(TAG, "(%s) udp stream init failed!", __func__);
            _udp_music_proto_notify_status(1, "reader创建失败");
            goto _finish;
        }
        if(play.format == 1) { // wav
            wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
            decoder_stream = wav_decoder_init(&wav_cfg);
        } else if(play.format == 2) { // pcm
            pcm_decoder_cfg_t pcm_cfg = DEFAULT_PCM_DECODER_CONFIG();
            decoder_stream = pcm_decoder_init(&pcm_cfg);
        } else { // mp3
            mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
            decoder_stream = mp3_decoder_init(&mp3_cfg);
        }
        if(decoder_stream == NULL) {
            ESP_LOGE(TAG, "(%s) decoder init failed!", __func__);
            _udp_music_proto_notify_status(1, "decoder创建失败");
            goto _finish;
        }

        i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
        i2s_cfg.type = AUDIO_STREAM_WRITER;
        i2s_cfg.i2s_config.sample_rate = play.rate;
        i2s_cfg.i2s_config.channel_format = (play.channel == 2) ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT;
        i2s_cfg.i2s_config.bits_per_sample = play.bits;
        i2s_stream = i2s_stream_init(&i2s_cfg);
        if(i2s_stream == NULL) {
            ESP_LOGE(TAG, "(%s) i2s stream init failed!", __func__);
            _udp_music_proto_notify_status(1, "writer创建失败");
            goto _finish;
        }

        audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
        pipeline = audio_pipeline_init(&pipeline_cfg);
        if(pipeline == NULL) {
            ESP_LOGE(TAG, "(%s) pipeline init failed!", __func__);
            _udp_music_proto_notify_status(1, "pipeline创建失败");
            goto _finish;
        }
        audio_pipeline_register(pipeline, udp_stream, "udp_stream");
        audio_pipeline_register(pipeline, decoder_stream, "decoder_stream");
        audio_pipeline_register(pipeline, i2s_stream, "i2s_stream");

        const char *link_tag[3] = {"udp_stream", "decoder_stream", "i2s_stream"};
        audio_pipeline_link(pipeline, &link_tag[0], 3);
        audio_pipeline_run(pipeline);

        _udp_music_proto_notify_status(0, "正在播放");
        audio_hal_enable_pa(board_handle->audio_hal, true);
        audio_element_state_t state = AEL_STATE_NONE;
        uint32_t state_ticks = 0;
        while (1) {
            state = audio_element_get_state(i2s_stream);
            if(state >= AEL_STATE_STOPPED) {
                printf("i2s stream stop, state = %d\n", state);
                break;
            }
            state = audio_element_get_state(udp_stream);
            if(state >= AEL_STATE_ERROR) {
                printf("udp stream stop, state = %d\n", state);
                break;
            }
            if(_udp_music_get_status(UDP_MUSIC_STATUS_STOP, 0) == ESP_OK) {
                printf("udp music player stop\n");
                break;
            }
            state_ticks++;
            if((state_ticks % 5000) == 0) {
                _udp_music_set_status(UDP_MUSIC_STATUS_SYNC, true);
            }
            vTaskDelay(1);
        }
        audio_hal_enable_pa(board_handle->audio_hal, false);
        _udp_music_proto_notify_status(0, "播放结束");

    _finish:
        if(pipeline) {
            audio_pipeline_stop(pipeline);
            audio_pipeline_wait_for_stop(pipeline);
            audio_pipeline_terminate(pipeline);
            audio_pipeline_unregister(pipeline, udp_stream);
            audio_pipeline_unregister(pipeline, decoder_stream);
            audio_pipeline_unregister(pipeline, i2s_stream);
            audio_pipeline_deinit(pipeline);
        }
        if(udp_stream) {
            audio_element_deinit(udp_stream);
        }
        if(decoder_stream) {
            audio_element_deinit(decoder_stream);
        }
        if(i2s_stream) {
            audio_element_deinit(i2s_stream);
        }
        if(board_handle) {
            audio_board_deinit(board_handle);
        }
        _udp_music_event_cb(UDP_MUSIC_EVENT_TYPE_STOP, NULL);
    }
    ESP_LOGW(TAG, "udp music player task exit!");
    vTaskDelete(NULL);
}

static void udp_status_task(void* arg)
{
    ESP_LOGI(TAG, "udp music status task start!");
    while(1) {
        if(_udp_music_get_status(UDP_MUSIC_STATUS_SYNC, portMAX_DELAY) == ESP_OK) {
            _udp_music_proto_notify_status(0, "正在播放");
            _udp_music_set_status(UDP_MUSIC_STATUS_SYNC, false);
        }
    }
    ESP_LOGW(TAG, "udp music status task exit!");
    vTaskDelete(NULL);
}

static esp_err_t _udp_music_send_proto_msg(char* ip, uint16_t port, char* msg)
{
    esp_err_t ret = ESP_FAIL;
    if(msg == NULL) {
        return ret;
    }
    if(_udp_music_ip_valid(ip) != ESP_OK || _udp_music_port_valid(port) != ESP_OK) {
        ESP_LOGE(TAG, "udp music send ip invalid or port invalid");
        return ret;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0) {
        ESP_LOGE(TAG, "udp music send socket create failed!");
        goto _exit;
    }
    int opt = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(TAG, "udp music send socket set reuse addr failed!");
        goto _exit;
    }
    ESP_LOGI(TAG, "udp music send msg, ip=%s, port=%d, msg=%s", ip, port, msg);
    struct sockaddr_in sockaddr;
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = inet_addr(ip);
    if(sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
        ESP_LOGE(TAG, "udp music send socket sendto failed!");
        goto _exit;
    }
    ret = ESP_OK;
_exit:
    if(sock > 0) {
        close(sock);
    }
    return ret;
}

// need free if not null
static char* _udp_music_get_proto_response_json(cJSON* root, char* method, int code, char* msg, cJSON* data)
{
    char* json = NULL;
    cJSON* res = cJSON_CreateObject();
    if(res == NULL) {
        cJSON_Delete(data);
        return json;
    }
    cJSON_AddItemToObject(res, "task_id", cJSON_Duplicate(cJSON_GetObjectItem(root, "task_id"), 1));
    cJSON_AddStringToObject(res, "method", method);
    cJSON_AddNumberToObject(res, "code", code);
    cJSON_AddStringToObject(res, "msg", msg);
    cJSON_AddItemToObject(res, "data", data);
    json = cJSON_PrintUnformatted(res);
    cJSON_Delete(res); // free res and data object
    return json;
}

static esp_err_t _udp_music_proto_update_send_addr(cJSON* root)
{
    if(root == NULL) {
        return ESP_FAIL;
    }
    cJSON* response = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "params"), "response");
    if(response == NULL) {
        return ESP_FAIL;
    }
    char* ip = cJSON_GetStringValue(cJSON_GetObjectItem(response, "ip"));
    uint16_t port = cJSON_GetNumberValue(cJSON_GetObjectItem(response, "port"));
    if(_udp_music_ip_valid(ip) != ESP_OK || _udp_music_port_valid(port) != ESP_OK) {
        ESP_LOGE(TAG, "udp music params response ip invaild or port invaild");
        return ESP_FAIL;
    }
    _udp_music_set_send_addr(ip, port);
    return ESP_OK;
}

static esp_err_t _udp_music_proto_mac_match(char* list)
{
    if(list == NULL) {
        return ESP_FAIL;
    }
    cJSON* root = cJSON_Parse(list);
    if(cJSON_IsArray(root) && cJSON_GetArraySize(root)) {
        for(int i = 0; i < cJSON_GetArraySize(root); i++) {
            char* mac = cJSON_GetStringValue(cJSON_GetArrayItem(root, i));
            if(mac && strcmp(mac, _udp_music_get_mac()) == 0) {
                cJSON_Delete(root);
                ESP_LOGI(TAG, "mac match success.");
                return ESP_OK;
            }
        }
    }
    cJSON_Delete(root);
    ESP_LOGE(TAG, "(%s), mac match failed!", _udp_music_get_mac());
    return ESP_FAIL;
}

static esp_err_t _udp_music_proto_mac_match_success(cJSON* root)
{
    esp_err_t ret = ESP_FAIL;
    if(root == NULL) {
        return ret;
    }
    char* gzip_mac = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "params"), "music"), "mac"));
    if(gzip_mac == NULL) {
        ESP_LOGE(TAG, "udp music mac is null!");
        return ret;
    }
    int in_size = UDP_MUSIC_BASE64_BUF_SIZE;
    int out_size = UDP_MUSIC_GZIP_BUF_SIZE;
#if UDP_MUSIC_GZIP_BUF_PSRAM
    uint8_t* in = (uint8_t*)heap_caps_malloc(in_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t* out = (uint8_t*)heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    uint8_t* in = (uint8_t*)malloc(in_size);
    uint8_t* out = (uint8_t*)malloc(out_size);
#endif // UDP_MUSIC_GZIP_BUF_PSRAM
    if(in == NULL || out == NULL) {
        ESP_LOGE(TAG, "udp music gzip memory malloc failed!");
        goto _exit;
    }
    int in_len = 0;
    if(mbedtls_base64_decode(in, (size_t)in_size, (size_t*)&in_len, (uint8_t*)gzip_mac, (size_t)strlen(gzip_mac)) != 0) {
        ESP_LOGE(TAG, "udp music gzip base64 decode failed, in: %s", gzip_mac);
        goto _exit;
    }
    if(gzip_inflate(in, in_len, out, &out_size) != ESP_OK) {
        ESP_LOGE(TAG, "udp music gzip inflate failed, in: %s", gzip_mac);
        goto _exit;
    }
    out[out_size] = '\0'; // make string
    // ESP_LOGI(TAG, "udp music gzip inflate success, mac list: %s", out);
    ret = _udp_music_proto_mac_match((char*)out);
_exit:
    if(in) {
        free(in);
    }
    if(out) {
        free(out);
    }
    return ret;
}

static esp_err_t udp_music_proto_handler(char* msg)
{
    esp_err_t ret = ESP_FAIL;
    cJSON* root = cJSON_Parse(msg);
    if(root == NULL) {
        ESP_LOGE(TAG, "(%s) json parse failed!", __func__);
        return ret;
    }
    char* method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    udp_music_proto_t proto = _udp_music_get_proto(method);
    ESP_LOGI(TAG, "(%s) msg proto=%d", __func__, proto);
    switch (proto)
    {
    case UDP_MUSIC_PROTO_SEARCH: {
        ret = udp_music_proto_search_handler(root);
    } break;
    case UDP_MUSIC_PROTO_HEARTBEAT: {
        ret = udp_music_proto_heartbeat_handler(root);
    } break;
    case UDP_MUSIC_PROTO_START: {
        ret = udp_music_proto_start_handler(root);
    } break;
    case UDP_MUSIC_PROTO_STOP: {
        ret = udp_music_proto_stop_handler(root);
    } break;
    case UDP_MUSIC_PROTO_STATUS: {
        ret = udp_music_proto_status_handler(root);
    } break;
    default:
        break;
    }
    cJSON_Delete(root);
    return ret;
}

static esp_err_t _udp_music_proto_response(cJSON* root, udp_music_proto_t proto, int code, char* msg)
{
    cJSON* data = cJSON_CreateObject();
    if(data) {
        cJSON_AddStringToObject(data, "mac", _udp_music_get_mac());
        char* json = _udp_music_get_proto_response_json(root, _udp_music_get_proto_str(proto), code, msg, data);
        if(json) {
            char* ip = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "params"), "response"), "ip"));
            uint16_t port = cJSON_GetNumberValue(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "params"), "response"), "port"));
            _udp_music_send_proto_msg(ip, port, json);
            free(json);
        }
        // data object free in _udp_music_get_proto_response_json
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t udp_music_proto_search_handler(cJSON* root)
{
    ESP_LOGI(TAG, "udp proto search");

    // response
    return _udp_music_proto_response(root, UDP_MUSIC_PROTO_SEARCH, 0, "成功");
}

static esp_err_t udp_music_proto_heartbeat_handler(cJSON* root)
{
    ESP_LOGI(TAG, "udp proto heartbeat");

    // response
    return _udp_music_proto_response(root, UDP_MUSIC_PROTO_HEARTBEAT, 0, "成功");
}

static esp_err_t udp_music_proto_start_handler(cJSON* root)
{
    ESP_LOGI(TAG, "udp proto start");

    // mac match
    if(_udp_music_proto_mac_match_success(root) != ESP_OK) {
        return ESP_FAIL;
    }
    // music parse
    cJSON* music = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "params"), "music");
    if(music == NULL) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "music字段缺失");
    }
    char* ip = cJSON_GetStringValue(cJSON_GetObjectItem(music, "ip"));
    if(_udp_music_ip_valid(ip) != ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "ip字段有误");
    }
    uint16_t port = cJSON_GetNumberValue(cJSON_GetObjectItem(music, "port"));
    if(_udp_music_port_valid(port) != ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "port字段有误");
    }
    int format = cJSON_GetNumberValue(cJSON_GetObjectItem(music, "format"));
    if(_udp_music_format_valid(format) != ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "format字段有误");
    }
    int rate = cJSON_GetNumberValue(cJSON_GetObjectItem(music, "rate"));
    if(_udp_music_rate_valid(rate) != ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "rate字段有误");
    }
    int channel = cJSON_GetNumberValue(cJSON_GetObjectItem(music, "channel"));
    if(_udp_music_channel_valid(channel) != ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "channel字段有误");
    }
    int bits = cJSON_GetNumberValue(cJSON_GetObjectItem(music, "bits"));
    if(_udp_music_bits_valid(bits) != ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "bits字段有误");
    }
    int bit_rate = cJSON_GetNumberValue(cJSON_GetObjectItem(music, "bit_rate"));
    if(bit_rate > UDP_MUSIC_PLAYER_MAX_BIT_RATE) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "bit_rate字段有误");
    }
    int buff_size = bit_rate * ((UDP_MUSIC_PLAYER_TIMEOUT + 2000) / 1000) / 8;
    if(buff_size > UDP_MUSIC_PLAYER_MAX_BUF_SIZE) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "bit_rate不支持");
    }
    buff_size += UDP_MUSIC_PLAYER_IDLE_BUF_SIZE;
    // play status
    if(_udp_music_get_status(UDP_MUSIC_STATUS_BUSY, 0) == ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 1, "播放中");
    }
    // success
    _udp_music_set_music_addr(ip, port); // set music addr
    _udp_music_proto_update_send_addr(root); // update send addr
    _udp_music_set_task_id(cJSON_GetStringValue(cJSON_GetObjectItem(root, "task_id")));
    udp_music_play_t play = { .format = format, .rate = rate, .channel = channel, .bits = bits, .bit_rate = bit_rate, .buff_size = buff_size };
    _udp_music_set_play_info(play); // update play info
    _udp_music_set_status(UDP_MUSIC_STATUS_START, true);

    return _udp_music_proto_response(root, UDP_MUSIC_PROTO_START, 0, "成功");
}

static esp_err_t udp_music_proto_stop_handler(cJSON* root)
{
    ESP_LOGI(TAG, "udp proto stop");

    // mac match
    if(_udp_music_proto_mac_match_success(root) != ESP_OK) {
        return ESP_FAIL;
    }
    if(_udp_music_get_status(UDP_MUSIC_STATUS_BUSY, 0) != ESP_OK) {
        return _udp_music_proto_response(root, UDP_MUSIC_PROTO_STOP, 1, "未播放");
    }

    _udp_music_set_status(UDP_MUSIC_STATUS_STOP, true);

    // response
    return _udp_music_proto_response(root, UDP_MUSIC_PROTO_STOP, 0, "成功");
}

static esp_err_t udp_music_proto_status_handler(cJSON* root)
{
    ESP_LOGI(TAG, "udp proto status");

    // response
    return _udp_music_proto_response(root, UDP_MUSIC_PROTO_STATUS, 0, "成功");
}

static esp_err_t _udp_music_proto_notify_status(int code, char* msg)
{
    udp_music_addr_t addr = { 0 };
    _udp_music_get_send_addr(addr.ip, &addr.port);
    if(_udp_music_ip_valid(addr.ip) != ESP_OK || _udp_music_port_valid(addr.port) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* params = cJSON_CreateObject();
    cJSON* response = cJSON_CreateObject();
    if(root == NULL || params == NULL || response == NULL) {
        UDP_MUSIC_JSON_DELETE(root);
        UDP_MUSIC_JSON_DELETE(params);
        UDP_MUSIC_JSON_DELETE(response);
        return ESP_FAIL;
    }
    cJSON_AddItemToObject(root, "params", params);
    cJSON_AddItemToObject(params, "response", response);
    cJSON_AddStringToObject(root, "task_id", _udp_music_get_task_id());
    cJSON_AddStringToObject(response, "ip", addr.ip);
    cJSON_AddNumberToObject(response, "port", addr.port);
    _udp_music_proto_response(root, UDP_MUSIC_PROTO_STATUS, code, msg);
    UDP_MUSIC_JSON_DELETE(root);
    return ESP_OK;
}


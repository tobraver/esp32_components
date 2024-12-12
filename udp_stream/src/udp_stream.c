#include <stdio.h>
#include <string.h>
#include "sys/types.h"
#include "sys/socket.h"
#include "esp_log.h"
#include "esp_err.h"
#include "audio_mem.h"
#include "udp_stream.h"

static const char *TAG = "UDP_STREAM";

typedef struct udp_stream {
    int                           sock;
    audio_stream_type_t           type;
    int                           port;
    char                          *host;
    struct sockaddr_in            addr;
    struct ip_mreq                mreq;
    bool                          is_open;
    bool                          use_mreq;
    int                           timeout_ms;
    udp_stream_event_handle_cb    hook;
    void                          *ctx;
} udp_stream_t;

static int _get_socket_error_code_reason(const char *str, int sockfd)
{
    uint32_t optlen = sizeof(int);
    int result;
    int err;
    err = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &result, &optlen);
    if (err == -1) {
        ESP_LOGE(TAG, "%s, getsockopt failed (%d)", str, err);
        return -1;
    }
    if (result != 0) {
        ESP_LOGW(TAG, "%s error, error code: %d, reason: %s", str, err, strerror(result));
    }
    return result;
}

static esp_err_t _dispatch_event(audio_element_handle_t el, udp_stream_t *udp, void *data, int len, udp_stream_status_t state)
{
    if (el && udp && udp->hook) {
        udp_stream_event_msg_t msg = { 0 };
        msg.data = data;
        msg.data_len = len;
        msg.sock_fd = udp->sock;
        msg.source = el;
        return udp->hook(&msg, state, udp->ctx);
    }
    return ESP_FAIL;
}

static esp_err_t _udp_open(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    if (udp->is_open) {
        ESP_LOGE(TAG, "already opened");
        return ESP_FAIL;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "(%s) socket create failed", __func__);
        return ESP_FAIL;
    }
    memset(&udp->addr, 0, sizeof(udp->addr));
    udp->addr.sin_family = AF_INET;
    udp->addr.sin_port = htons(udp->port);
    if(udp->type == AUDIO_STREAM_READER) {
        udp->addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if(bind(sock, (struct sockaddr *)&udp->addr, sizeof(udp->addr)) < 0) {
            ESP_LOGE(TAG, "(%s) bind failed", __func__);
            _get_socket_error_code_reason(__func__, sock);
            goto _exit;
        }
    } else {
        udp->addr.sin_addr.s_addr = inet_addr(udp->host);
    }
    int opt = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(TAG, "(%s) (%d) set reuse addr failed", __func__, sock);
        _get_socket_error_code_reason(__func__, sock);
        goto _exit;
    }
    struct timeval timeout;
    timeout.tv_sec = udp->timeout_ms / 1000;
    timeout.tv_usec = (udp->timeout_ms % 1000) * 1000;
    if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGE(TAG, "(%s) (%d) set recv timeout failed", __func__, sock);
        _get_socket_error_code_reason(__func__, sock);
        goto _exit;
    }
    if(setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGE(TAG, "(%s) (%d) set send timeout failed", __func__, sock);
        _get_socket_error_code_reason(__func__, sock);
        goto _exit;
    }
    if(udp->use_mreq) {
        memset(&udp->mreq, 0, sizeof(udp->mreq));
        udp->mreq.imr_multiaddr.s_addr = inet_addr(udp->host);
        udp->mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if(setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &udp->mreq, sizeof(udp->mreq)) < 0) {
            ESP_LOGE(TAG, "(%s) (%d) set multicast membership failed", __func__, sock);
            _get_socket_error_code_reason(__func__, sock);
            goto _exit;
        }
    }
    udp->sock = sock;
    udp->is_open = true;
    ESP_LOGI(TAG, "udp open success, sock:%d, port:%d", sock, udp->port);
    _dispatch_event(self, udp, NULL, 0, UDP_STREAM_STATE_OPEN);
    return ESP_OK;
_exit:
    if(sock > 0) {
        close(sock);
    }
    return ESP_FAIL;
}

static esp_err_t _udp_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    int rlen = recv(udp->sock, buffer, len, 0);
    if (rlen < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGW(TAG, "read timeout");
            _dispatch_event(self, udp, NULL, 0, UDP_STREAM_STATE_TIMEOUT);
            return ESP_OK;
        }
        int reason = _get_socket_error_code_reason(__func__, udp->sock);
        _dispatch_event(self, udp, &reason, sizeof(reason), UDP_STREAM_STATE_ERROR);
        return ESP_FAIL;
    }
    audio_element_update_byte_pos(self, rlen);
    ESP_LOGD(TAG, "read len=%d, rlen=%d", len, rlen);
    return rlen;
}

static esp_err_t _udp_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    int wlen = sendto(udp->sock, buffer, len, 0, (struct sockaddr *)&udp->addr, sizeof(udp->addr));
    printf("sendto %d bytes\n", wlen);
    if (wlen < 0) {
        int reason = _get_socket_error_code_reason(__func__, udp->sock);
        _dispatch_event(self, udp, &reason, sizeof(reason), UDP_STREAM_STATE_ERROR);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "write len=%d, rlen=%d", len, wlen);
    return wlen;
}

static esp_err_t _udp_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int r_size = audio_element_input(self, in_buffer, in_len);
    int w_size = 0;
    if (r_size > 0) {
        w_size = audio_element_output(self, in_buffer, r_size);
        if (w_size > 0) {
            audio_element_update_byte_pos(self, r_size);
        }
    } else {
        w_size = r_size;
    }
    return w_size;
}

static esp_err_t _udp_close(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    AUDIO_NULL_CHECK(TAG, udp, return ESP_FAIL);
    if (!udp->is_open) {
        ESP_LOGE(TAG, "already closed");
        return ESP_FAIL;
    }
    close(udp->sock);
    udp->is_open = false;
    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_set_byte_pos(self, 0);
    }
    _dispatch_event(self, udp, NULL, 0, UDP_STREAM_STATE_CLOSE);
    return ESP_OK;
}

static esp_err_t _udp_destroy(audio_element_handle_t self)
{
    AUDIO_NULL_CHECK(TAG, self, return ESP_FAIL);

    udp_stream_t *udp = (udp_stream_t *)audio_element_getdata(self);
    AUDIO_NULL_CHECK(TAG, udp, return ESP_FAIL);
    audio_free(udp);
    return ESP_OK;
}

audio_element_handle_t udp_stream_init(udp_stream_cfg_t *config)
{
    AUDIO_NULL_CHECK(TAG, config, return NULL);

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    audio_element_handle_t el;
    cfg.open = _udp_open;
    cfg.close = _udp_close;
    cfg.process = _udp_process;
    cfg.destroy = _udp_destroy;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.stack_in_ext = config->ext_stack;
    cfg.tag = "udp_stream";
    cfg.out_rb_size = config->out_rb_size;
    if (cfg.buffer_len == 0) {
        cfg.buffer_len = UDP_STREAM_BUF_SIZE;
    }

    udp_stream_t *udp = audio_calloc(1, sizeof(udp_stream_t));
    AUDIO_MEM_CHECK(TAG, udp, return NULL);

    udp->type = config->type;
    udp->port = config->port;
    udp->host = config->host;
    udp->use_mreq = config->use_mreq;
    udp->timeout_ms = config->timeout_ms;
    if (config->event_handler) {
        udp->hook = config->event_handler;
        if (config->event_ctx) {
            udp->ctx = config->event_ctx;
        }
    }

    if (config->type == AUDIO_STREAM_WRITER) {
        cfg.write = _udp_write;
    } else {
        cfg.read = _udp_read;
    }

    el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, goto _udp_init_exit);
    audio_element_setdata(el, udp);

    return el;
_udp_init_exit:
    audio_free(udp);
    return NULL;
}

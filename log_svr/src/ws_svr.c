#include "stdint.h"
#include "ws_svr.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "ws_svr";

typedef struct {
    httpd_handle_t server;
    uint16_t server_port;
} ws_desc_t;

static ws_desc_t s_ws_desc = {
    .server = NULL,
    .server_port = WS_SVR_PORT,
};

esp_err_t _ws_get_conn_client(httpd_handle_t server, int fd[CONFIG_LWIP_MAX_LISTENING_TCP], int* num)
{
    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];
    // get all connect client
    if (httpd_get_client_list(server, &fds, client_fds) != ESP_OK) { 
        return ESP_FAIL;
    }
    // get connect ws client
    for (int i = 0; i < fds; i++) {
        if(httpd_ws_get_fd_info(server, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            fd[*num] = client_fds[i];
            (*num)++;
        }
    }
    return ESP_OK;
}

static esp_err_t _root_max_connect(httpd_req_t *req)
{
    int client_fds[CONFIG_LWIP_MAX_LISTENING_TCP];
    int client_num = 0;
    int client_fd = httpd_req_to_sockfd(req);

    // get connect client
    httpd_handle_t server = req->handle;
    _ws_get_conn_client(server, client_fds, &client_num);

    for (int i = 0; i < client_num; i++) {
        if(client_fds[i] != client_fd) {
            httpd_sess_trigger_close(server, client_fds[i]); // close connect!
            ESP_LOGW(TAG, "ws server disconnect fd: %d", client_fds[i]);
        }
    }
    // listen new connect
    return ESP_OK;
}

static esp_err_t _uri_root_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGW(TAG, "Handshake done, the new connection was opened, socket fd: %d", httpd_req_to_sockfd(req));
        _root_max_connect(req);
        return ESP_OK;
    }
    return ESP_OK; // nothing
}

static const httpd_uri_t s_uri_root = {
    .uri        = "/",
    .method     = HTTP_GET,
    .handler    = _uri_root_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

esp_err_t ws_svr_start(void)
{
    esp_err_t ret = ESP_FAIL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = s_ws_desc.server_port;
    config.max_open_sockets = 2; // one connecting + one connected
    config.ctrl_port = config.ctrl_port + 1;

    ESP_LOGI(TAG, "ws server port: '%d'", config.server_port);
    ret = httpd_start(&s_ws_desc.server, &config);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "ws server start failed, error: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = httpd_register_uri_handler(s_ws_desc.server, &s_uri_root);
    ESP_LOGI(TAG, "ws server start success");
    return ret;
}

esp_err_t ws_svr_stop(void)
{
    if(s_ws_desc.server) {
        httpd_stop(s_ws_desc.server);
    }
    ESP_LOGI(TAG, "ws server stop success");
    return ESP_OK;
}

esp_err_t ws_svr_connected(void)
{
    int client_fds[CONFIG_LWIP_MAX_LISTENING_TCP];
    int client_num = 0;
    _ws_get_conn_client(s_ws_desc.server, client_fds, &client_num);
    return client_num ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_svr_send_text(void* buff, size_t len)
{
    if(s_ws_desc.server == NULL) {
        ESP_LOGE(TAG, "ws server not start");
        return ESP_FAIL;
    }
    if(!(buff && len)) {
        return ESP_OK;
    }
    
    // get connect client
    int client_fds[CONFIG_LWIP_MAX_LISTENING_TCP];
    int client_num = 0;
    _ws_get_conn_client(s_ws_desc.server, client_fds, &client_num);

    // send text to all connect
    httpd_ws_frame_t ws_pkt = { 0 };
    ws_pkt.payload = (uint8_t*)buff;
    ws_pkt.len = len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    for (int i = 0; i < client_num; i++) {
        httpd_ws_send_frame_async(s_ws_desc.server, client_fds[i], &ws_pkt);
    }
    return ESP_OK;
}

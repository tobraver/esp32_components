# 项目说明

基于 ESP-ADF 的设备分区升级，支持分区：model、www、tone、ota。

1. 支持多种分区类型

2. 支持包完整性校验（md5）

3. 支持升级断点续传

4. 支持重复升级检查

注意：断点续传功能，需要服务器支持Range请求。


# 源码适配

修改 `adf/components/aduio_stream/http_stream.c`中`_http_load_uri`函数。修改后，用户设置的Range不会被移除。

```

static esp_err_t _http_load_uri(audio_element_handle_t self, audio_element_info_t* info)
{
    esp_err_t err;
    http_stream_t *http = (http_stream_t *)audio_element_getdata(self);

    esp_http_client_close(http->client);

    _prepare_range(http, info->byte_pos);

    if (dispatch_hook(self, HTTP_STREAM_PRE_REQUEST, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to process user callback");
        return ESP_FAIL;
    }

    if (http->stream_type == AUDIO_STREAM_WRITER) {
        err = esp_http_client_open(http->client, -1);
        if (err == ESP_OK) {
            http->is_open = true;
        }
        return err;
    }

    char *buffer = NULL;
    int post_len = esp_http_client_get_post_field(http->client, &buffer);
_stream_redirect:
    if (http->gzip_encoding) {
        gzip_miniz_deinit(http->gzip);
        http->gzip = NULL;
        http->gzip_encoding = false;
    }
    if ((err = esp_http_client_open(http->client, post_len)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open http stream");
        return err;
    }

    int wrlen = dispatch_hook(self, HTTP_STREAM_ON_REQUEST, buffer, post_len);
    if (wrlen < 0) {
        ESP_LOGE(TAG, "Failed to process user callback");
        return ESP_FAIL;
    }

    if (post_len && buffer && wrlen == 0) {
        if (esp_http_client_write(http->client, buffer, post_len) <= 0) {
            ESP_LOGE(TAG, "Failed to write data to http stream");
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "len=%d, data=%s", post_len, buffer);
    }

    if (dispatch_hook(self, HTTP_STREAM_POST_REQUEST, NULL, 0) < 0) {
        esp_http_client_close(http->client);
        return ESP_FAIL;
    }
    /*
    * Due to the total byte of content has been changed after seek, set info.total_bytes at beginning only.
    */
    int64_t cur_pos = esp_http_client_fetch_headers(http->client);
    audio_element_getinfo(self, info);
    if (info->byte_pos <= 0) {
        info->total_bytes = cur_pos;
        ESP_LOGI(TAG, "total_bytes=%d", (int)info->total_bytes);
        audio_element_set_total_bytes(self, info->total_bytes);
    }
    int status_code = esp_http_client_get_status_code(http->client);
    if (status_code == 301 || status_code == 302) {
        esp_http_client_set_redirection(http->client);
        goto _stream_redirect;
    }
    if (status_code != 200
        && (esp_http_client_get_status_code(http->client) != 206)
        && (esp_http_client_get_status_code(http->client) != 416)) {
        ESP_LOGE(TAG, "Invalid HTTP stream, status code = %d", status_code);
        if (http->enable_playlist_parser) {
            http_playlist_clear(http->playlist);
            http->is_playlist_resolved = false;
        }
        return ESP_FAIL;
    }
    return err;
}
```

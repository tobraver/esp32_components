#include "http_cli.h"

#include "esp_log.h"

static const char* TAG = "http_cli";

// http client response free
#define HTTP_CLI_RESPONSE_MEM_FREE(addr) if(addr) { \
                                            free(addr); \
                                        }

typedef int (*http_response_handler_t)(char* response, char* modify_time);

esp_err_t http_cli_create(http_cli_t* handle)
{
    if(handle == NULL) {
        return ESP_FAIL;
    }
    if( (handle->url == NULL) || (handle->boundary == NULL) || \
        (handle->token == NULL) || (handle->basetoken == NULL) || \
        (handle->module == NULL) || (handle->orgid == NULL) || \
        (handle->mac == NULL) || (handle->file_name == NULL) ) {
        return ESP_FAIL;
    }
    esp_http_client_config_t config = {
        .url = HTTP_CLI_POST_URL(handle),
        .timeout_ms = HTTP_CLI_TIMEOUT_SEC * 1000,
        // .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if(client == NULL) {
        ESP_LOGE(TAG, "client create failed!");
        return ESP_FAIL;
    }

    esp_http_client_set_url(client, config.url);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    char content_type[128] = {0};
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", HTTP_CLI_FORM_BOUNDARY(handle));
    esp_http_client_set_header(client, "Content-Type", content_type);
    ESP_LOGI(TAG, "client create success, url: %s", config.url);
    handle->client = client;
    return ESP_OK;
}

uint32_t http_cli_get_content_len(http_cli_t* handle, uint32_t file_size)
{
    int boundary_len = strlen(HTTP_CLI_FORM_BOUNDARY(handle));
    int body_prefix_len = 2; // \r\n
    int body_suffix_len = boundary_len + 6; // --${boundary}--\r\n
    int body_total_len = 0;
    char* form_prefix = "Content-Disposition: form-data; name=";

    char* key[] = {
        "\"token\"", "\"basetoken\"", "\"module\"", "\"orgid\"", "\"mac\""
    };
    char* value[] = {
        HTTP_CLI_PARAMS_TOKEN(handle), HTTP_CLI_PARAMS_BASETOKEN(handle), HTTP_CLI_FORM_BODY_MODULE(handle),
        HTTP_CLI_FORM_BODY_ORGID(handle), HTTP_CLI_FORM_BODY_MAC(handle)
    };
    // prefix
    body_total_len += body_prefix_len;
    // key + value
    for(int i = 0; i < sizeof(key) / sizeof(key[0]); i++) {
        body_total_len += boundary_len + 4; // --${boundary}\r\n
        body_total_len += strlen(form_prefix) + strlen(key[i]) + 4; // ${form_prefix}${key}\r\n\r\n
        body_total_len += strlen(value[i]) + 2; // ${value}\r\n
    }
    // data
    body_total_len += boundary_len + 4; // --${boundary}\r\n
    body_total_len += strlen(form_prefix) + strlen("\"data\"; filename=\"") + strlen(HTTP_CLI_FORM_FILE_NAME(handle)) + strlen("\"") + 2; // ${form_prefix}${key}\r\n
    body_total_len += strlen("Content-Type: application/gzip") + 4; // ${type}\r\n\r\n
    body_total_len += file_size + 2; // ${value}\r\n
    // suffix
    body_total_len += body_suffix_len;
    return body_total_len;
}

esp_err_t http_cli_form_begin(http_cli_t* handle, uint32_t file_size)
{
    if(handle->client == NULL) {
        ESP_LOGE(TAG, "[form begin] client is null!");
        return ESP_FAIL;
    }
    esp_http_client_handle_t client = handle->client;

    uint32_t content_len = http_cli_get_content_len(handle, file_size);
    esp_err_t ret = esp_http_client_open(client, content_len);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "[form begin] http open failed, error: %s!", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[form begin] http connect success.");
    
    // prefix
    if(esp_http_client_write(client, "\r\n", 2) < 0) {
        goto write_error;
    }
    char* form_prefix = "Content-Disposition: form-data; name=";
    char* key[] = {
        "\"token\"", "\"basetoken\"", "\"module\"", "\"orgid\"", "\"mac\""
    };
    char* value[] = {
        HTTP_CLI_PARAMS_TOKEN(handle), HTTP_CLI_PARAMS_BASETOKEN(handle), HTTP_CLI_FORM_BODY_MODULE(handle),
        HTTP_CLI_FORM_BODY_ORGID(handle), HTTP_CLI_FORM_BODY_MAC(handle)
    };
    char* boundary = HTTP_CLI_FORM_BOUNDARY(handle);
    // key + value
    for(int i = 0; i < sizeof(key) / sizeof(key[0]); i++) {
        // ESP_LOGI(TAG, "[form begin] form key: %s, value: %s", key[i], value[i]);
        if(esp_http_client_write(client, "--", 2) < 0) {
            goto write_error;
        }
        if(esp_http_client_write(client, boundary, strlen(boundary)) < 0) {
            goto write_error;
        }
        if(esp_http_client_write(client, "\r\n", 2) < 0) {
            goto write_error;
        }
        if(esp_http_client_write(client, form_prefix, strlen(form_prefix)) < 0) {
            goto write_error;
        }
        if(esp_http_client_write(client, key[i], strlen(key[i])) < 0) {
            goto write_error;
        }
        if(esp_http_client_write(client, "\r\n\r\n", 4) < 0) {
            goto write_error;
        }
        if(esp_http_client_write(client, value[i], strlen(value[i])) < 0) {
            goto write_error;
        }
        if(esp_http_client_write(client, "\r\n", 2) < 0) {
            goto write_error;
        }
    }
    // file header
    if(esp_http_client_write(client, "--", 2) < 0) {
        goto write_error;
    }
    if(esp_http_client_write(client, boundary, strlen(boundary)) < 0) {
        goto write_error;
    }
    if(esp_http_client_write(client, "\r\n", 2) < 0) {
        goto write_error;
    }
    if(esp_http_client_write(client, form_prefix, strlen(form_prefix)) < 0) {
        goto write_error;
    }
    char* tmp_str = "\"data\"; filename=\"";
    if(esp_http_client_write(client, tmp_str, strlen(tmp_str)) < 0) {
        goto write_error;
    }
    tmp_str = HTTP_CLI_FORM_FILE_NAME(handle);
    if(esp_http_client_write(client, tmp_str, strlen(tmp_str)) < 0) {
        goto write_error;
    }
    tmp_str = "\"\r\nContent-Type: application/gzip\r\n\r\n";
    if(esp_http_client_write(client, tmp_str, strlen(tmp_str)) < 0) {
        goto write_error;
    }
    return ESP_OK;
write_error:
    ESP_LOGI(TAG, "[form begin] write error!");
    return ESP_FAIL;
}

esp_err_t http_cli_form_file_write(http_cli_t* handle, void* buff, size_t len)
{
    esp_http_client_handle_t client = handle->client;
    if(client == NULL) {
        ESP_LOGE(TAG, "[form write] client is null!");
        return ESP_FAIL;
    }

    if(esp_http_client_write(client, (const char*)buff, len) < 0) {
        goto write_error;
    }
    return ESP_OK;
write_error:
    ESP_LOGI(TAG, "[form write] write error!");
    return ESP_FAIL;
}

esp_err_t http_cli_form_finish(http_cli_t* handle)
{
    esp_http_client_handle_t client = handle->client;
    if(client == NULL) {
        ESP_LOGE(TAG, "[form finish] client is null!");
        return ESP_FAIL;
    }

    size_t response_len = HTTP_CLI_RESPONSE_LEN;
    char* response_buf = (char*)calloc(1, response_len + 1);
    if(response_buf == NULL) {
        ESP_LOGE(TAG, "[form finish] malloc response failed!");
        return ESP_FAIL;
    }

    char* boundary = HTTP_CLI_FORM_BOUNDARY(handle);
    // file footer
    if(esp_http_client_write(client, "\r\n", 2) < 0) {
        goto write_error;
    }
    // body suffix
    if(esp_http_client_write(client, "--", 2) < 0) {
        goto write_error;
    }
    if(esp_http_client_write(client, boundary, strlen(boundary)) < 0) {
        goto write_error;
    }
    if(esp_http_client_write(client, "--\r\n", 4) < 0) {
        goto write_error;
    }

    if(esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGE(TAG, "[form finish] http fetch header failed, status = %d", esp_http_client_get_status_code(client));
        HTTP_CLI_RESPONSE_MEM_FREE(response_buf);
        return ESP_FAIL;
    }

    int data_read = esp_http_client_read_response(client, response_buf, response_len);
    if(data_read < 0) {
        ESP_LOGE(TAG, "[form finish] http read response failed, status = %d", esp_http_client_get_status_code(client));
        HTTP_CLI_RESPONSE_MEM_FREE(response_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "[form finish] http response status = %d, content_length = %lld, response = %s",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client),
                response_buf);

    if(handle->response_handler) {
        http_response_handler_t _response_handler = (http_response_handler_t)handle->response_handler;
        _response_handler(response_buf, handle->modify_time);
    }

    HTTP_CLI_RESPONSE_MEM_FREE(response_buf);
    return ESP_OK;
write_error:
    HTTP_CLI_RESPONSE_MEM_FREE(response_buf);
    ESP_LOGI(TAG, "[form finish] write error!");
    return ESP_FAIL;
}

esp_err_t http_cli_destroy(http_cli_t* handle)
{
    if(handle == NULL) {
        return ESP_FAIL;
    }
    HTTP_CLI_RESPONSE_MEM_FREE(handle->url);
    HTTP_CLI_RESPONSE_MEM_FREE(handle->basetoken);
    HTTP_CLI_RESPONSE_MEM_FREE(handle->orgid);

    esp_http_client_handle_t client = handle->client;
    if(client == NULL) {
        ESP_LOGE(TAG, "[destroy] client is null!");
        return ESP_FAIL;
    }
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "client destory success!");
    return ESP_OK;
}


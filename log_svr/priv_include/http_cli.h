#ifndef __HTTP_CLI_H__
#define __HTTP_CLI_H__

#include "stdio.h"
#include "esp_err.h"
#include "esp_http_client.h"

// http timeout in seconds
#define HTTP_CLI_TIMEOUT_SEC  10
// http response buffer length
#define HTTP_CLI_RESPONSE_LEN 4096

typedef struct {
    char* url; // heap memory, free in http_cli_destroy()
    char* boundary; // static memory
    char* token; // static memory
    char* basetoken; // heap memory, free in http_cli_destroy()
    char* module; // static memory
    char* orgid; // heap memory, free in http_cli_destroy()
    char* mac; // static memory
    char* file_name; // static memory
    esp_http_client_handle_t client;
    void* response_handler;
    char* modify_time;
} http_cli_t;

// http post url
#define HTTP_CLI_POST_URL(handle) handle->url
// http form token
#define HTTP_CLI_PARAMS_TOKEN(handle) handle->token
// http form basetoken
#define HTTP_CLI_PARAMS_BASETOKEN(handle) handle->basetoken

// http form boundary
#define HTTP_CLI_FORM_BOUNDARY(handle) handle->boundary
// http form form file name
#define HTTP_CLI_FORM_FILE_NAME(handle) handle->file_name
// http form body module
#define HTTP_CLI_FORM_BODY_MODULE(handle) handle->module
// http form body organization id
#define HTTP_CLI_FORM_BODY_ORGID(handle) handle->orgid
// http form body mac
#define HTTP_CLI_FORM_BODY_MAC(handle) handle->mac

esp_err_t http_cli_create(http_cli_t* handle);
esp_err_t http_cli_form_begin(http_cli_t* handle, uint32_t file_size);
esp_err_t http_cli_form_file_write(http_cli_t* handle, void* buff, size_t len);
esp_err_t http_cli_form_finish(http_cli_t* handle);
esp_err_t http_cli_destroy(http_cli_t* handle);

#endif // __HTTP_CLI_H__

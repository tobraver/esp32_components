#include "log_svr.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "sys/time.h"
#include "audio_thread.h"

#include "rb_log.h"
#include "gzip_deflate.h"
#include "ws_svr.h"
#include "http_cli.h"
#include "file_svr.h"
#include "cJSON.h"

static const char * TAG = "log_svr";

typedef struct {
    uint64_t start;
    uint64_t end;
} log_svr_duration_t;

typedef struct {
    uint32_t type;
    uint64_t timestamp;
} log_trigger_msg_t;

typedef struct {
    int task_exit;
    int trigger_flag;
    log_svr_config_t config;
    SemaphoreHandle_t mutex;
    log_svr_duration_t duration[LOG_SVR_DURATION_NUM];
    QueueHandle_t trigger_queue;
} log_svr_t;

static log_svr_t s_log_desc = {
    .task_exit = 0,
};

// log server ring buffer log threshold [20k], for uploading log cache
#define LOG_SVR_RB_LOG_THRESHOLD  (20 * 1024)

// log server http default handle
#define LOG_SVR_HTTP_DEFAULT_HANDLE()                           \
{                                                               \
    .url = s_log_desc.config.get_url(),                         \
    .boundary = s_log_desc.config.get_boundary(),               \
    .token = s_log_desc.config.get_token(),                     \
    .basetoken = s_log_desc.config.get_basetoken(),             \
    .module = s_log_desc.config.get_module(),                   \
    .orgid = s_log_desc.config.get_orgid(),                     \
    .mac = s_log_desc.config.get_mac(),                         \
    .file_name = s_log_desc.config.get_file_name(),             \
    .response_handler =  s_log_desc.config.response_handler,    \
}

static uint64_t _log_svr_get_current_time(void);
static char* _log_svr_get_datetime(char* buf, size_t len, uint64_t timestamp);
static char* _log_svr_get_modify_time(char* buf, size_t len, uint64_t timestamp);
static bool _log_svr_rate_monitor(void);

static esp_err_t _log_svr_http_stream_out(uint8_t* data, uint32_t len, void* user_ctx)
{
    http_cli_t* http_handle = (http_cli_t*)user_ctx;
    if(http_handle) {
        return http_cli_form_file_write(http_handle, data, len);
    }
    ESP_LOGE(TAG, "log svr http stream out, handle is null");
    return ESP_FAIL;
}

static uint32_t _log_svr_gzip_deflate_msg_size(char** msg_buf, int msg_num)
{
    char* finish_msg = "\n";
    uint32_t deflate_size = 0;
    if(msg_buf == NULL || msg_num <= 0) {
        return deflate_size;
    }
    // deflate create
    gzip_deflate_handle_t gzip_handle = gzip_deflate_create(NULL, NULL);
    if(gzip_handle == NULL) {
        return deflate_size;
    }
    // defalte msg
    for(int i = 0; i < msg_num; i++) {
        if(msg_buf[i]) {
            if(gzip_deflate_write(gzip_handle, (uint8_t*)msg_buf[i], strlen(msg_buf[i]), 0) != ESP_OK) {
                goto deflate_failed;
            }
        }
    }
    // deflate finish msg
    if(gzip_deflate_write(gzip_handle, (uint8_t*)finish_msg, strlen(finish_msg), 1) != ESP_OK) {
        goto deflate_failed;
    }
    // deflate destory
    deflate_size = gzip_handle->zipsize;
deflate_failed:
    gzip_deflate_destroy(gzip_handle);
    return deflate_size;
}

// http server http form msg
static esp_err_t _log_svr_http_form_msg(char** msg_buf, int msg_num)
{
    char* finish_msg = "\n";
    esp_err_t ret = ESP_FAIL;
    gzip_deflate_handle_t gzip_handle = NULL;
    if(msg_buf == NULL || msg_num <= 0) {
        return ret;
    }
    // gzip deflate msg for content length
    size_t content_len = _log_svr_gzip_deflate_msg_size(msg_buf, msg_num);
    if(content_len == 0) {
        ESP_LOGE(TAG, "[form msg] gzip deflate failed!");
        return ret;
    }
    char modify_time[32] = { 0 };
    _log_svr_get_modify_time(modify_time, sizeof(modify_time), _log_svr_get_current_time());
    // http form create
    http_cli_t http_handle = LOG_SVR_HTTP_DEFAULT_HANDLE();
    http_handle.modify_time = modify_time;
    if(http_cli_create(&http_handle) != ESP_OK) {
        ESP_LOGE(TAG, "[form msg] http create failed!");
        return ret;
    }
    // http form begin
    if(http_cli_form_begin(&http_handle, content_len) != ESP_OK) {
        goto _commit_exit;
    }
    // gzip deflate create with http stream out
    gzip_handle = gzip_deflate_create(_log_svr_http_stream_out, &http_handle);
    if(gzip_handle == NULL) {
        ESP_LOGE(TAG, "[form msg] gzip deflate create failed!");
        goto _commit_exit;
    }
    // deflate msg and http write
    for(int i = 0; i < msg_num; i++) {
        if(msg_buf[i] != NULL) {
            if(gzip_deflate_write(gzip_handle, (uint8_t*)msg_buf[i], strlen(msg_buf[i]), 0) != ESP_OK) {
                goto _commit_exit;
            }
        }
    }
    // deflate finish msg and http write
    if(gzip_deflate_write(gzip_handle, (uint8_t*)finish_msg, strlen(finish_msg), 1) != ESP_OK) {
        goto _commit_exit;
    }
    // http form finish
    if(http_cli_form_finish(&http_handle) != ESP_OK) {
        goto _commit_exit;
    }
    ret = ESP_OK;
_commit_exit:
    gzip_deflate_destroy(gzip_handle);
    http_cli_destroy(&http_handle);
    return ret;
}

esp_err_t _log_svr_file_save_msg(char** msg_buf, int msg_num)
{
    if(msg_buf == NULL || msg_num <= 0) {
        return ESP_FAIL;
    }
    // get file desc
    file_svr_desc_t file_desc = { 0 };
    file_svr_get_desc(&file_desc);
    // open file
    if(file_svr_open(&file_desc, "wb+") != ESP_OK) {
        return ESP_FAIL;
    }
    // write msg to file
    for(int i = 0; i < msg_num; i++) {
        if(msg_buf[i]) {
            if(file_svr_write(&file_desc, (uint8_t*)msg_buf[i], strlen(msg_buf[i])) != ESP_OK) {
                ESP_LOGE(TAG, "file write failed!");
                goto _commit_exit;
            }
        }
    }
    // close file
_commit_exit:
    file_svr_close(&file_desc);
    file_svr_next(&file_desc);
    file_svr_set_desc(&file_desc);
    return ESP_OK;
}

static uint32_t _log_svr_gzip_deflate_file_size(file_svr_desc_t* desc)
{
    char* finish_msg = "\n";
    uint32_t deflate_size = 0;
    if(desc == NULL) {
        return deflate_size;
    }
    // deflate create
    gzip_deflate_handle_t gzip_handle = gzip_deflate_create(NULL, NULL);
    if(gzip_handle == NULL) {
        return deflate_size;
    }
    // file open
    if(file_svr_open(desc, "rb") != ESP_OK) {
        goto deflate_failed;
    }
    // file size
    if(file_svr_size(desc) == 0) {
        goto deflate_failed;
    }
    // defalte file
    uint8_t buff[512];
    size_t len = sizeof(buff);
    do {
        len = sizeof(buff);
        if(file_svr_read(desc, buff, &len) != ESP_OK) {
            goto deflate_failed;
        }
        if(len && gzip_deflate_write(gzip_handle, buff, len, 0) != ESP_OK) {
            goto deflate_failed;
        }
    } while(len != 0);
    // deflate finish msg
    if(gzip_deflate_write(gzip_handle, (uint8_t*)finish_msg, strlen(finish_msg), 1) != ESP_OK) {
        goto deflate_failed;
    }
    // deflate destory
    deflate_size = gzip_handle->zipsize;
deflate_failed:
    file_svr_close(desc);
    gzip_deflate_destroy(gzip_handle);
    return deflate_size;
}

// http server http form file
static esp_err_t _log_svr_http_form_file(file_svr_desc_t* desc, uint64_t modify)
{
    char* finish_msg = "\n";
    esp_err_t ret = ESP_FAIL;
    gzip_deflate_handle_t gzip_handle = NULL;
    if(desc == NULL) {
        return ret;
    }
    // gzip deflate file for content length
    size_t content_len = _log_svr_gzip_deflate_file_size(desc);
    if(content_len == 0) {
        ESP_LOGE(TAG, "[form file] gzip deflate failed!");
        return ret;
    }
    // open file
    if(file_svr_open(desc, "rb") != ESP_OK) {
        return ret;
    }
    char modify_time[32] = { 0 };
    _log_svr_get_modify_time(modify_time, sizeof(modify_time), modify);
    // http form create
    http_cli_t http_handle = LOG_SVR_HTTP_DEFAULT_HANDLE();
    http_handle.modify_time = modify_time;
    if(http_cli_create(&http_handle) != ESP_OK) {
        ESP_LOGE(TAG, "[form file] http create failed!");
        return ret;
    }
    // http form begin
    if(http_cli_form_begin(&http_handle, content_len) != ESP_OK) {
        goto _commit_exit;
    }
    // gzip deflate create with http stream out
    gzip_handle = gzip_deflate_create(_log_svr_http_stream_out, &http_handle);
    if(gzip_handle == NULL) {
        ESP_LOGE(TAG, "[form file] gzip deflate create failed!");
        goto _commit_exit;
    }
    // deflate file and http write
    uint8_t buff[512];
    size_t len = sizeof(buff);
    do {
        len = sizeof(buff);
        if(file_svr_read(desc, buff, &len) != ESP_OK) {
            goto _commit_exit;
        }
        if(len && gzip_deflate_write(gzip_handle, buff, len, 0) != ESP_OK) {
            goto _commit_exit;
        }
    } while(len != 0);
    // deflate finish msg and http write
    if(gzip_deflate_write(gzip_handle, (uint8_t*)finish_msg, strlen(finish_msg), 1) != ESP_OK) {
        goto _commit_exit;
    }
    // http form finish
    if(http_cli_form_finish(&http_handle) != ESP_OK) {
        goto _commit_exit;
    }
    ret = ESP_OK;
_commit_exit:
    gzip_deflate_destroy(gzip_handle);
    http_cli_destroy(&http_handle);
    file_svr_close(desc);
    return ret;
}

static esp_err_t _log_svr_file_get_support(void)
{
    esp_err_t ret = ESP_FAIL;
    if(s_log_desc.config.get_file_support) {
        ret = s_log_desc.config.get_file_support() ? ESP_FAIL : ESP_OK;
    }
    return ret;
}

static esp_err_t _log_svr_upload_file(void)
{
    if(_log_svr_file_get_support() != ESP_OK) {
        ESP_LOGW(TAG, "file upload not support!");
        return ESP_FAIL;
    }
    
    file_svr_desc_t desc = { 0 };
    if(file_svr_get_desc(&desc) != ESP_OK) {
        ESP_LOGW(TAG, "file get desc failed!");
        return ESP_FAIL;
    }

    uint64_t modify_time[FILE_SVR_PATH_MAX_NUM];
    for(int i = 0; i < FILE_SVR_PATH_MAX_NUM; i++) {
        modify_time[i] = desc.m_modify_time[i];
    }

    ESP_LOGI(TAG, "file upload start...");
    for(int i = 0; i < FILE_SVR_PATH_MAX_NUM; i++) {
        file_svr_update(&desc, i);
        if(modify_time[i]) {
            ESP_LOGI(TAG, "upload file index:%d, modify time:%llu", i, modify_time[i]);
            if(_log_svr_http_form_file(&desc, modify_time[i]) == ESP_OK) { // http form file
                modify_time[i] = 0; // upload success
            }
        }
    }

    for(int i = 0; i < FILE_SVR_PATH_MAX_NUM; i++) {
        desc.m_modify_time[i] = modify_time[i];
    }
    file_svr_next(&desc);
    file_svr_set_desc(&desc);
    ESP_LOGI(TAG, "file upload end...");
    return ESP_OK;
}

static void _log_svr_mutex_lock(void)
{
    if(s_log_desc.mutex) {
        xSemaphoreTake(s_log_desc.mutex, portMAX_DELAY);
    }
}

static void _log_svr_mutex_unlock(void)
{
    if(s_log_desc.mutex) {
        xSemaphoreGive(s_log_desc.mutex);
    }
}

static esp_err_t _log_svr_send_trigger_msg(log_trigger_msg_t msg)
{
    esp_err_t ret = ESP_FAIL;
    if(s_log_desc.trigger_queue) {
        ret = xQueueSend(s_log_desc.trigger_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_FAIL;
    }
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "send trigger msg failed!");
    } else {
        ESP_LOGI(TAG, "send trigger msg success, type:%ld, timestamp:%llu", msg.type, msg.timestamp);
    }
    return ret;
}

static esp_err_t _log_svr_recv_trigger_msg(log_trigger_msg_t* msg)
{
    esp_err_t ret = ESP_FAIL;
    if(msg == NULL) {
        return ret;
    }
    if(s_log_desc.trigger_queue) {
        ret = xQueueReceive(s_log_desc.trigger_queue, msg, 0) == pdTRUE ? ESP_OK : ESP_FAIL;; // peek and remove
    }
    // if(ret == ESP_OK) {
    //     ESP_LOGI(TAG, "recv trigger msg success, type:%d, timestamp:%llu", msg->type, msg->timestamp);
    // }
    return ret;
}

static int _log_svr_trigger_finish(uint32_t type, uint64_t timestamp)
{
    int ret = 0;
    if(s_log_desc.config.trigger_finish) {
        ret = s_log_desc.config.trigger_finish(type, timestamp);
    }
    return ret;
}

static uint64_t _log_svr_get_current_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

static uint64_t _log_svr_get_timestamp(char* datetime)
{
    struct tm tm = { 0 };
    uint64_t timestamp = 0;
    if(datetime == NULL || strlen(datetime) == 0) {
        return timestamp;
    }
    if (strptime(datetime, "%Y-%m-%d %H:%M", &tm) == NULL) {
        return timestamp;
    }
    tm.tm_sec = 0;
    // printf("fmt tm y:%d m:%d d:%d, h:%d m:%d s:%d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    // timestamp = mktime(&tm) - 8*60*60; // UTC+8
    timestamp = mktime(&tm); // UTC+8
    return timestamp;
}

static char* _log_svr_get_datetime(char* buf, size_t len, uint64_t timestamp)
{
    time_t now = timestamp;
    struct tm *local = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", local);
    return buf;
}

static char* _log_svr_get_modify_time(char* buf, size_t len, uint64_t timestamp)
{
    time_t now = timestamp;
    struct tm *local = localtime(&now);
    snprintf(buf, len, "%04d%02d%02d%02d%02d%02d", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec);
    return buf;
}

static esp_err_t _log_svr_set_duration(char* json)
{
    if(json == NULL) {
        ESP_LOGE(TAG, "json is null!");
        return ESP_FAIL;
    }

    // printf("# log svr set duration: %s\n", json);
    _log_svr_mutex_lock();
    memset(&s_log_desc.duration, 0, sizeof(s_log_desc.duration));
    _log_svr_mutex_unlock();
    
    cJSON* root = cJSON_Parse(json);
    if(root == NULL) {
        ESP_LOGE(TAG, "json parse failed!");
        return ESP_FAIL;
    }
    
    _log_svr_mutex_lock();
    if(cJSON_IsArray(root) && cJSON_GetArraySize(root)) {
        int item_size = cJSON_GetArraySize(root);
        item_size = (item_size > LOG_SVR_DURATION_NUM) ? LOG_SVR_DURATION_NUM : item_size;
        for(int i = 0; i < item_size; i++) {
            cJSON* tm = cJSON_GetArrayItem(root, i);
            uint64_t start = _log_svr_get_timestamp(cJSON_GetStringValue(cJSON_GetObjectItem(tm, "st")));
            uint64_t end = _log_svr_get_timestamp(cJSON_GetStringValue(cJSON_GetObjectItem(tm, "et")));
            // printf("index[%d], start: [%llu], end: [%llu]\n", i, start, end);
            if(start && end) {
                s_log_desc.duration[i].start = start;
                s_log_desc.duration[i].end   = end;
            }
        }
    }
    _log_svr_mutex_unlock();

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t _log_svr_get_duration(void)
{
    esp_err_t ret = ESP_FAIL;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t current = tv.tv_sec;
    _log_svr_mutex_lock();
    for(int i = 0; i < LOG_SVR_DURATION_NUM; i++) {
        if((s_log_desc.duration[i].start <= current) && \
           (current <= s_log_desc.duration[i].end)) {
            ret = ESP_OK;
            break;
        }
    }
    _log_svr_mutex_unlock();
    return ret;
}

static esp_err_t _log_svr_get_netif_connect(void)
{
    esp_err_t ret = ESP_FAIL; // 默认输出到文件
    if(s_log_desc.config.get_connect) {
        ret = (s_log_desc.config.get_connect() == 0) ? ESP_OK : ESP_FAIL;
    }
    return ret;
}

static esp_err_t _log_svr_wait_netif_connect(uint32_t timeout)
{
    while (timeout--) {
        if(_log_svr_get_netif_connect() == ESP_OK) {
            return ESP_OK;
        }
        sys_delay_ms(1);
    }
    return ESP_FAIL;
}

static esp_err_t _log_svr_get_rb_full(void)
{
    int free_size = rb_log_get_free_size();
    int threshold = LOG_SVR_RB_LOG_THRESHOLD;
    if(free_size <= threshold) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t _log_svr_drop_rb_msg(int msg_num)
{
    for(int i = 0; i < msg_num; i++) {
        char* msg = rb_log_get_msg();
        if(msg) {
            rb_log_free_msg(msg);
        }
    }
    return ESP_OK;
}

static int _log_svr_get_netif_value(void)
{
    int ret = 0;
    if(s_log_desc.config.get_netif) {
        ret = s_log_desc.config.get_netif();
    }
    return ret;
}

static bool _log_svr_get_login(void)
{
    bool ret = false;
    if(s_log_desc.config.get_login) {
        ret = s_log_desc.config.get_login() ? true : false;
    }
    return ret;
}

static bool _log_svr_get_plugin(void)
{
    bool ret = false;
    if(s_log_desc.config.get_plugin) {
        ret = s_log_desc.config.get_plugin() ? true : false;
    }
    return ret;
}

static bool _log_svr_rate_monitor(void)
{
    bool ret = false;
    static int last_num = 0, curr_num = 0, threshold = 200;
    static int64_t last_time_ms = 0, curr_time_ms = 0;
    static uint64_t last_time = 0, curr_time = 0, interval = 1 * (60 * 60); // 1 hour
    curr_num = rb_log_get_msg_num();
    curr_time = _log_svr_get_current_time();
    curr_time_ms = esp_timer_get_time() / 1000;
    if((last_num == 0) || (last_time_ms == 0)) {
        last_num = curr_num;
        last_time_ms = curr_time_ms;
        return ret;
    }
    if(last_time && (curr_time < (last_time + interval))) {
        last_num = curr_num;
        last_time_ms = curr_time_ms;
        return ret;
    }
    if((curr_num <= last_num) || (curr_time_ms <= last_time_ms)) {
        last_num = curr_num;
        last_time_ms = curr_time_ms;
        return ret;
    }
    int rate = (curr_num - last_num) * 1000 / (curr_time_ms - last_time_ms);
    if(rate > threshold) {
        ret = true;
        ESP_LOGW(TAG, "log svr rate is too fast (%d/sec), threshold: (%d/sec), last_num: %d, curr_num: %d, last_time: %llu, curr_time: %llu", rate, threshold, last_num, curr_num, last_time_ms, curr_time_ms);
        last_time = curr_time;
    }
    last_num = curr_num;
    last_time_ms = curr_time_ms;
    return ret;
}

void log_svr_task(void * arg)
{
    ESP_LOGI(TAG, "log svr task start");
    bool is_login = false;
    bool is_plugin = false;
    bool is_eth = _log_svr_get_netif_value() ? false : true;
    bool is_fast = false;
    log_trigger_msg_t trigger_msg = { 0 };
    char msg_header[128] = { 0 };
    char curr_time[64] = { 0 };

    while (!s_log_desc.task_exit)
    {
        is_fast = _log_svr_rate_monitor();
        // eth plugin
        if(is_plugin == false) {
            is_plugin = _log_svr_get_plugin();
            if(is_plugin && is_eth) {
                ws_svr_start(); // start ws server
            }
        }
        // ws server connected
        if(is_eth && ws_svr_connected() == ESP_OK) {
            char* msg = rb_log_get_msg();
            if(msg != NULL) {
                ws_svr_send_text(msg, strlen(msg));
                rb_log_free_msg(msg);
            }
            sys_delay_ms(10); continue;
        }

        // first login, upload file
        if(is_login == false) {
            is_login = _log_svr_get_login();
            if(is_login) {
                ESP_LOGW(TAG, "upload file, wait for netif connect...");
                _log_svr_wait_netif_connect(2*60*1000);
                printf("# log svr, device first login, upload local file\n");
                _log_svr_upload_file(); // http upload file
            }
        }
        
        bool is_output = false;
        bool is_trigger = (_log_svr_recv_trigger_msg(&trigger_msg) == ESP_OK) ? true : false;
        bool is_duration = (_log_svr_get_duration() == ESP_OK) ? true : false;
        bool is_rb_full = (_log_svr_get_rb_full() == ESP_OK) ? true : false;
        if(is_trigger) {
            is_output = true;
        } else if(is_rb_full && is_duration){
            is_output = true;
        }
        if(is_rb_full && !(is_trigger || is_duration)) { // full but not trigger or duration
            _log_svr_drop_rb_msg(100); // drop 100 msg
            printf("rb is full, but not trigger or duration, drop some msg.\n");
            continue;
        }
        if(is_output == false) {
            is_output = is_fast;
        }
        if(is_output == false) {
            sys_delay_ms(100); continue;
        }

        printf("start output ring buffer log, full: %d, trigger: %d, duration: %d, fast: %d, ", is_rb_full, is_trigger, is_duration, is_fast);
        
        // can output msg, but not support file system, avoid dropping all msg, when not connect
        bool is_connect = (_log_svr_get_netif_connect() == ESP_OK) ? true : false;
        bool file_support = (_log_svr_file_get_support() == ESP_OK) ? true : false;
        printf("connect: %d, file_support: %d, ", is_connect, file_support);
        if(!file_support && !is_connect) {
            _log_svr_drop_rb_msg(200); // drop 200 msg
            printf("\ndevice not support file system, netif down, drop some msg.\n");
            continue;
        }

        int msg_num = rb_log_get_msg_num();
        if(msg_num <= 0) {
            ESP_LOGE(TAG, "msg output, msg num is %d", msg_num);
            sys_delay_ms(10); continue;
        }
        char** msg_buf = (char**)malloc(msg_num * sizeof(char*)); // free by LOG_SVR_RB_MSG_FREE
        if(msg_buf == NULL) {
            ESP_LOGE(TAG, "msg buffer malloc failed!");
            _log_svr_drop_rb_msg(msg_num); // drop all msg
            sys_delay_ms(10); continue;
        }
        printf("msg num: %d\n", msg_num);
        // featch msg from ring buffer
        for(int i = 0; i < msg_num; i++) {
            msg_buf[i] = rb_log_get_msg();
        }

        // msg header
        snprintf(msg_header, sizeof(msg_header), "## MAC:%s, Time:%s ##\n", s_log_desc.config.get_mac(), _log_svr_get_datetime(curr_time, sizeof(curr_time), _log_svr_get_current_time()));
        char* first_msg = msg_buf[0];
        msg_buf[0] = msg_header;

        // output msg
        bool is_dump_file = false;
        if(is_connect) {
            if(_log_svr_http_form_msg(msg_buf, msg_num) != ESP_OK) { // http form msg
                is_dump_file = true; // http form failed
            }
            if(file_support && is_trigger) {
                _log_svr_upload_file(); // http upload file
            }
        } else { // netif down
            is_dump_file = true;
        }
        if(is_dump_file) {
            if(file_support) {
                _log_svr_file_save_msg(msg_buf, msg_num); // file save msg
            } else {
                ESP_LOGW(TAG, "file output not support!");
            }
        }

        // free msg
        msg_buf[0] = first_msg;
        for(int i = 0; i < msg_num; i++) {
            if(msg_buf[i]) {
                rb_log_free_msg(msg_buf[i]);
            }
        }
        if(msg_buf) {
            free(msg_buf);
        }

        if(is_trigger) {
            _log_svr_trigger_finish(trigger_msg.type, trigger_msg.timestamp);
        }
    }
    ESP_LOGW(TAG, "log svr task stop");
    vTaskDelete(NULL);
}

void log_svr_init(log_svr_config_t config)
{
    s_log_desc.config = config;
    if(!s_log_desc.mutex) {
        s_log_desc.mutex = xSemaphoreCreateMutex();
    }
    if(!s_log_desc.trigger_queue) {
        s_log_desc.trigger_queue = xQueueCreate(10, sizeof(log_trigger_msg_t));
    }
    // xTaskCreate(log_svr_task, "log_svr", 30*1024, NULL, 5, NULL);
    audio_thread_create(NULL, "log_svr", log_svr_task, NULL, 30*1024, 5, true, 0);
    ESP_LOGI(TAG, "log svr init.");
}

void log_svr_deinit(void)
{
    s_log_desc.task_exit = 1;
    ws_svr_stop();
    ESP_LOGI(TAG, "log svr deinit.");
}

void log_svr_trigger_output(uint32_t type, uint64_t timestamp)
{
    log_trigger_msg_t msg = {
        .type = type,
        .timestamp = timestamp
    };
    _log_svr_send_trigger_msg(msg);
}

void log_svr_output_duration(char* json)
{
    _log_svr_set_duration(json);
}

bool log_svr_duration_verify(void)
{
    return _log_svr_get_duration() == ESP_OK ? true : false;
}

bool log_svr_utils_file_upload(const char* module, const char* name, char* content, int len, log_svr_response_handler_t response_handle)
{
    bool ret = false;
    http_cli_t handle = LOG_SVR_HTTP_DEFAULT_HANDLE();
    handle.module = (char*)module;
    handle.file_name = (char*)name;
    handle.response_handler = response_handle;
    if(http_cli_create(&handle) != ESP_OK) {
        ESP_LOGE(TAG, "[utils] http create failed!");
        return ret;
    }
    if(http_cli_form_begin(&handle, len) != ESP_OK) {
        goto _destory;
    }
    if(http_cli_form_file_write(&handle, content, len) != ESP_OK) {
        goto _destory;
    }
    if(http_cli_form_finish(&handle) != ESP_OK) {
        goto _destory;
    }
    ret = true;
_destory:
    http_cli_destroy(&handle);
    ESP_LOGI(TAG, "[utils] http file upload %s", ret ? "success" : LOG_COLOR_E"failed"LOG_RESET_COLOR);
    return ret;
}

esp_err_t log_svr_http_test(void)
{
    esp_err_t ret = ESP_FAIL;
    // http_cli_t handle = {
    //     .url = "https://16.ss360.org/csafety/service/resource/file/fileUpload",
    //     .boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW",
    //     .token = "default_token",
    //     .basetoken = "k1bUNalKBrJskRrxWssoRCAoIF4LlfhDHnhb3s1Inthgz28TdnX6VA==",
    //     .module = "alldevice",
    //     .orgid = "330023960",
    //     .mac = "e4b06385e750",
    //     .file_name = "output.gz",
    // };
    http_cli_t handle = LOG_SVR_HTTP_DEFAULT_HANDLE();
    if(http_cli_create(&handle) != ESP_OK) {
        return ESP_FAIL;
    }

    uint32_t len = 1024 * 100;
    uint8_t* buf = (uint8_t*)malloc(len);
    for(int i = 0; i < len; i++) {
        buf[i] = i % 256;
    }

    if(http_cli_form_begin(&handle, len) != ESP_OK) {
        goto http_destory;
    }

    if(http_cli_form_file_write(&handle, buf, len) != ESP_OK) {
        goto http_destory;
    }

    if(http_cli_form_finish(&handle) != ESP_OK) {
        goto http_destory;
    }

    ret = ESP_OK;
http_destory:
    http_cli_destroy(&handle);
    return ret;
}
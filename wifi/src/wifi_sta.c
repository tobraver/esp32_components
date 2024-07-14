#include "wifi_sta.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"

#include "esp_log.h"

static const char* TAG = "wifi_sta";

/**
 * @brief wifi interface name
*/
#define WIFI_INTERFACE_NAME "wlan0"

/**
 * @brief wifi ipv6 method, if not mask it.
 */
// #define WIFI_IPV6_SUPPORT

/**
 * @brief wifi scan method
 * 
 * @ref wifi_scan_method_t
 */
#define WIFI_SCAN_METHOD WIFI_FAST_SCAN

/**
 * @brief wifi connect ap sort method
 * 
 * @ref wifi_sort_method_t
 */
#define WIFI_CONNECT_AP_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL

/**
 * @brief The minimum rssi to accept in the scan mode.
 * 
 * @note range: -127 ~ 0
 */
#define WIFI_SCAN_RSSI_THRESHOLD -127

/**
 * @brief wifi scan auth mode threshold
 * 
 * @ref wifi_auth_mode_t
 */
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN


typedef struct {
    esp_netif_t*        netif;
    uint8_t             is_init;
    uint8_t             is_connect;
    esp_ip4_addr_t      ip_addr;
    char                ip_str[64];
    char                ssid[32];
    char                password[64];
    SemaphoreHandle_t   lock;
} wifi_desc_t;

/**
 * @brief wifi desc ctrl
 */
static wifi_desc_t s_wifi_desc;

void wifi_desc_lock(void)
{
    if(s_wifi_desc.lock) {
        xSemaphoreTake(s_wifi_desc.lock, portMAX_DELAY);
    }
}

void wifi_desc_unlock(void)
{
    if(s_wifi_desc.lock) {
        xSemaphoreGive(s_wifi_desc.lock);
    }
}

void wifi_desc_reset(void)
{
    wifi_desc_lock();
    s_wifi_desc.netif = NULL;
    s_wifi_desc.is_init = false;
    s_wifi_desc.is_connect = false;
    memset(&s_wifi_desc.ip_addr, 0, sizeof(s_wifi_desc.ip_addr));
    memset(s_wifi_desc.ip_str, 0, sizeof(s_wifi_desc.ip_str));
    memset(s_wifi_desc.ssid, 0, sizeof(s_wifi_desc.ssid));
    memset(s_wifi_desc.password, 0, sizeof(s_wifi_desc.password));
    wifi_desc_unlock();
}

static void on_wifi_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    wifi_desc_lock();
    memcpy(&s_wifi_desc.ip_addr, &event->ip_info.ip, sizeof(s_wifi_desc.ip_addr));
    snprintf(s_wifi_desc.ip_str, sizeof(s_wifi_desc.ip_str), IPSTR, IP2STR(&event->ip_info.ip));
    s_wifi_desc.is_connect = true;
    wifi_desc_unlock();
}

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    wifi_desc_lock();
    s_wifi_desc.is_connect = false;
    memset(&s_wifi_desc.ip_addr, 0, sizeof(s_wifi_desc.ip_addr));
    memset(s_wifi_desc.ip_str, 0, sizeof(s_wifi_desc.ip_str));
    wifi_desc_unlock();
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    ESP_ERROR_CHECK(err);
}

/**
 * @brief wifi start and connect to ap
 * 
 * @param ssid : wifi ssid, max 32 bytes
 * @param password : wifi password, max 64 bytes
 * @return true 
 * @return false 
 */
bool wifi_sta_start(char ssid[32], char password[64])
{
    if(ssid == NULL || password == NULL) {
        ESP_LOGE(TAG, "ssid or password is null");
        return false;
    }

    if(s_wifi_desc.is_init) {
        ESP_LOGW(TAG, "wifi is alreay init");
        return false;
    }

    if(s_wifi_desc.lock == NULL) {
        s_wifi_desc.lock = xSemaphoreCreateMutex();
    }

    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    netif_config.if_desc = WIFI_INTERFACE_NAME;
    netif_config.route_prio = 128;
    s_wifi_desc.netif = esp_netif_create_wifi(WIFI_IF_STA, &netif_config);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_got_ip, NULL));
#ifdef WIFI_IPV6_SUPPORT
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6, NULL));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .scan_method = WIFI_SCAN_METHOD,
            .sort_method = WIFI_CONNECT_AP_SORT_METHOD,
            .threshold.rssi = WIFI_SCAN_RSSI_THRESHOLD,
            .threshold.authmode = WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };
    snprintf((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    snprintf((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);
    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    wifi_desc_lock();
    s_wifi_desc.is_init = true;
    snprintf(s_wifi_desc.ssid, sizeof(s_wifi_desc.ssid), "%s", ssid);
    snprintf(s_wifi_desc.password, sizeof(s_wifi_desc.password), "%s", password);
    wifi_desc_unlock();
    ESP_LOGW(TAG, "wifi start success");
    return true;
}

bool wifi_sta_stop(void)
{
    if(!s_wifi_desc.is_init) {
        ESP_LOGE(TAG, "wifi is not init");
        return false;
    }

    esp_netif_t *wifi_netif = s_wifi_desc.netif;
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_got_ip));
#ifdef CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &on_got_ipv6));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect));
#endif
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_wifi_clear_default_wifi_driver_and_handlers(wifi_netif);
    esp_netif_destroy(wifi_netif);
    wifi_desc_reset();
    ESP_LOGW(TAG, "wifi stop success");
    return true;
}

bool wifi_sta_is_connected(void)
{
    return s_wifi_desc.is_connect ? true : false;
}

bool wifi_sta_wait_connected(uint32_t timeout_ms)
{
    while (timeout_ms--)
    {
        if(s_wifi_desc.is_connect) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

uint32_t wifi_sta_get_ip_addr(void)
{
    return s_wifi_desc.ip_addr.addr;
}

char* wifi_sta_get_ip_str(void)
{
    return s_wifi_desc.ip_str;
}

char* wifi_sta_get_ssid(void)
{
    return s_wifi_desc.ssid;
}

char* wifi_sta_get_password(void)
{
    return s_wifi_desc.password;
}
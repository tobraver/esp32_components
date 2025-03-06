#include "eth.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#if CONFIG_ETH_USE_SPI_ETHERNET
#include "driver/spi_master.h"
#endif // CONFIG_ETH_USE_SPI_ETHERNET

static const char *TAG = "ETH";
static esp_netif_t *g_eth_netif = NULL;
static bool g_eth_connected = false;

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        g_eth_connected = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        g_eth_connected = false;
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        g_eth_connected = false;
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    g_eth_connected = true;
}

void eth_init(void)
{
    // Initialize TCP/IP network interface (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    // Set default handlers to process TCP/IP stuffs
    ESP_ERROR_CHECK(esp_eth_set_default_handlers(eth_netif));
    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_NETWORK_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_NETWORK_ETH_PHY_RST_GPIO;
#if CONFIG_NETWORK_USE_INTERNAL_ETHERNET
    mac_config.smi_mdc_gpio_num = CONFIG_NETWORK_ETH_MDC_GPIO;
    mac_config.smi_mdio_gpio_num = CONFIG_NETWORK_ETH_MDIO_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);
#if CONFIG_NETWORK_ETH_PHY_IP101
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
#elif CONFIG_NETWORK_ETH_PHY_RTL8201
    esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
#elif CONFIG_NETWORK_ETH_PHY_LAN8720
    esp_eth_phy_t *phy = esp_eth_phy_new_lan8720(&phy_config);
#elif CONFIG_NETWORK_ETH_PHY_DP83848
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
#elif CONFIG_NETWORK_ETH_PHY_KSZ8041
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz8041(&phy_config);
#endif
#elif CONFIG_ETH_USE_SPI_ETHERNET
    gpio_install_isr_service(0);
    spi_device_handle_t spi_handle = NULL;
    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_NETWORK_ETH_SPI_MISO_GPIO,
        .mosi_io_num = CONFIG_NETWORK_ETH_SPI_MOSI_GPIO,
        .sclk_io_num = CONFIG_NETWORK_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_NETWORK_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
#if CONFIG_NETWORK_USE_DM9051
    spi_device_interface_config_t devcfg = {
        .command_bits = 1,
        .address_bits = 7,
        .mode = 0,
        .clock_speed_hz = CONFIG_NETWORK_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_NETWORK_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_NETWORK_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* dm9051 ethernet driver is based on spi driver */
    eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(spi_handle);
    dm9051_config.int_gpio_num = CONFIG_NETWORK_ETH_SPI_INT_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dm9051(&phy_config);
#elif CONFIG_NETWORK_USE_W5500
    spi_device_interface_config_t devcfg = {
        .command_bits = 16, // Actually it's the address phase in W5500 SPI frame
        .address_bits = 8,  // Actually it's the control phase in W5500 SPI frame
        .mode = 0,
        .clock_speed_hz = CONFIG_NETWORK_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_NETWORK_ETH_SPI_CS_GPIO,
        .queue_size = 20
    };
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_NETWORK_ETH_SPI_HOST, &devcfg, &spi_handle));
    /* w5500 ethernet driver is based on spi driver */
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(spi_handle);
    w5500_config.int_gpio_num = CONFIG_NETWORK_ETH_SPI_INT_GPIO;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
#endif
#endif // CONFIG_ETH_USE_SPI_ETHERNET
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
#if !CONFIG_NETWORK_USE_INTERNAL_ETHERNET
    /* The SPI Ethernet module might doesn't have a burned factory MAC address, we cat to set it manually.
       02:00:00 is a Locally Administered OUI range so should not be used except when testing on a LAN under your control.
    */
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, (uint8_t[]) {
        0x02, 0x00, 0x00, 0x12, 0x34, 0x56
    }));
#endif
    /* attach Ethernet driver to TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    /* start Ethernet driver state machine */
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    g_eth_netif = eth_netif;
}

void eth_wait_connected(void)
{
    ESP_LOGI(TAG, "Ethernet Waiting IP");
    while (g_eth_connected == false)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Ethernet Got IP Success");
}

bool eth_config(char* ip, char* mask, char* gw, char* dns1, char* dns2)
{
    const char* TAG = "eth_config";
    if(g_eth_netif == NULL)
    {
        return false;
    }
    esp_netif_t * esp_netif = g_eth_netif;

    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
	esp_netif_ip_info_t info = { 0 };
    info.ip.addr = ipaddr_addr(ip);
    info.gw.addr = ipaddr_addr(gw);
    info.netmask.addr = ipaddr_addr(mask);

    esp_err_t err = ESP_OK;
    err = esp_netif_dhcpc_get_status(esp_netif, &status);
    if(err)
    {
        ESP_LOGE(TAG, "DHCPC Get Status Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return false;
    }
    // stop dhcp
    err = esp_netif_dhcpc_stop(esp_netif);
    if(err && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
    {
        ESP_LOGE(TAG, "DHCPC Stop Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return err;
    }
    // set ip info
    err = esp_netif_set_ip_info(esp_netif, &info);
    if(err)
    {
        ESP_LOGE(TAG, "Netif Set IP Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return false;
    }
    // if dhcp
    if(info.ip.addr == 0)
    {
        err = esp_netif_dhcpc_start(esp_netif);
        if(err)
        {
            ESP_LOGE(TAG, "DHCPC Start Failed! 0x%04x, %s", err, esp_err_to_name(err));
            return false;
        }
    }
    // dns server
    esp_netif_dns_info_t dns = { 0 };
	dns.ip.type = ESP_IPADDR_TYPE_V4;
	dns.ip.u_addr.ip4.addr = ipaddr_addr(dns1);
	if(dns.ip.u_addr.ip4.addr && esp_netif_set_dns_info(esp_netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK)
    {
    	ESP_LOGE(TAG, "Set Main DNS Failed!");
    	return false;
    }
    
    dns.ip.u_addr.ip4.addr = ipaddr_addr(dns2);
    if(dns.ip.u_addr.ip4.addr && esp_netif_set_dns_info(esp_netif, ESP_NETIF_DNS_BACKUP, &dns) != ESP_OK)
    {
        ESP_LOGE(TAG, "Set Backup DNS Failed!");
    	return false;
    }
    return true;
}

bool eth_got_ip(void)
{
    return g_eth_connected;
}

char* eth_get_ip(void)
{
    const char* TAG = "eth_get_ip";
    if(g_eth_netif == NULL)
    {
        return NULL;
    }
    esp_netif_t * esp_netif = g_eth_netif;

    esp_netif_ip_info_t info = { 0 };
    esp_err_t err = esp_netif_get_ip_info(esp_netif, &info);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Netif Get IP Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return NULL;
    }
    
    ip4_addr_t addr = {
        .addr = info.ip.addr,
    };
    static char str[IP4ADDR_STRLEN_MAX];
    return ip4addr_ntoa_r(&addr, str, IP4ADDR_STRLEN_MAX);
}

char* eth_get_mask(void)
{
    const char* TAG = "eth_get_mask";
    if(g_eth_netif == NULL)
    {
        return NULL;
    }
    esp_netif_t * esp_netif = g_eth_netif;

    esp_netif_ip_info_t info = { 0 };
    esp_err_t err = esp_netif_get_ip_info(esp_netif, &info);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Netif Get IP Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return NULL;
    }
    
    ip4_addr_t addr = {
        .addr = info.netmask.addr,
    };
    static char str[IP4ADDR_STRLEN_MAX];
    return ip4addr_ntoa_r(&addr, str, IP4ADDR_STRLEN_MAX);
}

char* eth_get_gw(void)
{
    const char* TAG = "eth_get_gw";
    if(g_eth_netif == NULL)
    {
        return NULL;
    }
    esp_netif_t * esp_netif = g_eth_netif;

    esp_netif_ip_info_t info = { 0 };
    esp_err_t err = esp_netif_get_ip_info(esp_netif, &info);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Netif Get IP Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return NULL;
    }
    
    ip4_addr_t addr = {
        .addr = info.gw.addr,
    };
    static char str[IP4ADDR_STRLEN_MAX];
    return ip4addr_ntoa_r(&addr, str, IP4ADDR_STRLEN_MAX);
}

char* eth_get_dns1(void)
{
    const char* TAG = "eth_get_dns1";
    if(g_eth_netif == NULL)
    {
        return NULL;
    }
    esp_netif_t * esp_netif = g_eth_netif;

    esp_netif_dns_info_t dns = { 0 };
    esp_err_t err = esp_netif_get_dns_info(esp_netif, ESP_NETIF_DNS_MAIN, &dns);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Netif Get DNS Info Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGW("DNS Main Servers", IPSTR, IP2STR(&dns.ip.u_addr.ip4));

    ip4_addr_t addr = {
        .addr = dns.ip.u_addr.ip4.addr,
    };
    static char str[IP4ADDR_STRLEN_MAX];
    return ip4addr_ntoa_r(&addr, str, IP4ADDR_STRLEN_MAX);
}

char* eth_get_dns2(void)
{
    const char* TAG = "eth_get_dns2";
    if(g_eth_netif == NULL)
    {
        return NULL;
    }
    esp_netif_t * esp_netif = g_eth_netif;

    esp_netif_dns_info_t dns = { 0 };
    esp_err_t err = esp_netif_get_dns_info(esp_netif, ESP_NETIF_DNS_BACKUP, &dns);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "Netif Get DNS Info Failed! 0x%04x, %s", err, esp_err_to_name(err));
        return NULL;
    }

    ESP_LOGW("DNS Backup Servers", IPSTR, IP2STR(&dns.ip.u_addr.ip4));

    ip4_addr_t addr = {
        .addr = dns.ip.u_addr.ip4.addr,
    };
    static char str[IP4ADDR_STRLEN_MAX];
    return ip4addr_ntoa_r(&addr, str, IP4ADDR_STRLEN_MAX);
}



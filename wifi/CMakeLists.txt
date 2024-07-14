idf_component_register(
    SRCS "src/wifi_sta.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif
)

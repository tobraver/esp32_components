idf_component_register(
    SRCS "src/mqtt_tcp_client.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif mqtt
)

idf_component_register(
    SRCS "src/mqtt_client.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_netif mqtt
)

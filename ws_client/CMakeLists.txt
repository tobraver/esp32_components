idf_component_register(
    SRCS "src/ws_client.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_netif esp_websocket_client
)

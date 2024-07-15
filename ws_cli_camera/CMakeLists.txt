idf_component_register(
    SRCS "src/ws_camera.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ws_client wifi
)

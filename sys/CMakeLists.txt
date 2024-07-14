idf_component_register(
    SRCS "src/sys.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log esp_system esp_timer
)

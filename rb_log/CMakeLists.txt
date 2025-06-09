idf_component_register(
    SRCS "src/rb_log.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log heap esp_ringbuf driver
)

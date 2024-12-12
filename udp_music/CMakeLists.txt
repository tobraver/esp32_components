idf_component_register(
    SRCS "src/udp_music.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES udp_stream json mbedtls gzip_inflate audio_board esp_peripherals
)

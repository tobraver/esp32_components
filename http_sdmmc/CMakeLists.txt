idf_component_register(
    SRCS "src/http_sdmmc.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES audio_board audio_hal audio_pipeline audio_sal audio_stream esp_peripherals
)

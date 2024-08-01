idf_component_register(
    SRCS "src/http_ota.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log audio_board audio_sal ota_service esp_dispatcher audio_pipeline audio_stream
)

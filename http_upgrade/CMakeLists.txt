idf_component_register(
    SRCS "src/http_upgrade.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES audio_hal audio_pipeline audio_stream ota_service mbedtls
)

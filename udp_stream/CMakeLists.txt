idf_component_register(
    SRCS "src/udp_stream.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_hal audio_pipeline audio_stream
)

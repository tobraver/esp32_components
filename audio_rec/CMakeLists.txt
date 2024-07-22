idf_component_register(
    SRCS "src/audio_rec.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_board audio_hal audio_pipeline audio_recorder audio_sal audio_stream esp-sr esp-adf-libs esp_peripherals
)


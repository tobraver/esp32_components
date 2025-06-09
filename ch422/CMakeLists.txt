idf_component_register(
    SRCS "src/ch422.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log esp_peripherals audio_sal
)

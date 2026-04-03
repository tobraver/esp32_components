idf_component_register(
    SRCS "example_mc3416_anymotion.c"
    INCLUDE_DIRS "."
    REQUIRES mc3416 driver esp_driver_gpio
)

if(!CONFIG_ESP32S3_SPIRAM_SUPPORT)
    message(STATUS "prefs not psram")
    idf_component_register(
        SRCS "src/prefs.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES nvs_flash
    )
else()
    message(STATUS "prefs with psram")
    idf_component_register(
        SRCS "src/prefs.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES nvs_flash esp_actions esp_dispatcher
    )
endif()

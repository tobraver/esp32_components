idf_component_register(
    SRCS "file_manager.c" "player.c" "pwm_audio.c"
    INCLUDE_DIRS "."
    REQUIRES main
)

# if use pwm music
# spiffs_create_partition_image(audio ${CMAKE_CURRENT_SOURCE_DIR}/player/spiffs_image FLASH_IN_PROJECT)
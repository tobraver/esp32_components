idf_component_register(
    SRCS "src/tone_player.c" "src/audio_tone_uri.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES audio_board audio_hal audio_stream audio_sal audio_pipeline esp-adf-libs esp_peripherals
)

# This is a cmake function, which is used to flash the bin file to the specified partition
function(esptool_py_flash_customize_image target_name image_name offset image)
    idf_build_get_property(build_dir BUILD_DIR)
    file(RELATIVE_PATH image ${build_dir} ${image})

    set_property(TARGET ${target_name} APPEND PROPERTY FLASH_FILE
                "\"${offset}\" : \"${image}\"")
    set_property(TARGET ${target_name} APPEND PROPERTY FLASH_ENTRY
                "\"${image_name}\" : { \"offset\" : \"${offset}\", \"file\" : \"${image}\" }")
    set_property(TARGET ${target_name} APPEND PROPERTY IMAGES "${offset} ${image}")

    if(CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT)
        set_property(TARGET encrypted-${target_name} APPEND PROPERTY FLASH_FILE
                    "\"${offset}\" : \"${image}\"")
        set_property(TARGET encrypted-${target_name} APPEND PROPERTY FLASH_ENTRY
                    "\"${image_name}\" : { \"offset\" : \"${offset}\", \"file\" : \"${image}\" }")
        set_property(TARGET encrypted-${target_name} APPEND PROPERTY IMAGES "${offset} ${image}")
    endif()
endfunction()

# Flash the custom partition named `flash_tone`.
set(partition flash_tone)
set(image_file ${CMAKE_CURRENT_LIST_DIR}/audio_tone.bin)
partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")
esptool_py_flash_customize_image(flash "${partition}" "${offset}" "${image_file}")
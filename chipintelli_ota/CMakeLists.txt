idf_component_register(SRCS "chip_ota.c"
                    INCLUDE_DIRS "."
                    REQUIRES main
                    EMBED_TXTFILES ci130x_updater.bin xiaoai.bin)

idf_component_register(SRCS "src/chip_ota.c"
                    INCLUDE_DIRS "inlcude"
                    REQUIRES driver
                    EMBED_TXTFILES bin/ci130x_updater.bin bin/xiaoai.bin)
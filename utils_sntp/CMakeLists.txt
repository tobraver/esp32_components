idf_component_register(
    SRCS "src/utils_sntp.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES lwip log
)

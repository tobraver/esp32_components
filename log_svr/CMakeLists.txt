idf_component_register(
    SRCS "src/log_svr.c" "src/ws_svr.c" "src/http_cli.c" "src/file_svr.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    PRIV_REQUIRES gzip_deflate rb_log esp_http_server esp_http_client json audio_sal
)

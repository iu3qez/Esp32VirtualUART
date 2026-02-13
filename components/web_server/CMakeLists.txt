idf_component_register(
    SRCS "web_server.c" "api_handler.c" "ws_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json port_core routing config_store wifi_mgr status_led log
    PRIV_REQUIRES joltwallet__littlefs
)

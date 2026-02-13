idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES port_core port_cdc port_uart port_tcp routing config_store wifi_mgr web_server status_led nvs_flash log
)

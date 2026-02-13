idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES port_core port_cdc port_uart routing config_store status_led nvs_flash log
)

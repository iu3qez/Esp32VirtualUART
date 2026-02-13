idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES port_core port_cdc status_led nvs_flash log
)

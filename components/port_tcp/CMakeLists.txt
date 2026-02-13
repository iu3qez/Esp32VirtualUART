idf_component_register(
    SRCS "port_tcp.c"
    INCLUDE_DIRS "include"
    REQUIRES port_core freertos log lwip
)

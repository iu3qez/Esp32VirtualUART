idf_component_register(
    SRCS "route_engine.c" "signal_router.c"
    INCLUDE_DIRS "include"
    REQUIRES port_core freertos log
)

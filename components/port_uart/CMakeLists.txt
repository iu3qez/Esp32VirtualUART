idf_component_register(
    SRCS "port_uart.c"
    INCLUDE_DIRS "include"
    REQUIRES port_core freertos log esp_driver_uart esp_driver_gpio
)

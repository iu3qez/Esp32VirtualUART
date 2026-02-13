idf_component_register(
    SRCS "port_cdc.c" "usb_descriptors.c"
    INCLUDE_DIRS "include"
    REQUIRES port_core freertos log
    PRIV_REQUIRES esp_tinyusb
)

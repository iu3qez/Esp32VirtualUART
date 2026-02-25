idf_component_register(
    SRCS "port_cdc.c" "usb_descriptors.c"
    INCLUDE_DIRS "include"
    REQUIRES port_core freertos log
    PRIV_REQUIRES esp_tinyusb usb
)

# Force linker to pull our strong descriptor callbacks from this archive,
# overriding the weak defaults in esp_tinyusb/tinyusb.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-u tud_descriptor_device_cb"
    "-u tud_descriptor_configuration_cb"
    "-u tud_descriptor_string_cb"
    "-u tud_descriptor_device_qualifier_cb"
    "-u tud_descriptor_other_speed_configuration_cb"
)

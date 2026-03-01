idf_component_register(
    SRCS "port_cdc.c" "usb_descriptors.c"
    INCLUDE_DIRS "include"
    REQUIRES port_core freertos log
    PRIV_REQUIRES esp_tinyusb usb
)

# esp_tinyusb's descriptors_control.c provides STRONG (non-weak) definitions
# of tud_descriptor_*_cb. To override them with ours, we must force the linker
# to include ALL objects from this archive via --whole-archive.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    -Wl,--whole-archive
    $<TARGET_FILE:${COMPONENT_LIB}>
    -Wl,--no-whole-archive
)

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_virtual_uart)

# Override TinyUSB CDC count: esp_tinyusb Kconfig caps at 2,
# but ESP32-P4 HS USB supports 6 CDC ports (12 of 16 EPs).
# We patch the generated sdkconfig.h after cmake configures to set CDC count to 6.
# This is done via a custom target that runs before compilation.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/config/.cdc_count_patched
    COMMAND sed -i "s/^#define CONFIG_TINYUSB_CDC_COUNT 2/#define CONFIG_TINYUSB_CDC_COUNT 6/" ${CMAKE_BINARY_DIR}/config/sdkconfig.h
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/config/.cdc_count_patched
    DEPENDS ${CMAKE_BINARY_DIR}/config/sdkconfig.h
    COMMENT "Patching CONFIG_TINYUSB_CDC_COUNT to 6 for ESP32-P4 (6 CDC ports)"
)
add_custom_target(patch_cdc_count ALL DEPENDS ${CMAKE_BINARY_DIR}/config/.cdc_count_patched)

# Patch DWC2 RX FIFO allocation: use single-buffered (1x) instead of double-buffered (2x).
# ESP32-P4 HS DWC2 has only 4096 bytes of FIFO. With the default 2x RX buffer, 6 CDC ports
# with 512-byte HS bulk endpoints exceed the FIFO budget by 96 words. Single-buffered RX
# is sufficient for CDC serial data — the DMA drains packets fast enough.
set(DCD_DWC2_FILE ${CMAKE_CURRENT_SOURCE_DIR}/managed_components/espressif__tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/config/.dfifo_patched
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/patch_dfifo.py ${DCD_DWC2_FILE}
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/config/.dfifo_patched
    DEPENDS ${DCD_DWC2_FILE}
    COMMENT "Patching DWC2 RX FIFO to single-buffered for 6 CDC HS bulk endpoints"
)
add_custom_target(patch_dfifo ALL DEPENDS ${CMAKE_BINARY_DIR}/config/.dfifo_patched)

# Make all ESP-IDF components depend on both patches
# This ensures sdkconfig.h and dcd_dwc2.c are patched before any compilation
idf_build_get_property(build_components BUILD_COMPONENTS)
foreach(comp ${build_components})
    if(TARGET __idf_${comp})
        add_dependencies(__idf_${comp} patch_cdc_count patch_dfifo)
    endif()
endforeach()

# Auto-build frontend with npm
set(FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)

add_custom_target(frontend_build
    COMMAND ${CMAKE_COMMAND} -E echo "Building frontend..."
    COMMAND npm install --prefix ${FRONTEND_DIR}
    COMMAND npm run build --prefix ${FRONTEND_DIR}
    WORKING_DIRECTORY ${FRONTEND_DIR}
    COMMENT "Building Svelte frontend"
)

# Ensure data/www exists (needed before littlefs_create_partition_image configures)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/www)

# Generate LittleFS partition image from data/www/ directory
# This flashes the web frontend alongside the firmware
littlefs_create_partition_image(storage data/www FLASH_IN_PROJECT)

# Wire frontend build before LittleFS image generation
add_dependencies(littlefs_storage_bin frontend_build)

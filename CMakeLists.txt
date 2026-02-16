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

# Make all ESP-IDF components depend on the patch
# This ensures sdkconfig.h is patched before any compilation
idf_build_get_property(build_components BUILD_COMPONENTS)
foreach(comp ${build_components})
    if(TARGET __idf_${comp})
        add_dependencies(__idf_${comp} patch_cdc_count)
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

# Generate LittleFS partition image from data/www/ directory
# This flashes the web frontend alongside the firmware
littlefs_create_partition_image(storage data/www FLASH_IN_PROJECT)

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_virtual_uart)

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

cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_virtual_uart)

# Generate LittleFS partition image from data/www/ directory
# This flashes the web frontend alongside the firmware
littlefs_create_partition_image(storage data/www FLASH_IN_PROJECT)

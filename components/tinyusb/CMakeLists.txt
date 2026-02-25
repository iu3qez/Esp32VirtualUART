# Local TinyUSB component wrapping the dual-instance fork at /home/sf/src/tinyusb
# This replaces the managed espressif__tinyusb component.
# The esp_tinyusb wrapper auto-detects this local component (see its CMakeLists.txt lines 50-55).

set(TINYUSB_DIR "/home/sf/src/tinyusb")

idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "esp32s3")
    set(tusb_mcu "OPT_MCU_ESP32S3")
elseif(${target} STREQUAL "esp32s2")
    set(tusb_mcu "OPT_MCU_ESP32S2")
elseif(${target} STREQUAL "esp32p4")
    set(tusb_mcu "OPT_MCU_ESP32P4")
elseif(${target} STREQUAL "esp32h4")
    set(tusb_mcu "OPT_MCU_ESP32H4")
endif()

set(compile_options
    "-DCFG_TUSB_MCU=${tusb_mcu}"
    )

idf_component_get_property(freertos_include freertos ORIG_INCLUDE_PATH)

set(includes_private
    "${TINYUSB_DIR}/src/"
    "${TINYUSB_DIR}/src/device"
    "${TINYUSB_DIR}/lib/networking"
    )

set(includes_public
    "${TINYUSB_DIR}/src/"
    "${freertos_include}"
    )

set(srcs
    "${TINYUSB_DIR}/src/class/cdc/cdc_device.c"
    "${TINYUSB_DIR}/src/class/hid/hid_device.c"
    "${TINYUSB_DIR}/src/class/midi/midi_device.c"
    "${TINYUSB_DIR}/src/class/msc/msc_device.c"
    "${TINYUSB_DIR}/src/class/vendor/vendor_device.c"
    "${TINYUSB_DIR}/src/class/audio/audio_device.c"
    "${TINYUSB_DIR}/src/class/video/video_device.c"
    "${TINYUSB_DIR}/src/class/bth/bth_device.c"
    "${TINYUSB_DIR}/src/class/usbtmc/usbtmc_device.c"
    # NET class
    "${TINYUSB_DIR}/src/class/net/ecm_rndis_device.c"
    "${TINYUSB_DIR}/lib/networking/rndis_reports.c"
    "${TINYUSB_DIR}/src/class/net/ncm_device.c"
    # DFU
    "${TINYUSB_DIR}/src/class/dfu/dfu_device.c"
    "${TINYUSB_DIR}/src/class/dfu/dfu_rt_device.c"
    # Common, device-mode related
    "${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c"
    "${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c"
    "${TINYUSB_DIR}/src/common/tusb_fifo.c"
    "${TINYUSB_DIR}/src/device/usbd_control.c"
    "${TINYUSB_DIR}/src/device/usbd.c"
    "${TINYUSB_DIR}/src/tusb.c"
    )

set(requirements_private
    esp_netif
    )

if(${target} STREQUAL "esp32p4")
    list(APPEND requirements_private
                esp_mm
        )
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${includes_public}
                       PRIV_INCLUDE_DIRS ${includes_private}
                       PRIV_REQUIRES  ${requirements_private}
                       )

target_compile_options(${COMPONENT_LIB} PUBLIC ${compile_options})

# Suppress warning when no builtin class driver is enabled
set_source_files_properties("${TINYUSB_DIR}/src/device/usbd.c" PROPERTIES COMPILE_FLAGS "-Wno-type-limits")

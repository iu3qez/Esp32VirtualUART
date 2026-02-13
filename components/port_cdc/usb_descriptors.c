#include "tusb.h"
#include "tinyusb.h"

// ----- USB Descriptor Configuration for Composite 2x CDC-ACM Device -----
//
// ESP32-S3 USB-OTG has 6 device endpoints:
//   5 bidirectional (IN+OUT) + 1 IN-only
//   EP0 reserved for control
//
// Endpoint allocation:
//   CDC0: EP1 IN (notification), EP2 OUT (data), EP2 IN (data)
//   CDC1: EP3 IN (notification), EP4 OUT (data), EP4 IN (data)
//   Total: 4 bidirectional EPs + 2 IN-only (notifications) = fits within limits
//
// Interface allocation:
//   Interface 0: CDC0 Communication (Abstract Control Model)
//   Interface 1: CDC0 Data
//   Interface 2: CDC1 Communication
//   Interface 3: CDC1 Data

#define EPNUM_CDC0_NOTIF    0x81    // EP1 IN
#define EPNUM_CDC0_OUT      0x02    // EP2 OUT
#define EPNUM_CDC0_IN       0x82    // EP2 IN
#define EPNUM_CDC1_NOTIF    0x83    // EP3 IN
#define EPNUM_CDC1_OUT      0x04    // EP4 OUT
#define EPNUM_CDC1_IN       0x84    // EP4 IN

#define ITF_NUM_CDC0        0
#define ITF_NUM_CDC0_DATA   1
#define ITF_NUM_CDC1        2
#define ITF_NUM_CDC1_DATA   3
#define ITF_NUM_TOTAL       4

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

// String descriptor indices
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC0,
    STRID_CDC1,
};

// Device descriptor - Composite device (IAD)
const tusb_desc_device_t cdc_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    // Use Interface Association Descriptor (IAD) for composite device
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1234,
    .idProduct          = 0x5678,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
};

// Configuration descriptor
const uint8_t cdc_config_descriptor[] = {
    // Config descriptor
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // CDC0: Interface 0+1, EP1 notify, EP2 data
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC0, STRID_CDC0,
                       EPNUM_CDC0_NOTIF, 8,
                       EPNUM_CDC0_OUT, EPNUM_CDC0_IN, 64),

    // CDC1: Interface 2+3, EP3 notify, EP4 data
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC1, STRID_CDC1,
                       EPNUM_CDC1_NOTIF, 8,
                       EPNUM_CDC1_OUT, EPNUM_CDC1_IN, 64),
};

// String descriptors
const char *cdc_string_descriptor[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},  // English (US)
    [STRID_MANUFACTURER] = "VirtualUART",
    [STRID_PRODUCT]      = "ESP32 Virtual UART",
    [STRID_SERIAL]       = "000001",
    [STRID_CDC0]         = "Virtual UART Port 0",
    [STRID_CDC1]         = "Virtual UART Port 1",
};

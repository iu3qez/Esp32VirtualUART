#include "tusb.h"
#include "tinyusb.h"

// ----- USB Descriptor Configuration for Composite 3x CDC-ACM Device -----
//
// Standard 2-interface CDC-ACM layout: each CDC port uses a Communication
// interface (with interrupt notification EP) and a Data interface (with bulk
// IN+OUT pair). This is the standard CDC-ACM layout that works out-of-the-box
// on Linux (cdc_acm), Windows (usbser.sys), and macOS (IOUSBFamily).
//
// ESP32-P4 DWC2 HS hardware constraints (from GHWCFG registers):
//   NumInEps  = 7 non-zero (EP1-EP7 IN), 8 total with EP0
//   DfifoDepth = 896 words (3584 bytes)
//   3 CDC ports use 6 IN endpoints (3 notif + 3 bulk) — fits within 7
//
// Endpoint allocation (3 per CDC port):
//   CDC0: Notif EP1 IN, Bulk EP2 OUT + EP2 IN
//   CDC1: Notif EP3 IN, Bulk EP4 OUT + EP4 IN
//   CDC2: Notif EP5 IN, Bulk EP6 OUT + EP6 IN
//   Total: 6 IN + 3 OUT = 9 endpoints (of 16 available)
//
// Interface allocation (2 per CDC = 6 total):
//   Interface 0: CDC0 Comm,  Interface 1: CDC0 Data
//   Interface 2: CDC1 Comm,  Interface 3: CDC1 Data
//   Interface 4: CDC2 Comm,  Interface 5: CDC2 Data

// Notification endpoint addresses (interrupt IN)
#define EPNUM_CDC0_NOTIF 0x81
#define EPNUM_CDC1_NOTIF 0x83
#define EPNUM_CDC2_NOTIF 0x85

// Data endpoint addresses (bulk)
#define EPNUM_CDC0_OUT   0x02
#define EPNUM_CDC0_IN    0x82
#define EPNUM_CDC1_OUT   0x04
#define EPNUM_CDC1_IN    0x84
#define EPNUM_CDC2_OUT   0x06
#define EPNUM_CDC2_IN    0x86

// Interface numbers (2 per CDC, 6 total)
#define ITF_NUM_CDC0     0
#define ITF_NUM_CDC1     2
#define ITF_NUM_CDC2     4
#define ITF_NUM_TOTAL    6

// Notification endpoint max packet size
#define CDC_NOTIF_EP_SIZE  8

// Bulk endpoint max packet sizes per USB speed
#define CDC_BULK_FS_EP_SIZE  64   // FullSpeed max
#define CDC_BULK_HS_EP_SIZE  512  // HighSpeed: must be 512 per USB 2.0 spec

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + 3 * TUD_CDC_DESC_LEN)

// String descriptor indices
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

// Device descriptor - Composite device (IAD)
const tusb_desc_device_t cdc_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1234,
    .idProduct          = 0x5678,
    .bcdDevice          = 0x0300,  // Bump to invalidate host descriptor cache
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
};

// FullSpeed configuration descriptor
const uint8_t cdc_fs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC0, 0, EPNUM_CDC0_NOTIF, CDC_NOTIF_EP_SIZE,
                       EPNUM_CDC0_OUT, EPNUM_CDC0_IN, CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC1, 0, EPNUM_CDC1_NOTIF, CDC_NOTIF_EP_SIZE,
                       EPNUM_CDC1_OUT, EPNUM_CDC1_IN, CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC2, 0, EPNUM_CDC2_NOTIF, CDC_NOTIF_EP_SIZE,
                       EPNUM_CDC2_OUT, EPNUM_CDC2_IN, CDC_BULK_FS_EP_SIZE),
};

// HighSpeed configuration descriptor
const uint8_t cdc_hs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC0, 0, EPNUM_CDC0_NOTIF, CDC_NOTIF_EP_SIZE,
                       EPNUM_CDC0_OUT, EPNUM_CDC0_IN, CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC1, 0, EPNUM_CDC1_NOTIF, CDC_NOTIF_EP_SIZE,
                       EPNUM_CDC1_OUT, EPNUM_CDC1_IN, CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC2, 0, EPNUM_CDC2_NOTIF, CDC_NOTIF_EP_SIZE,
                       EPNUM_CDC2_OUT, EPNUM_CDC2_IN, CDC_BULK_HS_EP_SIZE),
};

// Device qualifier descriptor (required for HS-capable devices)
const tusb_desc_device_qualifier_t cdc_qualifier_descriptor = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 1,
    .bReserved          = 0,
};

// String descriptors
const char *cdc_string_descriptor[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},  // English (US)
    [STRID_MANUFACTURER] = "VirtualUART",
    [STRID_PRODUCT]      = "ESP32-P4 Virtual UART",
    [STRID_SERIAL]       = "000001",
};

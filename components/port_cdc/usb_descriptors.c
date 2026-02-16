#include "tusb.h"
#include "tinyusb.h"

// ----- USB Descriptor Configuration for Composite 6x CDC-ACM Device -----
//
// ESP32-P4 HS USB (DWC2) has 16 endpoints:
//   EP0 reserved for control, 8 IN + 8 OUT available
//
// To fit 6 CDC ports we omit notification (interrupt IN) endpoints.
// Each CDC uses only bulk IN + bulk OUT (2 EPs per port).
//   CDC0: EP1 OUT + EP1 IN
//   CDC1: EP2 OUT + EP2 IN
//   CDC2: EP3 OUT + EP3 IN
//   CDC3: EP4 OUT + EP4 IN
//   CDC4: EP5 OUT + EP5 IN
//   CDC5: EP6 OUT + EP6 IN
//   Total: 6 IN + 6 OUT = 12 endpoints (fits in 16)
//
// Interface allocation (2 per CDC = 12 total):
//   Interface 0+1:   CDC0
//   Interface 2+3:   CDC1
//   Interface 4+5:   CDC2
//   Interface 6+7:   CDC3
//   Interface 8+9:   CDC4
//   Interface 10+11: CDC5

// Endpoint numbers
#define EPNUM_CDC0_OUT  0x01
#define EPNUM_CDC0_IN   0x81
#define EPNUM_CDC1_OUT  0x02
#define EPNUM_CDC1_IN   0x82
#define EPNUM_CDC2_OUT  0x03
#define EPNUM_CDC2_IN   0x83
#define EPNUM_CDC3_OUT  0x04
#define EPNUM_CDC3_IN   0x84
#define EPNUM_CDC4_OUT  0x05
#define EPNUM_CDC4_IN   0x85
#define EPNUM_CDC5_OUT  0x06
#define EPNUM_CDC5_IN   0x86

// Interface numbers
#define ITF_NUM_CDC0        0
#define ITF_NUM_CDC0_DATA   1
#define ITF_NUM_CDC1        2
#define ITF_NUM_CDC1_DATA   3
#define ITF_NUM_CDC2        4
#define ITF_NUM_CDC2_DATA   5
#define ITF_NUM_CDC3        6
#define ITF_NUM_CDC3_DATA   7
#define ITF_NUM_CDC4        8
#define ITF_NUM_CDC4_DATA   9
#define ITF_NUM_CDC5        10
#define ITF_NUM_CDC5_DATA   11
#define ITF_NUM_TOTAL       12

// CDC descriptor without notification endpoint (saves 1 IN EP per port)
// This is a custom macro based on TUD_CDC_DESCRIPTOR but with notif EP = 0
// and notif EP max size = 0, effectively disabling the interrupt endpoint.
//
// TinyUSB's CDC class driver tolerates epn_notif = 0 — it simply won't
// send serial-state notifications to the host. Line coding and DTR/RTS
// still work via control transfers on EP0.
#define TUD_CDC_DESC_NO_NOTIF(_itfnum, _stridx, _ep_out, _ep_in, _epsize) \
    /* CDC Communication Interface */ \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, _stridx, \
    /* CDC Header */ \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, U16_TO_U8S_LE(0x0120), \
    /* CDC Call Management */ \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_CALL_MANAGEMENT, 0, (uint8_t)((_itfnum) + 1), \
    /* CDC ACM */ \
    4, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, 0x02, \
    /* CDC Union */ \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_UNION, _itfnum, (uint8_t)((_itfnum) + 1), \
    /* CDC Data Interface */ \
    9, TUSB_DESC_INTERFACE, (uint8_t)((_itfnum) + 1), 0, 2, TUSB_CLASS_CDC_DATA, 0, 0, 0, \
    /* Data OUT endpoint */ \
    7, TUSB_DESC_ENDPOINT, _ep_out, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0, \
    /* Data IN endpoint */ \
    7, TUSB_DESC_ENDPOINT, _ep_in, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0

// Size of one CDC descriptor without notification EP:
//   Interface(9) + Header(5) + Call Mgmt(5) + ACM(4) + Union(5) + Data Interface(9) + EP OUT(7) + EP IN(7) = 51
#define TUD_CDC_DESC_NO_NOTIF_LEN  51

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + 6 * TUD_CDC_DESC_NO_NOTIF_LEN)

// String descriptor indices
// esp_tinyusb limits to 8 string descriptors (USB_STRING_DESCRIPTOR_ARRAY_SIZE)
// so we only use 4: LANGID, Manufacturer, Product, Serial
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
    // Use Interface Association Descriptor (IAD) for composite device
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1234,
    .idProduct          = 0x5678,
    .bcdDevice          = 0x0200,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
};

// Bulk endpoint max packet sizes per USB speed
#define CDC_BULK_FS_EP_SIZE  64   // FullSpeed max
#define CDC_BULK_HS_EP_SIZE  512  // HighSpeed max

// FullSpeed configuration descriptor (used when device operates at FS)
const uint8_t cdc_fs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC0, 0,
                          EPNUM_CDC0_OUT, EPNUM_CDC0_IN, CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC1, 0,
                          EPNUM_CDC1_OUT, EPNUM_CDC1_IN, CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC2, 0,
                          EPNUM_CDC2_OUT, EPNUM_CDC2_IN, CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC3, 0,
                          EPNUM_CDC3_OUT, EPNUM_CDC3_IN, CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC4, 0,
                          EPNUM_CDC4_OUT, EPNUM_CDC4_IN, CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC5, 0,
                          EPNUM_CDC5_OUT, EPNUM_CDC5_IN, CDC_BULK_FS_EP_SIZE),
};

// HighSpeed configuration descriptor (used when device operates at HS)
const uint8_t cdc_hs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC0, 0,
                          EPNUM_CDC0_OUT, EPNUM_CDC0_IN, CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC1, 0,
                          EPNUM_CDC1_OUT, EPNUM_CDC1_IN, CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC2, 0,
                          EPNUM_CDC2_OUT, EPNUM_CDC2_IN, CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC3, 0,
                          EPNUM_CDC3_OUT, EPNUM_CDC3_IN, CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC4, 0,
                          EPNUM_CDC4_OUT, EPNUM_CDC4_IN, CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESC_NO_NOTIF(ITF_NUM_CDC5, 0,
                          EPNUM_CDC5_OUT, EPNUM_CDC5_IN, CDC_BULK_HS_EP_SIZE),
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

// String descriptors (max 8 entries per esp_tinyusb USB_STRING_DESCRIPTOR_ARRAY_SIZE)
const char *cdc_string_descriptor[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},  // English (US)
    [STRID_MANUFACTURER] = "VirtualUART",
    [STRID_PRODUCT]      = "ESP32-P4 Virtual UART",
    [STRID_SERIAL]       = "000001",
};

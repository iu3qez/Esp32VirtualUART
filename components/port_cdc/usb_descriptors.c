#include "tusb.h"
#include "device/usbd.h"
#include "esp_log.h"

// ----- Dual USB Descriptor Configuration -----
//
// Two separate USB devices on the ESP32-P4:
//   rhport 0 (FS, OTG1.1): 2 CDC ports, 64-byte bulk, PID=0x567A
//   rhport 1 (HS, OTG2.0): 3 CDC ports, 512-byte bulk, PID=0x5678
//
// Each controller appears as a separate USB device to the host.
// The CDC class driver assigns instance numbers globally:
//   CDC 0-1 on FS, CDC 2-4 on HS (init order determines this).

// ===== FS USB (rhport 0): 2 CDC ports =====

// FS endpoint allocation (2 CDC ports, standard 2-interface layout):
//   CDC0: Notif EP1 IN, Bulk EP2 OUT + EP2 IN
//   CDC1: Notif EP3 IN, Bulk EP4 OUT + EP4 IN
#define FS_EPNUM_CDC0_NOTIF 0x81
#define FS_EPNUM_CDC0_OUT   0x02
#define FS_EPNUM_CDC0_IN    0x82
#define FS_EPNUM_CDC1_NOTIF 0x83
#define FS_EPNUM_CDC1_OUT   0x04
#define FS_EPNUM_CDC1_IN    0x84

#define FS_ITF_NUM_CDC0     0
#define FS_ITF_NUM_CDC1     2
#define FS_ITF_NUM_TOTAL    4

#define FS_CDC_NOTIF_EP_SIZE   8
#define FS_CDC_BULK_EP_SIZE    64

#define FS_CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

static const tusb_desc_device_t fs_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1234,
    .idProduct          = 0x567A,  // Different PID from HS device
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

static const uint8_t fs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, FS_ITF_NUM_TOTAL, 0, FS_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    TUD_CDC_DESCRIPTOR(FS_ITF_NUM_CDC0, 0, FS_EPNUM_CDC0_NOTIF, FS_CDC_NOTIF_EP_SIZE,
                       FS_EPNUM_CDC0_OUT, FS_EPNUM_CDC0_IN, FS_CDC_BULK_EP_SIZE),
    TUD_CDC_DESCRIPTOR(FS_ITF_NUM_CDC1, 0, FS_EPNUM_CDC1_NOTIF, FS_CDC_NOTIF_EP_SIZE,
                       FS_EPNUM_CDC1_OUT, FS_EPNUM_CDC1_IN, FS_CDC_BULK_EP_SIZE),
};

// ===== HS USB (rhport 1): 3 CDC ports =====

// HS endpoint allocation (3 CDC ports):
//   CDC2: Notif EP1 IN, Bulk EP2 OUT + EP2 IN
//   CDC3: Notif EP3 IN, Bulk EP4 OUT + EP4 IN
//   CDC4: Notif EP5 IN, Bulk EP6 OUT + EP6 IN
#define HS_EPNUM_CDC0_NOTIF 0x81
#define HS_EPNUM_CDC0_OUT   0x02
#define HS_EPNUM_CDC0_IN    0x82
#define HS_EPNUM_CDC1_NOTIF 0x83
#define HS_EPNUM_CDC1_OUT   0x04
#define HS_EPNUM_CDC1_IN    0x84
#define HS_EPNUM_CDC2_NOTIF 0x85
#define HS_EPNUM_CDC2_OUT   0x06
#define HS_EPNUM_CDC2_IN    0x86

#define HS_ITF_NUM_CDC0     0
#define HS_ITF_NUM_CDC1     2
#define HS_ITF_NUM_CDC2     4
#define HS_ITF_NUM_TOTAL    6

#define HS_CDC_NOTIF_EP_SIZE   8
#define HS_CDC_BULK_FS_EP_SIZE 64
#define HS_CDC_BULK_HS_EP_SIZE 512

#define HS_CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + 3 * TUD_CDC_DESC_LEN)

static const tusb_desc_device_t hs_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1234,
    .idProduct          = 0x5678,  // Existing PID for HS device
    .bcdDevice          = 0x0400,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

// HS device: FullSpeed fallback config descriptor (64-byte bulk)
static const uint8_t hs_fs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, HS_ITF_NUM_TOTAL, 0, HS_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    TUD_CDC_DESCRIPTOR(HS_ITF_NUM_CDC0, 0, HS_EPNUM_CDC0_NOTIF, HS_CDC_NOTIF_EP_SIZE,
                       HS_EPNUM_CDC0_OUT, HS_EPNUM_CDC0_IN, HS_CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(HS_ITF_NUM_CDC1, 0, HS_EPNUM_CDC1_NOTIF, HS_CDC_NOTIF_EP_SIZE,
                       HS_EPNUM_CDC1_OUT, HS_EPNUM_CDC1_IN, HS_CDC_BULK_FS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(HS_ITF_NUM_CDC2, 0, HS_EPNUM_CDC2_NOTIF, HS_CDC_NOTIF_EP_SIZE,
                       HS_EPNUM_CDC2_OUT, HS_EPNUM_CDC2_IN, HS_CDC_BULK_FS_EP_SIZE),
};

// HS device: HighSpeed config descriptor (512-byte bulk)
static const uint8_t hs_hs_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, HS_ITF_NUM_TOTAL, 0, HS_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    TUD_CDC_DESCRIPTOR(HS_ITF_NUM_CDC0, 0, HS_EPNUM_CDC0_NOTIF, HS_CDC_NOTIF_EP_SIZE,
                       HS_EPNUM_CDC0_OUT, HS_EPNUM_CDC0_IN, HS_CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(HS_ITF_NUM_CDC1, 0, HS_EPNUM_CDC1_NOTIF, HS_CDC_NOTIF_EP_SIZE,
                       HS_EPNUM_CDC1_OUT, HS_EPNUM_CDC1_IN, HS_CDC_BULK_HS_EP_SIZE),
    TUD_CDC_DESCRIPTOR(HS_ITF_NUM_CDC2, 0, HS_EPNUM_CDC2_NOTIF, HS_CDC_NOTIF_EP_SIZE,
                       HS_EPNUM_CDC2_OUT, HS_EPNUM_CDC2_IN, HS_CDC_BULK_HS_EP_SIZE),
};

// Device qualifier descriptor (required for HS-capable devices)
static const tusb_desc_device_qualifier_t hs_qualifier_descriptor = {
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

// ===== String Descriptors (shared by both devices) =====

static const char *string_descriptor_fs[] = {
    (const char[]){0x09, 0x04},             // 0: English (US)
    "VirtualUART",                           // 1: Manufacturer
    "ESP32-P4 Virtual UART (FS)",            // 2: Product
    "000001",                                // 3: Serial
};

static const char *string_descriptor_hs[] = {
    (const char[]){0x09, 0x04},             // 0: English (US)
    "VirtualUART",                           // 1: Manufacturer
    "ESP32-P4 Virtual UART (HS)",            // 2: Product
    "000002",                                // 3: Serial
};

#define STRING_DESC_COUNT 4

// ===== TinyUSB Descriptor Callbacks (override esp_tinyusb weak stubs) =====

// Buffer for UTF-16 string conversion
static uint16_t _desc_str_buf[32];

static uint16_t const* _make_string_desc(const char* str) {
    uint8_t chr_count;
    if (str == NULL) return NULL;

    if (str[0] == 0x09 && str[1] == 0x04) {
        // LANGID descriptor
        memcpy(&_desc_str_buf[1], str, 2);
        chr_count = 1;
    } else {
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str_buf[1 + i] = str[i];
        }
    }

    _desc_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str_buf;
}

uint8_t const *tud_descriptor_device_cb(void) {
    uint8_t rhport = tud_get_current_rhport();
    ESP_LOGI("USB_DESC", "tud_descriptor_device_cb called, rhport=%d", rhport);
    if (rhport == 0) {
        return (uint8_t const *)&fs_device_descriptor;
    } else {
        return (uint8_t const *)&hs_device_descriptor;
    }
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    uint8_t rhport = tud_get_current_rhport();
    ESP_LOGI("USB_DESC", "tud_descriptor_configuration_cb called, rhport=%d, index=%d", rhport, index);

    if (rhport == 0) {
        return fs_config_descriptor;
    } else {
        return hs_hs_config_descriptor;
    }
}

uint8_t const *tud_descriptor_device_qualifier_cb(void) {
    uint8_t rhport = tud_get_current_rhport();
    ESP_LOGI("USB_DESC", "tud_descriptor_device_qualifier_cb called, rhport=%d", rhport);
    if (rhport == 0) {
        return NULL;
    }
    return (uint8_t const *)&hs_qualifier_descriptor;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
    (void)index;
    uint8_t rhport = tud_get_current_rhport();
    if (rhport == 0) {
        return NULL;  // FS-only device
    }
    // Return the FS config as "other speed" for the HS device
    return hs_fs_config_descriptor;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t rhport = tud_get_current_rhport();
    ESP_LOGI("USB_DESC", "tud_descriptor_string_cb called, rhport=%d, index=%d", rhport, index);

    const char **descs = (rhport == 0) ? string_descriptor_fs : string_descriptor_hs;
    if (index >= STRING_DESC_COUNT) return NULL;

    return _make_string_desc(descs[index]);
}

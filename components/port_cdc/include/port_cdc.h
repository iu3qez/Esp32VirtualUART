#pragma once

#include "port.h"

// Total CDC-ACM ports on the USB bus (ESP32-P4 HS USB supports up to 6)
#define CDC_PORT_COUNT  6

// Initialize the TinyUSB driver and all CDC ports.
// Call once at startup. Registers all CDC ports in the port registry.
esp_err_t port_cdc_init(void);

// Get the port_t for a CDC port by index (0-5)
port_t *port_cdc_get(int cdc_index);

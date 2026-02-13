#pragma once

#include "port.h"

#define CDC_PORT_COUNT  2

// Initialize the TinyUSB driver and all CDC ports.
// Call once at startup. Registers CDC ports in the port registry.
esp_err_t port_cdc_init(void);

// Get the port_t for a CDC port by index (0 or 1)
port_t *port_cdc_get(int cdc_index);

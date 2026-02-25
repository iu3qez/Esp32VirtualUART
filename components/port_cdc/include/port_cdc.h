#pragma once

#include "port.h"

// Total CDC-ACM ports: 2 on FS USB (rhport 0) + 3 on HS USB (rhport 1)
#define CDC_PORT_COUNT_FS  2   // CDC0, CDC1 on Full-Speed USB
#define CDC_PORT_COUNT_HS  3   // CDC2, CDC3, CDC4 on High-Speed USB
#define CDC_PORT_COUNT     (CDC_PORT_COUNT_FS + CDC_PORT_COUNT_HS)

// Initialize dual USB PHYs, TinyUSB device stack, and all CDC ports.
// Call once at startup. Registers all CDC ports in the port registry.
esp_err_t port_cdc_init(void);

// Get the port_t for a CDC port by index (0-4)
port_t *port_cdc_get(int cdc_index);

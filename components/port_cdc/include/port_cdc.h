#pragma once

#include "port.h"
#include "sdkconfig.h"

// Total CDC-ACM ports on the USB bus
#define CDC_PORT_COUNT  2

// Number of CDC ports available for virtual UART routing
#ifdef CONFIG_VUART_CDC_DEBUG_PORT
#define CDC_PROJECT_PORT_COUNT  (CDC_PORT_COUNT - 1)
#define CDC_DEBUG_INDEX         CONFIG_VUART_CDC_DEBUG_INDEX
#else
#define CDC_PROJECT_PORT_COUNT  CDC_PORT_COUNT
#define CDC_DEBUG_INDEX         (-1)
#endif

// Initialize the TinyUSB driver and all CDC ports.
// Call once at startup. Registers routable CDC ports in the port registry.
// If CONFIG_VUART_CDC_DEBUG_PORT is enabled, one port is dedicated to
// ESP console output and not registered for routing.
esp_err_t port_cdc_init(void);

// Get the port_t for a routable CDC port by index (0 or 1)
// Returns NULL for the debug port index when debug is enabled.
port_t *port_cdc_get(int cdc_index);

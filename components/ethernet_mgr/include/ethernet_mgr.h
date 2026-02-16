#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Initialize Ethernet with IP101 PHY on Guition JC-ESP32P4-M3-Dev board.
// Configures internal EMAC + IP101 PHY and registers as esp_netif interface.
// Must be called after esp_netif_init() and esp_event_loop_create_default().
esp_err_t ethernet_mgr_init(void);

// Check if Ethernet link is up and has an IP address
bool ethernet_mgr_is_connected(void);

// Get the Ethernet IP address as string (empty if not connected)
const char *ethernet_mgr_get_ip(void);

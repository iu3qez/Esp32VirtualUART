#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Start DNS server that redirects all queries to the AP's IP (192.168.4.1).
// Used for captive portal in AP mode.
esp_err_t dns_server_start(void);

// Stop the DNS server
void dns_server_stop(void);

// Check if DNS server is running
bool dns_server_is_running(void);

#pragma once

#include "esp_err.h"

// Start the HTTP + WebSocket server.
// Must be called after WiFi is connected and all ports/routes are initialized.
esp_err_t web_server_start(void);

// Stop the web server
void web_server_stop(void);

// Notify WebSocket clients of a signal change on a port
void web_server_notify_signal_change(uint8_t port_id, uint32_t signals);

// Notify WebSocket clients of data flow stats
void web_server_notify_data_flow(uint8_t route_id, uint32_t bytes_src_to_dst, uint32_t bytes_dst_to_src);

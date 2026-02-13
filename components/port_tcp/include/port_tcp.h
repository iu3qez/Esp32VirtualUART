#pragma once

#include "port.h"

#define TCP_PORT_COUNT  4

typedef struct {
    char     host[64];      // Remote host (client mode) or bind address (server mode)
    uint16_t tcp_port;      // TCP port number
    bool     is_server;     // true = listen, false = connect
} tcp_port_config_t;

// Initialize a TCP port and register in port registry.
// port_id: unique ID (e.g., 4-7 for TCP ports)
esp_err_t port_tcp_init(uint8_t port_id, const tcp_port_config_t *cfg);

// Get a TCP port by index (0-3)
port_t *port_tcp_get(int tcp_index);

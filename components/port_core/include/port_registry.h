#pragma once

#include "port.h"

esp_err_t port_registry_init(void);
esp_err_t port_registry_add(port_t *port);
esp_err_t port_registry_remove(uint8_t port_id);
port_t   *port_registry_get(uint8_t port_id);
port_t   *port_registry_get_by_name(const char *name);
int       port_registry_get_all(port_t **ports, int max_count);
int       port_registry_count(void);

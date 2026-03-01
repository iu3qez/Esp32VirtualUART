# Code Style and Conventions

## C Code (ESP-IDF)

### Naming
- Functions: `snake_case` (e.g., `port_tcp_init`, `port_registry_add`)
- Types: `snake_case_t` suffix (e.g., `port_t`, `port_ops_t`, `tcp_port_config_t`)
- Enums: `UPPER_SNAKE_CASE` values with type prefix (e.g., `PORT_TYPE_CDC`, `PORT_STATE_READY`)
- Defines: `UPPER_SNAKE_CASE` (e.g., `SIGNAL_DTR`, `PORT_BUF_SIZE`)
- Static TAG: `static const char *TAG = "component_name";`

### Headers
- Use `#pragma once` (not `#ifndef` guards)
- Include `"port.h"` for port components
- Public API: `esp_err_t component_init(...)` pattern

### Component Pattern (port_*)
- Static arrays: `port_t ports[COUNT]` + `priv_t priv[COUNT]`
- Counter: `static int port_count = 0`
- Full `port_ops_t` vtable: open, close, read, write, get_signals, set_signals, set_line_coding, get_line_coding
- Init function: setup priv, fill port_t, create rx_buf via `xStreamBufferCreate(PORT_BUF_SIZE, 1)`, call `port_registry_add()`

### CMakeLists.txt
```cmake
idf_component_register(
    SRCS "file.c"
    INCLUDE_DIRS "include"
    REQUIRES component1 freertos log
)
```

### Logging
- Use `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`, `ESP_LOGD` with TAG
- Use `log` component (NOT `esp_log`)

## Frontend (Svelte 5)
- Plain JavaScript (no TypeScript)
- Svelte 5 with runes syntax
- Stores in `frontend/src/stores/`
- Components in `frontend/src/lib/`
- REST API client in `frontend/src/lib/api.js`

## General
- No docstrings or comments unless logic is non-obvious
- No automated tests
- MIT license

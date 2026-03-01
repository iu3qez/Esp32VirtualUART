---
name: new-component
description: Scaffold a new ESP-IDF component following project conventions. Usage - /new-component <name> [--type port] [--deps dep1,dep2]
---

# New ESP-IDF Component Skill

Creates a new component under `components/<name>/` matching the project's established patterns.

## Arguments

- **name** (required): Component name, e.g. `port_i2c`, `audio_mgr`
- **--type port**: If this is a port component (implements `port_ops_t`), scaffold the full port vtable
- **--deps**: Comma-separated external ESP-IDF component dependencies for `idf_component.yml`

## Steps

### 1. Create directory structure

```
components/<name>/
├── CMakeLists.txt
├── include/<name>.h
├── <name>.c
└── idf_component.yml    (only if --deps specified)
```

### 2. Generate CMakeLists.txt

```cmake
idf_component_register(
    SRCS "<name>.c"
    INCLUDE_DIRS "include"
    REQUIRES <dependencies> freertos log
)
```

For port components (`--type port`), always include `port_core` in REQUIRES.

### 3. Generate header file (`include/<name>.h`)

```c
#pragma once

#include "esp_err.h"
```

For port components, also include `"port.h"` and declare:
- `esp_err_t <name>_init(...)`
- `port_t *<name>_get(int index)`

Use `#pragma once` (not `#ifndef` guards) — this is the project convention.

### 4. Generate source file (`<name>.c`)

Include the component header, `"esp_log.h"`, and `"freertos/FreeRTOS.h"`.

Define `static const char *TAG = "<name>";`

For port components (`--type port`), scaffold the full pattern from `port_tcp.c`:
- Private data struct
- Static port array and private array
- All 8 `port_ops_t` callbacks (open, close, read, write, get_signals, set_signals, set_line_coding, get_line_coding)
- Static `port_ops_t` vtable with all callbacks assigned
- Public `_init()` function that: sets up priv data, fills `port_t`, creates `rx_buf` via `xStreamBufferCreate(PORT_BUF_SIZE, 1)`, calls `port_registry_add()`
- Public `_get()` accessor

For non-port components, scaffold:
- `esp_err_t <name>_init(void)` with `ESP_LOGI(TAG, "initialized")`

### 5. Generate idf_component.yml (only if --deps provided)

```yaml
dependencies:
  <dep>: "<version>"
```

### 6. Remind about integration

After creating the files, inform the user they need to:
1. Add `<name>` to `main/CMakeLists.txt` REQUIRES list
2. Add `#include "<name>.h"` and init call in `main/main.c`
3. If it's a new port type, add a value to the `port_type_t` enum in `components/port_core/include/port.h`

## Reference

The canonical port implementation to follow is `components/port_tcp/port_tcp.c`. Key patterns:
- Static arrays for ports and private data (`port_t ports[COUNT]` + `priv_t priv[COUNT]`)
- Port counter tracking (`static int port_count = 0`)
- `port_line_coding_default()` for initial line coding
- `port_get_effective_signals()` for signal reads
- `PORT_BUF_SIZE` for stream buffer creation

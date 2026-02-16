# Session: esp32-virtual-uart
Updated: 2026-02-16T14:38:00Z

## Goal
Hardware testing, stabilization, and integration validation of the ESP32-S3 Virtual UART device. Phases 1-7 implemented and ready for hardware validation.

## Constraints
- **Tech Stack**: ESP-IDF v5.5.2, C firmware (NOT Arduino), TinyUSB for USB CDC-ACM
- **Hardware**: ESP32-S3 only (uses USB-OTG peripheral), DevKitC with RGB LED on GPIO48
- **Framework**: ESP-IDF component-based architecture with managed dependencies via `idf_component.yml`
- **USB Limitation**: Max 2 CDC-ACM ports (6 USB endpoints, 3 per CDC device)
- **Filesystem**: LittleFS (SPIFFS deprecated) for web frontend hosting at `/data/www/`
- **Build Pattern**: MUST source ESP-IDF environment before all `idf.py` commands:
  ```bash
  . /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py <command>
  ```
- **Logging**: Use `log` not `esp_log` in CMakeLists.txt REQUIRES
- **Partition Table**: Custom `partitions.csv` — factory app 1.75MB (0x1C0000), storage 192KB (0x30000, LittleFS)
- **Status LED**: WS2812 addressable RGB on GPIO48 via `led_strip` component
- **Frontend Build**: Svelte SPA in `frontend/`, built output goes to `data/www/`, embedded via `littlefs_create_partition_image()`. CMake `frontend_build` target auto-runs `npm install` + `npm run build`.

## Key Decisions
1. **TinyUSB over ESP-IDF USB**: TinyUSB provides composite CDC device support (2 ports)
2. **Port Abstraction Layer**: `port_core` with ops vtable pattern for CDC/UART/TCP unification
3. **Routing Engine**: FreeRTOS tasks per forwarding direction, supports bridge/clone/merge modes
4. **Signal Routing**: Separate signal_router task handles DTR/RTS/CTS/DSR/DCD/RI forwarding
5. **WiFi AP Fallback**: If STA credentials fail after retries → auto-fallback to AP mode "VirtualUART"
6. **Captive Portal**: DNS server redirects all DNS queries to 192.168.4.1 in AP mode
7. **WebSocket Real-time Updates**: `/ws/signals` for signal changes, `/ws/monitor` for data flow stats
8. **Config Persistence**: NVS flash stores WiFi creds, routes, line coding, UART pin mappings
9. **No OTA Yet**: OTA firmware update deferred to future (not in current scope)

## State
- Now: [→] Brownfield analysis complete, all phases implemented, awaiting hardware testing
- Next: Flash to ESP32-S3 DevKitC, validate USB CDC enumeration, test routing scenarios

## Working Set
- **Key Files**:
  - `main/main.c` - Application entry point, initialization sequence
  - `components/port_core/` - Port abstraction (`port.h`, `port_registry.c`)
  - `components/port_cdc/` - TinyUSB CDC-ACM (2 virtual COM ports, IDs 0-1)
  - `components/port_uart/` - Hardware UART ports (IDs 2-3)
  - `components/port_tcp/` - TCP socket ports (IDs 4-7, server or client)
  - `components/routing/` - Route engine (`route_engine.c`) + signal router (`signal_router.c`)
  - `components/config_store/` - NVS persistence for routes and system config
  - `components/wifi_mgr/` - WiFi STA with AP fallback, mode change callbacks
  - `components/web_server/` - HTTP server with REST API, WebSocket, LittleFS static files
  - `components/dns_server/` - Captive portal DNS redirector (AP mode only)
  - `components/status_led/` - RGB LED state machine (9 states: booting, ready, data flow, etc.)
  - `frontend/` - Svelte visual node editor (drag-and-drop routing GUI)

- **Build Commands**:
  ```bash
  # Set target (first time only)
  . /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py set-target esp32s3

  # Build firmware
  . /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py build

  # Build frontend (auto-build target, or manual)
  . /home/sf/esp/esp-idf/export.sh 2>/dev/null && cmake --build build --target frontend_build
  # Or manually: cd frontend && npm run build && cd ..

  # Flash and monitor
  . /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 flash monitor

  # Just monitor
  . /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 monitor
  ```

- **Test Scenarios** (hardware required):
  1. USB CDC enumeration - 2 virtual COM ports appear in Windows/Linux/macOS
  2. Bridge mode - Data flows bidirectionally between CDC0 ↔ UART1
  3. Clone mode - CDC0 → [UART1, TCP0] (one-to-many)
  4. Baud rate conversion - Bridge CDC0@9600 ↔ UART1@115200
  5. Signal routing - DTR on CDC0 → RTS on UART1
  6. WiFi AP mode - Connect to "VirtualUART", captive portal redirects to 192.168.4.1
  7. WiFi STA mode - Connect to home network, access web GUI at assigned IP
  8. Web GUI - Drag ports to create routes, configure line coding, view real-time data flow
  9. Config persistence - Reboot, routes and WiFi creds restored from NVS
  10. TCP server mode - Port_TCP listens on port 8080, forwards to CDC0

## Open Questions
- RESOLVED: Frontend build output (`data/www/`) now auto-built via CMake `frontend_build` target
- UNCONFIRMED: Actual hardware pin assignments for UART1/UART2 (defaults in config_store may need adjustment for specific board)
- UNCONFIRMED: TinyUSB CDC-ACM descriptor strings (VID=0x1234, PID=0x5678 are placeholders - may need official VID/PID)
- UNCONFIRMED: LittleFS partition flashing works correctly (untested in hardware)
- UNCONFIRMED: WebSocket connections remain stable under heavy data flow
- UNCONFIRMED: Signal line routing timing (edge cases with rapid DTR/RTS toggling)

## Codebase Summary

**Architecture**: ESP-IDF component-based C firmware with TinyUSB for USB CDC-ACM and a Svelte web GUI. Main loop initializes all subsystems, then monitors port states and updates RGB LED accordingly.

**Port Abstraction**: 8-port system (2 CDC, 2 UART, 4 TCP) unified behind `port_t` struct with ops vtable (`open`, `close`, `read`, `write`, `get_signals`, `set_signals`, `get/set_line_coding`). Each port type implements these ops:
- **port_cdc**: TinyUSB CDC-ACM callbacks push RX data to FreeRTOS stream buffer, ops read from buffer
- **port_uart**: ESP-IDF UART driver with signal line GPIO control
- **port_tcp**: lwIP socket-based, server or client mode, reconnection logic

**Routing Engine**: Creates FreeRTOS tasks to forward data between ports. Route types:
- **Bridge** (1:1 bidirectional): 2 tasks (src→dst, dst→src)
- **Clone** (1:N unidirectional): 1 task (src → all dests)
- **Merge** (N:1 unidirectional): N tasks (each src → dest)

**Signal Routing**: Separate task polls signal lines (DTR/RTS/CTS/DSR) and applies signal mappings defined in routes. Supports override (force a signal high/low regardless of hardware).

**Config Persistence**: `system_config_t` struct serialized to NVS. Includes:
- WiFi SSID/password
- Per-port line coding (baud, data bits, parity, stop bits)
- TCP port configs (host, port, server/client)
- UART pin assignments (TX, RX, RTS, CTS, DTR, DSR, DCD, RI)
- Route definitions (type, src, dests, signal mappings)

**WiFi Manager**: Boots in STA mode if credentials exist, falls back to AP mode ("VirtualUART" open network at 192.168.4.1) after 5 retries. Mode change callback triggers web server restart and DNS server start/stop.

**Web Server**: HTTP server with:
- **REST API**: `/api/ports`, `/api/routes`, `/api/config`, `/api/system`
- **WebSocket**: `/ws/signals` (real-time signal changes), `/ws/monitor` (data flow stats)
- **Static Files**: Serves Svelte SPA from LittleFS partition
- **Fallback HTML**: Minimal inline HTML if LittleFS not available
- **Captive Portal**: Redirects known probe URLs (generate_204, connecttest, etc.) to root in AP mode

**Frontend (Svelte)**: Visual node editor with drag-and-drop routing. Features:
- **PortNode.svelte**: Draggable port cards (CDC/UART/TCP) showing state, signals, line coding
- **Wire.svelte**: SVG lines connecting source to destination ports
- **ConfigPanel.svelte**: WiFi config, line coding editor, signal mapping editor
- **StatusBar.svelte**: System info (IP, mode, port count, active routes)
- **Stores**: Reactive stores for ports, routes, signals (updated via WebSocket)
- **api.js**: REST API client + WebSocket reconnection logic

**Status LED**: 9 states with distinct colors/animations:
- Booting (blue), No USB (pulse purple), Ready (pulse green), Idle (solid green)
- Data Flow (blink green), WiFi Connecting (pulse orange), WiFi Ready (cyan)
- Data Flow Net (blink white), Error (red)

**Commit History**:
- `d435a4b` Fix AP mode (2026-02-16)
- `e5de14d` Phase 7: Svelte frontend (2026-02-14)
- `672917a` Phase 6: Web server + REST + WebSocket (2026-02-14)
- `e8192ae` Phase 5: WiFi manager + TCP ports (2026-02-14)
- `9e4956f` Phases 2-4: UART, routing, config (2026-02-13)
- `e4056ce` Phase 1: Scaffold, USB CDC, port abstraction, LED (2026-02-13)

**Code Statistics**:
- 15 C source files, 12 header files across 11 components
- Main.c: 232 lines (initialization + state monitoring loop)
- Web server: 362 lines (static file serving, captive portal, API + WS integration)
- Route engine: ~300 lines (forwarding tasks, route lifecycle)
- Port implementations: ~150-200 lines each (CDC, UART, TCP)

**Next Steps for Hardware Testing**:
1. Build frontend: `cmake --build build --target frontend_build` (or `cd frontend && npm run build`)
2. Flash firmware + LittleFS: `. ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyUSB0 flash`
3. Monitor boot: `. ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyUSB0 monitor`
4. Verify USB CDC: Check Device Manager/lsusb for 2 COM ports
5. Test web GUI: Connect to "VirtualUART" AP → 192.168.4.1
6. Create test route: Bridge CDC0 ↔ UART1 via web GUI
7. Validate data flow: Send data through CDC0, verify on UART1 oscilloscope/analyzer
8. Test signal routing: Toggle DTR on CDC0, measure mapped signal on UART1 pins
9. Persistence test: Reboot, verify routes and WiFi creds restored
10. TCP test: Configure TCP server port, connect via netcat, bridge to CDC0

**Known Patterns**:
- All error handling uses `ESP_LOG*` macros with component TAGs
- FreeRTOS primitives: tasks, semaphores (mutexes), stream buffers
- Component init returns `esp_err_t`, logs error and returns on failure
- Port registry uses mutex for thread-safe access to port array
- Route engine uses mutex for thread-safe route array access
- WebSocket clients stored in array, broadcast notifications iterate all connected clients
- Frontend uses Vite dev server for local dev, `npm run build` generates production bundle

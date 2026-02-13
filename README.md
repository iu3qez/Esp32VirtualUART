# ESP32 Virtual UART

A hardware replacement for Windows Virtual Serial Port Emulator (VSPE). An ESP32-S3 USB device that presents multiple virtual COM ports (CDC-ACM) to a host PC, with a web-based visual node editor for routing data and signals between USB ports, physical UARTs, and TCP sockets.

**No drivers needed** — CDC-ACM is natively supported on Windows, macOS, and Linux.

## Features

- **2x USB Virtual COM Ports** — CDC-ACM composite device, appears as real COM ports
- **2x Hardware UARTs** — Connect physical serial devices
- **Routing Engine** — Bridge, clone, or merge any combination of ports
- **Baud Rate Conversion** — Bridge ports running at different speeds
- **TCP Streaming** — Each port can be a TCP server or client over WiFi
- **Signal Line Routing** — DTR, RTS, CTS, DSR — route, simulate, or override
- **Visual Node Editor** — Svelte web GUI with drag-and-drop routing configuration
- **Persistent Config** — Save/restore routing profiles across reboots
- **RGB LED Status** — Visual indication of device state and data activity

## Hardware

- ESP32-S3-DevKitC (or any ESP32-S3 board with USB-OTG)
- Optional: external UART transceiver for second UART port

## Building

Requires [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/).

```bash
# Source ESP-IDF environment
. ~/esp/esp-idf/export.sh

# Set target (first time only)
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Architecture

```
[Windows PC] ──USB──> [ESP32-S3] ──UART1──> [Device A]
                          │       ──UART2──> [Device B]
                          │
                          └──WiFi──> [Web GUI / TCP ports]
```

### Components

| Component | Description |
|-----------|-------------|
| `port_core` | Port abstraction layer — common interface for all port types |
| `port_cdc` | USB CDC-ACM implementation via TinyUSB |
| `port_uart` | Hardware UART port driver |
| `port_tcp` | TCP socket port (server/client) |
| `routing` | Route engine — bridge, clone, merge with signal routing |
| `config_store` | NVS flash persistence |
| `wifi_mgr` | WiFi STA management |
| `web_server` | HTTP server, REST API, WebSocket |
| `status_led` | RGB LED status indicator |
| `frontend/` | Svelte visual node editor SPA |

## Status

Work in progress. Phase 1 (USB CDC + port abstraction) compiles and is ready for hardware testing.

## License

MIT

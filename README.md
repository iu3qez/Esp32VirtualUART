# ESP32 Virtual UART

A hardware replacement for Windows Virtual Serial Port Emulator (VSPE). An ESP32-P4 USB device that presents 6 virtual COM ports (CDC-ACM) over High-Speed USB to a host PC, with a web-based visual node editor for routing data and signals between USB ports, physical UARTs, and TCP sockets.

**No drivers needed** — CDC-ACM is natively supported on Windows, macOS, and Linux.

## Features

- **6x USB Virtual COM Ports** — CDC-ACM composite device over HS USB, appears as real COM ports
- **Hardware UARTs** — Connect physical serial devices
- **Routing Engine** — Bridge, clone, or merge any combination of ports
- **Baud Rate Conversion** — Bridge ports running at different speeds
- **TCP Streaming** — Each port can be a TCP server or client
- **Signal Line Routing** — DTR, RTS, CTS, DSR — route, simulate, or override
- **Visual Node Editor** — Svelte web GUI with drag-and-drop routing configuration
- **Persistent Config** — Save/restore routing profiles across reboots
- **RGB LED Status** — Visual indication of device state and data activity
- **Dual Networking** — Ethernet (IP101) + WiFi (via ESP32-C6 companion)

## Hardware

- **Board:** Guition JC-ESP32P4-M3-Dev
- **MCU:** ESP32-P4 (dual-core RISC-V, 360 MHz, HS USB)
- **WiFi:** ESP32-C6 companion chip via ESP-Hosted (SDIO)
- **Ethernet:** Internal EMAC + IP101 PHY

## Building

Requires [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/get-started/).

```bash
# Source ESP-IDF environment
. ~/esp/esp-idf/export.sh

# Set target (first time only)
idf.py set-target esp32p4

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Architecture

```
[Host PC] ──HS USB──> [ESP32-P4] ──UARTs──> [Serial Devices]
                           │
                           ├──Ethernet (IP101)──> [LAN]
                           │
                           └──SDIO──> [ESP32-C6] ──WiFi──> [WLAN]
                                          │
                                     [Web GUI / TCP ports]
```

### Components

| Component | Description |
|-----------|-------------|
| `port_core` | Port abstraction layer — common interface for all port types |
| `port_cdc` | USB CDC-ACM implementation via TinyUSB (6 ports, HS USB) |
| `port_uart` | Hardware UART port driver |
| `port_tcp` | TCP socket port (server/client) |
| `routing` | Route engine — bridge, clone, merge with signal routing |
| `config_store` | NVS flash persistence |
| `wifi_mgr` | WiFi STA via ESP32-C6 companion (ESP-Hosted, SDIO) |
| `ethernet_mgr` | Ethernet (internal EMAC + IP101 PHY) |
| `dns_server` | Captive portal DNS redirector (AP mode) |
| `web_server` | HTTP server, REST API, WebSocket |
| `status_led` | RGB LED status indicator |
| `frontend/` | Svelte visual node editor SPA |

## Status

Work in progress. USB CDC (6 ports, HS), WiFi (ESP-Hosted), and Ethernet build successfully. Hardware testing in progress.

## License

MIT

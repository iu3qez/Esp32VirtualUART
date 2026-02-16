# ESP32 Virtual UART - Claude Code Instructions

## Project Overview

Hardware VSPE (Virtual Serial Port Emulator) replacement using ESP32-P4. Presents 6 USB CDC-ACM virtual COM ports (HS USB) to a host PC, with a Svelte web GUI for routing data between USB, UART, and TCP ports. Target board: Guition JC-ESP32P4-M3-Dev with ESP32-C6 companion (WiFi via ESP-Hosted/SDIO) and IP101 Ethernet.

## ESP-IDF Setup

ESP-IDF v5.5.2 is installed at `/home/sf/esp/esp-idf`.

**IMPORTANT:** Every Bash command that uses `idf.py` must source the environment first:

```bash
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py <command>
```

### Common Commands

```bash
# Build
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py build

# Flash (adjust port as needed)
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 monitor

# Flash and monitor
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 flash monitor

# Clean build
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py fullclean

# Set target (only needed once or after fullclean)
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py set-target esp32p4

# Menuconfig
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py menuconfig
```

## Project Structure

```
Esp32VirtualUART/
├── CMakeLists.txt              # Root ESP-IDF project
├── sdkconfig.defaults          # Default Kconfig (target, TinyUSB, partitions)
├── partitions.csv              # Custom partition table
├── main/                       # Entry point
├── components/
│   ├── port_core/              # Port abstraction layer (port_t, registry)
│   ├── port_cdc/               # USB CDC-ACM port implementation (TinyUSB)
│   ├── port_uart/              # Hardware UART port (Phase 2)
│   ├── port_tcp/               # TCP socket port (Phase 5)
│   ├── routing/                # Route engine + signal router (Phase 3)
│   ├── config_store/           # NVS persistence (Phase 4)
│   ├── wifi_mgr/               # WiFi STA via ESP32-C6 companion (ESP-Hosted)
│   ├── ethernet_mgr/           # Ethernet (IP101 PHY, internal EMAC)
│   ├── dns_server/             # Captive portal DNS redirector (AP mode)
│   ├── status_led/             # RGB LED status indicator
│   └── web_server/             # HTTP + WebSocket + REST API (Phase 6)
├── frontend/                   # Svelte SPA (Phase 7)
└── data/www/                   # Built frontend output for LittleFS
```

## Key Technical Constraints

- **ESP32-P4 HS USB:** 6 CDC-ACM ports (no notification EPs, bulk IN+OUT only, 12 of 16 EPs used)
- **Target:** ESP32-P4 (dual-core RISC-V, 360MHz, HS USB)
- **Board:** Guition JC-ESP32P4-M3-Dev (C6 companion on SDIO, IP101 Ethernet)
- **Firmware framework:** ESP-IDF v5.x (C), NOT Arduino
- **USB stack:** TinyUSB via `espressif/esp_tinyusb` component (HS port)
- **WiFi:** Via ESP32-C6 companion using ESP-Hosted (SDIO transport)
- **Ethernet:** Internal EMAC + IP101 PHY (MDC=31, MDIO=52, power=51, CLK=50)
- **Filesystem:** LittleFS via `joltwallet/littlefs: "^1.14.0"` (SPIFFS deprecated)
- **RGB LED:** Configurable GPIO via Kconfig (`CONFIG_VUART_STATUS_LED_GPIO`)

## ESP-IDF Component Dependencies

Components use `idf_component.yml` for managed dependencies:
- `port_cdc/idf_component.yml` → `espressif/esp_tinyusb: "^1.7.0"`
- `wifi_mgr/idf_component.yml` → `espressif/esp_wifi_remote`, `espressif/esp_hosted: "~2.6.0"`
- `status_led/idf_component.yml` → `espressif/led_strip: "^2.5.0"`
- `web_server/idf_component.yml` → `joltwallet/littlefs: "^1.14.0"`

## Frontend Build

The Svelte frontend is in `frontend/` and builds to `data/www/` (Vite output).
A CMake custom target `frontend_build` auto-runs npm install + build:

```bash
# Via CMake (after idf.py build has created the build dir)
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && cmake --build build --target frontend_build

# Or manually
cd frontend && npm install && npm run build
```

In CMakeLists.txt, use `log` not `esp_log` for the logging component.

## Build Artifacts

- Plan: `thoughts/shared/plans/PLAN-esp32-virtual-uart.md`
- Spec: `thoughts/shared/specs/2026-02-13-esp32-virtual-uart-spec.md`
- Validation: `thoughts/shared/handoffs/build-20260213-esp32-virtual-uart/validation-report.md`
- Continuity ledger: `thoughts/ledgers/CONTINUITY_CLAUDE-esp32-virtual-uart.md`

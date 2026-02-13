# ESP32 Virtual UART - Claude Code Instructions

## Project Overview

Hardware VSPE (Virtual Serial Port Emulator) replacement using ESP32-S3. Presents multiple USB CDC-ACM virtual COM ports to a host PC, with a Svelte web GUI for routing data between USB, UART, and TCP ports.

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
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py set-target esp32s3

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
│   ├── wifi_mgr/               # WiFi STA management (Phase 5)
│   ├── status_led/             # RGB LED status indicator
│   └── web_server/             # HTTP + WebSocket + REST API (Phase 6)
├── frontend/                   # Svelte SPA (Phase 7)
└── data/www/                   # Built frontend output for LittleFS
```

## Key Technical Constraints

- **ESP32-S3 USB endpoint limit:** Max 2 CDC-ACM ports (6 endpoints total, 3 per CDC)
- **Target:** ESP32-S3 only (uses USB-OTG peripheral)
- **Firmware framework:** ESP-IDF v5.x (C), NOT Arduino
- **USB stack:** TinyUSB via `espressif/esp_tinyusb` component
- **Filesystem:** LittleFS via `espressif/led_strip` component (SPIFFS deprecated)
- **RGB LED:** Addressable WS2812 on GPIO48 (DevKitC) via `espressif/led_strip`

## ESP-IDF Component Dependencies

Components use `idf_component.yml` for managed dependencies:
- `port_cdc/idf_component.yml` → `espressif/esp_tinyusb: "^1.7.0"`
- `status_led/idf_component.yml` → `espressif/led_strip: "^2.5.0"`

In CMakeLists.txt, use `log` not `esp_log` for the logging component.

## Build Artifacts

- Plan: `thoughts/shared/plans/PLAN-esp32-virtual-uart.md`
- Spec: `thoughts/shared/specs/2026-02-13-esp32-virtual-uart-spec.md`
- Validation: `thoughts/shared/handoffs/build-20260213-esp32-virtual-uart/validation-report.md`

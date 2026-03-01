# ESP32 Virtual UART

Hardware VSPE replacement using ESP32-P4. 5 USB CDC-ACM virtual COM ports (2 FS + 3 HS) to host PC, Svelte web GUI for routing between USB, UART, and TCP. Board: Guition JC-ESP32P4-M3-Dev (C6 companion for WiFi, IP101 Ethernet).

## Critical Rules

- **ESP-IDF prefix:** Every `idf.py` command must start with `. /home/sf/esp/esp-idf/export.sh 2>/dev/null &&`
- **Never edit `managed_components/`** — patched at build time by `tools/` scripts. Edit the patch scripts instead.
- **sdkconfig.defaults changes require:** `idf.py fullclean && idf.py set-target esp32p4 && idf.py build`
- **CMakeLists.txt:** use `log` not `esp_log` for the logging component

## Project Structure

```
├── CMakeLists.txt              # Root project + build patches (CDC count, tusb_config, dfifo)
├── sdkconfig.defaults          # Kconfig defaults (target, TinyUSB, USB-SERIAL-JTAG disable)
├── tools/                      # Build-time patch scripts
├── main/                       # Entry point
├── components/
│   ├── port_core/              # Port abstraction (port_t, registry)
│   ├── port_cdc/               # USB CDC-ACM (TinyUSB, dual USB init, PHY mux)
│   ├── port_uart/              # Hardware UART
│   ├── port_tcp/               # TCP socket
│   ├── routing/                # Route engine + signal router
│   ├── config_store/           # NVS persistence
│   ├── wifi_mgr/               # WiFi via ESP32-C6 (ESP-Hosted/SDIO)
│   ├── ethernet_mgr/           # IP101 PHY (MDC=31, MDIO=52, power=51, CLK=50)
│   ├── dns_server/             # Captive portal DNS
│   ├── status_led/             # RGB LED
│   └── web_server/             # HTTP + WebSocket + REST API
├── frontend/                   # Svelte SPA (Vite → data/www/)
└── data/www/                   # Built frontend for LittleFS
```

## Technical Constraints

- **Target:** ESP32-P4 (dual-core RISC-V, 360MHz), ESP-IDF v5.5.2 (C, NOT Arduino)
- **USB:** 5 CDC ports — 2 on FS OTG (rhport 0) + 3 on HS OTG (rhport 1), bulk IN+OUT only
- **USB PHY mux:** ESP32-P4 has 2 FS PHYs. `port_cdc.c` swaps via `LP_SYS.usb_ctrl` so OTG FS uses PHY 0
- **TinyUSB fork:** `/home/sf/src/tinyusb` (dual-instance device stack), NOT managed by ESP-IDF component manager
- **Build patches** (CMakeLists.txt): CDC_COUNT 2→5, dual RHPORT config, single-buffered HS FIFO
- **Linker:** port_cdc uses `--whole-archive` to override esp_tinyusb's strong `tud_descriptor_*_cb` symbols

## Frontend

```bash
cd frontend && npm install && npm run build   # or: cmake --build build --target frontend_build
```

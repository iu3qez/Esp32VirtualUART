# Project Structure

```
Esp32VirtualUART/
├── CMakeLists.txt              # Root: patches TinyUSB CDC count, DWC2 FIFO, frontend build target
├── sdkconfig.defaults          # Kconfig defaults (target, TinyUSB, partitions, ESP-Hosted)
├── sdkconfig                   # AUTO-GENERATED — do not edit directly
├── partitions.csv              # Custom partition table (NVS, PHY, factory 2.5MB, storage 2MB)
├── dependencies.lock           # ESP-IDF component lock
├── tools/patch_dfifo.py        # DWC2 RX FIFO patching script
│
├── main/
│   ├── CMakeLists.txt          # REQUIRES all components
│   ├── Kconfig.projbuild       # Project-level Kconfig
│   └── main.c                  # app_main: init sequence for all subsystems
│
├── components/                 # 13 custom ESP-IDF components
│   ├── port_core/              # Port abstraction (port_t, port_ops_t, registry)
│   ├── port_cdc/               # USB CDC-ACM (TinyUSB, usb_descriptors.c)
│   ├── port_uart/              # Hardware UART driver
│   ├── port_tcp/               # TCP socket port (server/client)
│   ├── routing/                # Route engine + signal router
│   ├── config_store/           # NVS persistence
│   ├── wifi_mgr/               # WiFi STA via ESP32-C6 (ESP-Hosted)
│   ├── ethernet_mgr/           # IP101 Ethernet
│   ├── dns_server/             # Captive portal DNS
│   ├── status_led/             # RGB LED indicator
│   ├── web_server/             # HTTP + WebSocket + REST API + LittleFS
│   └── tinyusb/                # Local wrapper pointing to fork
│
├── frontend/                   # Svelte 5 SPA
│   ├── src/
│   │   ├── App.svelte
│   │   ├── lib/                # NodeEditor, PortNode, Wire, ConfigPanel, StatusBar
│   │   └── stores/             # ports.js, routes.js, signals.js
│   └── package.json
│
├── data/www/                   # Built frontend output (LittleFS partition)
├── managed_components/         # Auto-downloaded ESP-IDF components (DO NOT EDIT)
└── thoughts/                   # Plans, specs, handoffs, continuity ledger
```

## Component Dependencies (idf_component.yml)
- port_cdc → espressif/esp_tinyusb ^1.7.0
- status_led → espressif/led_strip ^2.5.0
- web_server → joltwallet/littlefs ^1.14.0
- wifi_mgr → espressif/esp_wifi_remote, espressif/esp_hosted ~2

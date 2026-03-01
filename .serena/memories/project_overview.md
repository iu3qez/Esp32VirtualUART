# ESP32 Virtual UART - Project Overview

Hardware replacement for Windows VSPE (Virtual Serial Port Emulator) using ESP32-P4.

## Purpose
Presents 6 USB CDC-ACM virtual COM ports to a host PC, with a Svelte web GUI for routing data between USB, UART, and TCP ports. No drivers needed — CDC-ACM is natively supported on all OSes.

## Target Hardware
- **Board:** Guition JC-ESP32P4-M3-Dev
- **MCU:** ESP32-P4 (dual-core RISC-V, 360MHz, HS USB)
- **Companion:** ESP32-C6 (WiFi via ESP-Hosted/SDIO)
- **Ethernet:** IP101 PHY (internal EMAC, MDC=31, MDIO=52, power=51, CLK=50)
- **USB:** 6 CDC-ACM ports total (2 FS + 3 HS, patched TinyUSB)

## Tech Stack
- **Firmware:** ESP-IDF v5.5.2 (C), NOT Arduino
- **USB stack:** TinyUSB via `espressif/esp_tinyusb` (patched for 5+ CDC)
- **Frontend:** Svelte 5 + Vite 7 (SPA, built to LittleFS partition)
- **Filesystem:** LittleFS (`joltwallet/littlefs`)
- **RGB LED:** WS2812 via `espressif/led_strip`

## License
MIT

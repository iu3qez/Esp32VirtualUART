# ESP32 Virtual UART - Roadmap

## Vision
**End goal:** 6 USB-CDC virtual ports dedicated to virtual UART routing on ESP32-P4.
Migrated from ESP32-S3 (2 CDC ports, FS USB) to ESP32-P4 (6 CDC ports, HS USB).
Target board: Guition JC-ESP32P4-M3-Dev with ESP32-C6 companion (WiFi via ESP-Hosted) and IP101 Ethernet.

## Current Focus

_No current goal. Next planned item will be promoted on next planning session._

## Completed
- [x] Switch to 3 standard CDC-ACM ports with notification endpoints (2026-02-17) `cdf658e`
- [x] WIP: Fix 6-port CDC-ACM USB enumeration (2026-02-16) `82c6415`
- [x] Revert esp_hosted version pin to ~2 for broader compatibility (2026-02-16) `ef4789d`
- [x] Update docs: remove esp-extconn reference, log frontend build wiring (2026-02-16) `c930f75`
- [x] Replace esp-extconn with direct esp_hosted, pin ~2.6.0 for C6 co-proc compat (2026-02-16) `92e13ba`
- [x] Add FS/HS config descriptors and device qualifier for P4 HS USB (2026-02-16) `2f1cf4f`
- [x] Wire frontend build as dependency of LittleFS image generation (2026-02-16) `2c3c89f`
- [x] ESP32-P4 migration — build verification and hardware testing (2026-02-16)
- [x] Migrate from ESP32-S3 to ESP32-P4: 6 CDC ports, HS USB, ESP-Hosted WiFi, IP101 Ethernet (2026-02-16)
- [x] OTA firmware update support (2026-02-16)
- [x] Phase 8: USB CDC-ACM composite device with all available virtual COM ports (2026-02-16)
- [x] Frontend UX polish and mobile responsive layout (2026-02-16)
- [x] Add CDC debug console as build feature (CONFIG_VUART_CDC_DEBUG_PORT) (2026-02-16) `3928c01`
- [x] TCP client/server stress testing (2026-02-16)
- [x] Baud rate conversion validation (2026-02-16)
- [x] Signal line routing (DTR, RTS, CTS, DSR) end-to-end testing (2026-02-16)
- [x] Hardware testing on ESP32-S3-DevKitC (2026-02-16)
- [x] CDC debug console as build feature — CDC0 default debug, toggleable via menuconfig (2026-02-16)
- [x] Phase 1-7 implemented, testing and stabilization (2026-02-16)
- [x] Optimize sdkconfig for ESP32-S3 performance (240MHz, 16MB flash, 8MB PSRAM) (2026-02-16) `53239f5`
- [x] Add WiFi STA settings UI and fix config save race condition (2026-02-16) `9c99b79`
- [x] Add frontend build target, fix docs, add project onboarding files (2026-02-16) `ebb33c0`
- [x] Phase 1: USB CDC-ACM composite device with 2 virtual COM ports (2026-02-13)
- [x] Phase 2: Hardware UART port driver (2026-02-13)
- [x] Phase 3: Routing engine with bridge/clone/merge (2026-02-13)
- [x] Phase 4: NVS config persistence (2026-02-13)
- [x] Phase 5: WiFi STA management with AP fallback + TCP ports (2026-02-14)
- [x] Phase 6: Web server with REST API and WebSocket (2026-02-14)
- [x] Phase 7: Svelte visual node editor frontend (2026-02-14)
- [x] AP mode fix (2026-02-16)

## Planned

## Recent Planning Sessions
_Planning sessions will be recorded here automatically._

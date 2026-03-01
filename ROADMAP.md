# ESP32 Virtual UART - Roadmap

## Vision
**End goal:** 6 USB-CDC virtual ports dedicated to virtual UART routing on ESP32-P4.
Migrated from ESP32-S3 (2 CDC ports, FS USB) to ESP32-P4 (6 CDC ports, HS USB).
Target board: Guition JC-ESP32P4-M3-Dev with ESP32-C6 companion (WiFi via ESP-Hosted) and IP101 Ethernet.

## Current Focus

_No current goal. Next planned item will be promoted on next planning session._

## Completed
- [x] Fix device stack build issues exposed by dual-instance integration (2026-02-25) `7507d1e`
- [x] Rename tusb_config.h to tusb_config_host.h to prevent shadowing (2026-02-25) `1d6bf64`
- [x] Add dual USB support: 2 FS CDC + 3 HS CDC on ESP32-P4 (2026-02-25) `71112a3`
- [x] Add local tinyusb component wrapper pointing to fork (2026-02-25) `b00af45`
- [x] Increase fan-out subscriber limit to fix "Too many subscribers" on bridge routes (2026-02-22) `a074ae1`
- [x] Fix route engine races: semaphore-based task join, error checking, rollback (2026-02-21) `8a03c6b`
- [x] Fix route persistence, SDIO GPIO conflict, and multi-wire UI bugs (2026-02-19) `aea130a`
- [x] Fix CDC stack overflow and node editor wire tracking (2026-02-18) `edffc39`
- [x] Update ROADMAP with LittleFS fix (2026-02-17) `4eaf25d`
- [x] Fix LittleFS path mismatch and enable frontend build (2026-02-17) `4b72f68`
- [x] Fix captive portal: use Host header check instead of specific URL list (2026-02-17) `faac5bb`
- [x] Add session handoff documents for USB CDC work (2026-02-17) `499c2f5`
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

### Investigated / Blocked

- [ ] **Dual USB CDC: 2 extra CDC ports on second USB interface (FS)** — BLOCKED by TinyUSB single-instance limitation. ESP32-P4 has two USB OTG peripherals (HS + FS) but TinyUSB can only run one device stack instance at a time ([esp-idf#15810](https://github.com/espressif/esp-idf/issues/15810), [tinyusb#3092](https://github.com/hathach/tinyusb/issues/3092)). Revisit when/if TinyUSB adds multi-instance support. Alternatives: (a) drop notification EPs to fit more CDC ports on existing HS interface, (b) use FS port as USB Host for external USB-serial adapters. (2026-02-25)

## Recent Planning Sessions
_Planning sessions will be recorded here automatically._

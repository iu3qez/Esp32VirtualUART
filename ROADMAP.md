# ESP32 Virtual UART - Roadmap

## Vision
**End goal:** All USB-CDC virtual ports dedicated to virtual UART routing (no debug port consumed).
Phase 1 provides 2 CDC-ACM ports. The debug console CDC port is a build-time feature
(`CONFIG_VUART_CDC_DEBUG_PORT`) — when disabled, all CDC ports are available for the project.

## Current Focus

**Frontend UX polish and mobile responsive layout**
- Started: 2026-02-16



## Completed
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
- [ ] Phase 8: USB CDC-ACM composite device with all available virtual COM ports
- [ ] OTA firmware update support (future)

## Recent Planning Sessions
_Planning sessions will be recorded here automatically._

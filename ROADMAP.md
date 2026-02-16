# ESP32 Virtual UART - Roadmap

## Current Focus
**Phase 1-7 implemented, testing and stabilization**
- All major components built (USB CDC, UART, TCP, routing, config, WiFi, web server, frontend)
- Ready for hardware testing and integration validation
- Started: 2026-02-13

## Completed
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
- [ ] Hardware testing on ESP32-S3-DevKitC (high priority)
- [ ] Signal line routing (DTR, RTS, CTS, DSR) end-to-end testing
- [ ] Baud rate conversion validation
- [ ] TCP client/server stress testing
- [ ] Frontend UX polish and mobile responsive layout
- [ ] OTA firmware update support (future)

## Recent Planning Sessions
_Planning sessions will be recorded here automatically._

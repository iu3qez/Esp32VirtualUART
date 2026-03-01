# Suggested Commands

All ESP-IDF commands require sourcing the environment first.

## Build & Flash

```bash
# Build firmware
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py build

# Flash (default port)
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 monitor

# Flash + monitor
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 flash monitor

# Clean build
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py fullclean

# Set target (after fullclean)
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py set-target esp32p4

# Menuconfig
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py menuconfig

# Binary size report
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py size
```

## Frontend

```bash
# Build Svelte SPA → data/www/
cd frontend && npm install && npm run build

# Dev server (proxies API to 192.168.4.1)
cd frontend && npm run dev

# Via CMake target
. /home/sf/esp/esp-idf/export.sh 2>/dev/null && cmake --build build --target frontend_build
```

## Testing
No unit test suite exists yet. Testing is manual on hardware.

## Linting / Formatting
No automated linting or formatting configured for C code.
Frontend has no ESLint/Prettier configured.

## System Utils
Standard Linux: `git`, `ls`, `cd`, `grep`, `find`, `rg` (ripgrep available)

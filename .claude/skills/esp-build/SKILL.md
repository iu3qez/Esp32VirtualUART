---
name: esp-build
description: Build, flash, or monitor ESP32 firmware. Usage - /esp-build build | /esp-build flash | /esp-build monitor | /esp-build flash-monitor | /esp-build fullclean | /esp-build menuconfig | /esp-build frontend
---

# ESP-IDF Build Skill

Run ESP-IDF commands with the environment automatically sourced.

## Commands

Based on the argument provided, run the corresponding command:

| Argument | Command |
|----------|---------|
| `build` | `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py build` |
| `flash` | `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 flash` |
| `monitor` | `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 monitor` |
| `flash-monitor` | `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py -p /dev/ttyUSB0 flash monitor` |
| `fullclean` | `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py fullclean` |
| `menuconfig` | `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py menuconfig` |
| `frontend` | `cd /home/sf/src/Esp32VirtualUART/frontend && npm install && npm run build` |
| `size` | `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py size` |

If no argument is given, default to `build`.

If the user specifies a serial port (e.g., `/dev/ttyACM0`), substitute it for `/dev/ttyUSB0` in the command.

## Notes

- Always run from the project root: `/home/sf/src/Esp32VirtualUART`
- The `frontend` command builds the Svelte SPA into `data/www/` for LittleFS
- After `fullclean`, a `set-target esp32p4` is needed before the next build

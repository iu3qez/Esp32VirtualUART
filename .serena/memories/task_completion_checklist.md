# Task Completion Checklist

When a coding task is completed, verify:

## Firmware (C)
1. **Build succeeds**: `. /home/sf/esp/esp-idf/export.sh 2>/dev/null && idf.py build`
2. **No new warnings** introduced (check build output)
3. **Binary size** hasn't grown unexpectedly (check with `idf.py size`)
4. **New components** added to `main/CMakeLists.txt` REQUIRES list
5. **New components** initialized in `main/main.c` init sequence
6. **New port types** added to `port_type_t` enum in `port_core/include/port.h`

## Frontend (Svelte)
1. **Build succeeds**: `cd frontend && npm run build`
2. **Output in `data/www/`** is updated
3. **API endpoints** match what `web_server` serves

## General
- Do NOT edit `managed_components/` (auto-downloaded)
- Do NOT edit `sdkconfig` directly (edit `sdkconfig.defaults`)
- Verify changes are consistent with existing component patterns

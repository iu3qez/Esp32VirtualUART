#!/usr/bin/env python3
"""Patch tusb_config.h for dual USB device mode (FS + HS).

The stock esp_tinyusb tusb_config.h only defines ONE rhport mode based on
CONFIG_TINYUSB_RHPORT_HS. For dual USB we need both:
  - CFG_TUSB_RHPORT0_MODE  (FS device)
  - CFG_TUSB_RHPORT1_MODE  (HS device)
  - CFG_TUD_MAX_RHPORT = 2
"""
import re
import sys

filepath = sys.argv[1]
with open(filepath, 'r') as f:
    content = f.read()

old = re.compile(
    r'#ifdef CONFIG_TINYUSB_RHPORT_HS\n'
    r'#\s+define CFG_TUSB_RHPORT1_MODE\s+.*\n'
    r'#else\n'
    r'#\s+define CFG_TUSB_RHPORT0_MODE\s+.*\n'
    r'#endif'
)

new = (
    '// Patched for dual USB: both FS and HS device mode\n'
    '#define CFG_TUSB_RHPORT0_MODE    (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)\n'
    '#define CFG_TUSB_RHPORT1_MODE    (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)\n'
    '#define CFG_TUD_MAX_RHPORT       2'
)

if old.search(content):
    content = old.sub(new, content)
    with open(filepath, 'w') as f:
        f.write(content)
    print('Patched tusb_config.h for dual USB device mode')
elif 'CFG_TUD_MAX_RHPORT' in content:
    print('tusb_config.h already patched for dual USB')
else:
    print('WARNING: Could not find rhport pattern to patch', file=sys.stderr)
    sys.exit(1)

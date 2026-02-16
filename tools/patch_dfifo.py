#!/usr/bin/env python3
"""Patch TinyUSB DWC2 RX FIFO from double-buffered (2x) to single-buffered (1x).

ESP32-P4 HS DWC2 has only 4096 bytes of FIFO. With the default 2x RX buffer,
6 CDC ports with 512-byte HS bulk endpoints exceed the FIFO budget by 96 words.
Single-buffered RX is sufficient for CDC serial data.
"""
import sys

filepath = sys.argv[1]
with open(filepath, 'r') as f:
    content = f.read()

old = '+ 2 * ((largest_ep_size / 4) + 1)'
new = '+ 1 * ((largest_ep_size / 4) + 1)'

if old in content:
    content = content.replace(old, new, 1)
    with open(filepath, 'w') as f:
        f.write(content)
    print('Patched DWC2 RX FIFO to single-buffered (1x)')
elif new in content:
    print('DWC2 RX FIFO already patched')
else:
    print('WARNING: Could not find FIFO pattern to patch', file=sys.stderr)
    sys.exit(1)

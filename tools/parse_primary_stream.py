#!/usr/bin/env python3
"""
parse_primary_stream.py — Parse the LDOS primary boot stream (T09/S0 to T10/S6)
to understand all record types, including undocumented ones.

Known record types from CLAUDE.md:
  0x01: PATCH(n, addr_lo, addr_hi, data[n-2])
  0x02: JUMP(skip, jmp_lo, jmp_hi)
  0x05: SKIP(n, ignored_bytes[n])
  0x1F: INFO(n, text[n])

We'll try to decode the unknown types by looking at patterns.
"""

import os, sys
from collections import Counter

DISK = os.path.join(os.path.dirname(__file__), '..', 'disks', 'ld1-531.dsk')
SPT = 10
BPS = 256

def read_sector(d, t, s):
    off = (t * SPT + s) * BPS
    return bytearray(d[off:off+BPS])

with open(DISK, 'rb') as f:
    disk = f.read()

# Build the primary stream: T09/S0 through T10/S6
primary = bytearray()
sectors_used = []
for t, s in ([(9, i) for i in range(10)] + [(10, i) for i in range(7)]):
    sec = read_sector(disk, t, s)
    sectors_used.append((t, s, len(primary)))
    primary += sec

print(f"Primary stream: {len(primary)} bytes ({len(sectors_used)} sectors)")
print()

# Parse the stream — try to figure out unknown types
# Strategy: parse known types; for unknown, print surrounding bytes and try heuristics

pos = 0
records = 0
high_patches = []

print("=== Primary Boot Stream Parse ===\n")

while pos < len(primary):
    t = primary[pos]

    if t == 0x00:
        # Trailing zeros (padding)
        zeros = 0
        while pos < len(primary) and primary[pos] == 0x00:
            zeros += 1
            pos += 1
        print(f"  {pos-zeros:04X}: PAD  ({zeros} zero bytes)")
        if zeros > 8:
            break

    elif t == 0x1F:  # INFO
        n = primary[pos+1]
        text = primary[pos+2:pos+2+n]
        printable = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in text)
        print(f"  {pos:04X}: INFO  n={n}  '{printable}'")
        pos += 2 + n
        records += 1

    elif t == 0x01:  # PATCH
        n = primary[pos+1]
        addr = primary[pos+2] | (primary[pos+3] << 8)
        data = primary[pos+4:pos+2+n]
        hex_d = ' '.join(f'{b:02X}' for b in data[:24])
        region = 'HIGH' if addr > 0x7FFF else 'low '
        print(f"  {pos:04X}: PATCH n={n} addr=0x{addr:04X}[{region}] ({n-2}B) = {hex_d}{'...' if len(data)>24 else ''}")
        if addr > 0x7FFF:
            high_patches.append((addr, bytes(data)))
        pos += 2 + n
        records += 1

    elif t == 0x02:  # JUMP
        skip = primary[pos+1]
        jmp  = primary[pos+2] | (primary[pos+3] << 8)
        print(f"  {pos:04X}: JUMP  skip={skip} → 0x{jmp:04X}")
        pos += 4
        records += 1

    elif t == 0x05:  # SKIP
        n = primary[pos+1]
        skip_data = primary[pos+2:pos+2+n]
        printable = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in skip_data)
        print(f"  {pos:04X}: SKIP  n={n}  '{printable}'")
        pos += 2 + n
        records += 1

    else:
        # Unknown — print context and stop
        ctx = ' '.join(f'{primary[pos+i]:02X}' for i in range(min(32, len(primary)-pos)))
        print(f"  {pos:04X}: ??? type=0x{t:02X}  [{ctx}]")
        # Attempt to figure out if this is another PATCH-like record
        # Common pattern: type n addr_lo addr_hi data[n-2] where n-2 = data bytes
        # Try n=primary[pos+1]; if it looks reasonable (2..128), try parsing
        guessed_n = primary[pos+1] if pos+1 < len(primary) else 0
        if 2 <= guessed_n <= 128:
            guessed_addr = primary[pos+2] | (primary[pos+3] << 8) if pos+3 < len(primary) else 0
            guessed_data = primary[pos+4:pos+2+guessed_n]
            print(f"         Guess PATCH-like: n={guessed_n} addr=0x{guessed_addr:04X} data[{guessed_n-2}]={' '.join(f'{b:02X}' for b in guessed_data[:16])}")
        break  # Stop and let the user know where we are

print(f"\nRecords: {records}, High patches: {len(high_patches)}")
if high_patches:
    print("High-RAM patches:")
    for a, d in high_patches:
        print(f"  0x{a:04X}-0x{a+len(d)-1:04X} ({len(d)} bytes)")

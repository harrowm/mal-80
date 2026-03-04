#!/usr/bin/env python3
"""Search disk for address bytes 0x73 0xF4 (address 0xF473 little-endian)."""
import sys

disk = open('disks/ld1-531.dsk', 'rb').read()
SPT = 10
BPS = 256

def sector_offset(t, s):
    return (t * SPT + s) * BPS

matches = []
for t in range(35):
    for s in range(SPT):
        off = sector_offset(t, s)
        data = disk[off:off+BPS]
        for i in range(len(data)-1):
            if data[i] == 0x73 and data[i+1] == 0xF4:
                matches.append(f'T{t:02d}/S{s}: offset 0x{i:02X} = {data[i]:02X} {data[i+1]:02X}')

print(f'Found {len(matches)} occurrences of 73 F4 (addr 0xF473 LE):')
for m in matches[:40]:
    print(m)

# Also check what's at T09/T10 sector data that would sit at RAM 0xF473
# T09 loads to 0x5100. If sequential: T09/S0 → 0x5100, S1→0x5200, ..., S9→0x5A00
# T10: S0→0x5B00, ... S6→0x6100
# 0xF473 would be 0xF473-0x5100 = 0xA373 = 41843 bytes past 0x5100
# That's WAY beyond 17 sectors (4352 bytes)
print(f'\n0xF473 offset from base 0x5100 = {0xF473-0x5100:#x} bytes = {(0xF473-0x5100)//256} sectors beyond sector 0')
print('T09/T10 only have 17 sectors = 4352 bytes → max addr 0x5100+4352-1 = 0x{:04X}'.format(0x5100+4352-1))

#!/usr/bin/env python3
# Dump SYS12 module from LDOS disk image (JV1 format)
import sys

TRACK = 20
SECTORS = [5, 6, 7, 8]
LOAD_ADDR = 0x50A0  # where LDOS puts it on a "16KB" system

with open('disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

def sector(t, s):
    off = (t * 10 + s) * 256
    return data[off:off+256]

sys12 = b''
for s in SECTORS:
    sys12 += sector(TRACK, s)

print(f'SYS12 (T{TRACK}/S{SECTORS[0]}-S{SECTORS[-1]}): {len(sys12)} bytes')
print(f'First byte: 0x{sys12[0]:02X}  (0x1F = module header marker)')
print()

# The SVC block is at 0x50B0-0x50B7 = offsets 16-23 from load address 0x50A0
print('=== Bytes at offsets 16-23 (mapped to 0x50B0-0x50B7 on 16KB system) ===')
for i in range(16, 24):
    b = sys12[i]
    ch = chr(b) if 0x20 <= b < 0x7F else '.'
    print(f'  offset {i:3d}  addr 0x{LOAD_ADDR+i:04X}: 0x{b:02X}  {ch}')

print()
print('=== First 128 bytes of SYS12 ===')
for i in range(0, 128, 16):
    chunk = sys12[i:i+16]
    hex_part = ' '.join(f'{b:02X}' for b in chunk)
    asc_part = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in chunk)
    print(f'  {LOAD_ADDR+i:04X}: {hex_part}  {asc_part}')

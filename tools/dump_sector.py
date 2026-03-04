#!/usr/bin/env python3
# Dump specific track/sector from ld1-531.dsk (JV1 format, SSSD, 256 bytes/sector)
# JV1: 10 sectors per track, 256 bytes/sector, sequential layout
import sys

SECTORS_PER_TRACK = 10
BYTES_PER_SECTOR  = 256

def sector_offset(t, s):
    return (t * SECTORS_PER_TRACK + s) * BYTES_PER_SECTOR

disk = open('disks/ld1-531.dsk','rb').read()

if len(sys.argv) >= 3:
    t, s = int(sys.argv[1]), int(sys.argv[2])
    off = sector_offset(t, s)
    data = disk[off:off+BYTES_PER_SECTOR]
    print(f'T{t:02d}/S{s}: offset=0x{off:06X}  ({BYTES_PER_SECTOR} bytes)')
    for row in range(0, 256, 16):
        hex_part = ' '.join(f'{data[row+j]:02X}' for j in range(16))
        chr_part = ''.join(chr(data[row+j]) if 0x20<=data[row+j]<0x7F else '.' for j in range(16))
        print(f'  {row:02X}: {hex_part}  |{chr_part}|')
else:
    # Dump track listing to see what's non-empty
    for t in range(35):
        for s in range(10):
            off = sector_offset(t, s)
            data = disk[off:off+BYTES_PER_SECTOR]
            if any(b != 0 for b in data):
                first8 = ' '.join(f'{b:02X}' for b in data[:8])
                print(f'T{t:02d}/S{s}: {first8}')

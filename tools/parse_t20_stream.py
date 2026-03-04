#!/usr/bin/env python3
"""
parse_t20_stream.py — Parse T20/S5-S8 as a LDOS boot stream.

Stream record types:
  0x01: PATCH(n, addr_lo, addr_hi, data[n-2])
  0x02: JUMP(skip, jmp_lo, jmp_hi)
  0x05: SKIP(n, ignored_bytes[n])
  0x1F: INFO(n, text[n])
"""

import os, sys

DISK = os.path.join(os.path.dirname(__file__), '..', 'disks', 'ld1-531.dsk')
SPT  = 10
BPS  = 256

def read_sector(d, t, s):
    off = (t * SPT + s) * BPS
    return bytearray(d[off:off+BPS])

def parse_stream(stream, label=""):
    if label:
        print(f"\n{'='*60}")
        print(f"  Stream: {label}")
        print(f"{'='*60}")
    pos = 0
    records = 0
    high_patches = []
    low_patches = []
    while pos < len(stream):
        t = stream[pos]

        if t == 0x1F:
            n = stream[pos+1]
            text = bytes(stream[pos+2:pos+2+n])
            printable = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in text)
            print(f"  {pos:04X}: INFO  n=0x{n:02X}  '{printable}'")
            pos += 2 + n
            records += 1

        elif t == 0x01:
            n = stream[pos+1]
            addr = stream[pos+2] | (stream[pos+3] << 8)
            data_len = n - 2
            data = stream[pos+4:pos+2+n]
            hex_data = ' '.join(f'{b:02X}' for b in data[:16])
            region = 'HIGH' if addr > 0x7FFF else 'low '
            print(f"  {pos:04X}: PATCH n=0x{n:02X} → 0x{addr:04X} [{region}] ({data_len}B) = {hex_data}{'...' if len(data)>16 else ''}")
            if addr > 0x7FFF:
                high_patches.append((addr, bytes(data)))
            else:
                low_patches.append((addr, bytes(data)))
            pos += 2 + n
            records += 1

        elif t == 0x02:
            skip = stream[pos+1]
            jmp  = stream[pos+2] | (stream[pos+3] << 8)
            print(f"  {pos:04X}: JUMP  skip=0x{skip:02X} → 0x{jmp:04X}")
            pos += 4
            records += 1

        elif t == 0x05:
            n = stream[pos+1]
            print(f"  {pos:04X}: SKIP  n=0x{n:02X}")
            pos += 2 + n
            records += 1

        elif t == 0x00:
            zeros = 0
            while pos < len(stream) and stream[pos] == 0:
                zeros += 1
                pos += 1
            print(f"  {pos-zeros:04X}: ({zeros} zero padding bytes)")
            if zeros > 4:
                break

        else:
            print(f"  {pos:04X}: UNKNOWN type=0x{t:02X}  next=[{' '.join(f'{stream[pos+i]:02X}' for i in range(min(12, len(stream)-pos)))}]")
            print(f"         STOP")
            break

    print(f"\nRecords: {records}  High-RAM patches: {len(high_patches)}  Low-RAM patches: {len(low_patches)}")
    if high_patches:
        print("High-RAM patch targets:")
        for addr, data in high_patches:
            print(f"  0x{addr:04X}-0x{addr+len(data)-1:04X}  ({len(data)} bytes)")
    return high_patches, low_patches

with open(DISK, 'rb') as f:
    disk = f.read()

# Build T20/S5-S8 stream
stream = bytearray()
for s in range(5, 9):
    sec = read_sector(disk, 20, s)
    stream += sec

parse_stream(stream, "T20/S5-S8 concatenated")

#!/usr/bin/env python3
import sys

disk = open('disks/ld1-531.dsk', 'rb').read()
SPT = 10
SPB = 256

def sector_offset(t, s):
    return (t * SPT + s) * SPB

# Dump T20/S5-S8 — the last sectors read before crash
print("=== T20/S5-S8 (last disk reads before crash) ===")
for t, s in [(20,5),(20,6),(20,7),(20,8)]:
    off = sector_offset(t, s)
    data = disk[off:off+SPB]
    hex_head = ' '.join(f'{b:02X}' for b in data[:32])
    hex_tail = ' '.join(f'{b:02X}' for b in data[-16:])
    print(f"T{t:02d}/S{s}: head=[{hex_head}]")
    print(f"         tail=[{hex_tail}]")

print()
print("=== T09 (SYS0) sector heads ===")
for s in range(10):
    off = sector_offset(9, s)
    data = disk[off:off+SPB]
    hex_head = ' '.join(f'{b:02X}' for b in data[:16])
    print(f"  T09/S{s}: {hex_head}")

print()
print("=== T10 (SYS1) sector heads ===")
for s in range(7):
    off = sector_offset(10, s)
    data = disk[off:off+SPB]
    hex_head = ' '.join(f'{b:02X}' for b in data[:16])
    print(f"  T10/S{s}: {hex_head}")

print()
print("=== Scanning ALL sectors for high-memory addresses (>= 0xE000) ===")
for t in range(35):
    for s in range(10):
        off = sector_offset(t, s)
        if off + 4 > len(disk):
            break
        data = disk[off:off+min(SPB, len(disk)-off)]
        # Check bytes at offset 0,1,2,3 for little-endian addresses
        for i in range(min(4, len(data)-1)):
            addr = data[i] | (data[i+1] << 8)
            if 0xE000 <= addr <= 0xFFFF:
                head = ' '.join(f'{b:02X}' for b in data[:8])
                print(f"  T{t:02d}/S{s} +{i}: addr=0x{addr:04X}  bytes=[{head}]")
                break

print()
print("=== T20 ALL sectors ===")
for s in range(10):
    off = sector_offset(20, s)
    if off + SPB > len(disk):
        print(f"  T20/S{s}: BEYOND EOF")
        break
    data = disk[off:off+SPB]
    hex_head = ' '.join(f'{b:02X}' for b in data[:16])
    print(f"  T20/S{s}: {hex_head}")

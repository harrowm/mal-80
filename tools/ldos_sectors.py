#!/usr/bin/env python3
SECTORS = 10
SECTOR_SIZE = 256

with open('disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

def read_sector(track, sector):
    off = (track * SECTORS + sector) * SECTOR_SIZE
    return data[off:off+SECTOR_SIZE]

def dump(track, sector, rows=4):
    sec = read_sector(track, sector)
    print(f'=== T{track:02d}/S{sector} ===')
    for i in range(0, rows*16, 16):
        h = ' '.join(f'{b:02X}' for b in sec[i:i+16])
        a = ''.join(chr(b) if 32<=b<127 else '.' for b in sec[i:i+16])
        print(f'  {i:03X}: {h}  |{a}|')

# T20/S5-S8 = what LDOS loads right before crash
for s in range(5, 9):
    dump(20, s, rows=4)

print()
print('=== T09/S0 (SYS0 start) ===')
dump(9, 0, rows=4)

print()
# T09/S3 had JP 0x4B8F, JP 0x4BA9 -> confirms code loaded at 0x4800ish
print('=== T09/S3 (has JP instructions) ===')
dump(9, 3, rows=3)

# Scan ALL sectors for writes that target 0xFExx:
# SYS files in relocated/absolute format usually have 2-byte load address per sector
# For LDOS: each sector of a SYS file is prefixed with 2 bytes: high-byte of segment addr
# Actually LDOS SYS format: first 2 bytes of each sector = segment address (big-endian)
print()
print('=== Scan all sectors for segment addresses >= 0xFE00 ===')
for track in range(35):
    for sector in range(10):
        sec = read_sector(track, sector)
        # Check if first 2 bytes look like a high-RAM load address
        addr = (sec[0] << 8) | sec[1]
        if 0xFE00 <= addr <= 0xFFFF:
            print(f'  T{track:02d}/S{sector}: first2=0x{addr:04X}')
        # Also search for 0xFE?? patterns as 16-bit values anywhere in sector
        for i in range(len(sec)-1):
            w = sec[i] | (sec[i+1] << 8)  # little-endian
            if 0xFE00 <= w <= 0xFFFE and i > 0:
                pass  # too noisy

print()
print('=== T09/S0 - S2 first 4 bytes (possible load addresses) ===')
for s in range(10):
    sec = read_sector(9, s)
    print(f'  T09/S{s}: {sec[0]:02X} {sec[1]:02X} {sec[2]:02X} {sec[3]:02X}')
for s in range(10):
    sec = read_sector(10, s)
    print(f'  T10/S{s}: {sec[0]:02X} {sec[1]:02X} {sec[2]:02X} {sec[3]:02X}')
for s in range(5, 9):
    sec = read_sector(20, s)
    print(f'  T20/S{s}: {sec[0]:02X} {sec[1]:02X} {sec[2]:02X} {sec[3]:02X}')

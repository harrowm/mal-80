#!/usr/bin/env python3
# Decode T17/S8 directory entry and T20 sectors to understand what LDOS is loading
import struct

disk = open('disks/ld1-531.dsk', 'rb').read()
SPT = 10
SPB = 256

def read_sector(t, s):
    off = (t * SPT + s) * SPB
    return disk[off:off+SPB]

# T17/S8 = directory entry for SYS5
print("=== T17/S8 raw (LDOS directory entry for SYS5) ===")
d = read_sector(17, 8)
print(' '.join(f'{b:02X}' for b in d[:48]))
print()

# LDOS FPDE (File Primary Directory Entry) format:
# Byte 0:    Attribute byte (file type)
# Byte 1:    End-of-file offset in last sector (0=256)
# Byte 2-7:  Filename (6 chars, padded with spaces)
# Byte 8:    Extension byte 0
# Byte 9:    Extension byte 1
# Byte 10:   Extension byte 2
# Byte 11:   File size in granules
# Byte 12-13: Date (packed)
# Byte 14+:  Extent Descriptors (FPDE has up to 4, each 2 bytes)
#            Extent: byte0 = (cylinder << 1) | active_flag, byte1 = granule info
# Actually the exact format varies — let me just dump all potential sector starts

# In LDOS JV1, directory entries are 32 bytes each, packed 8 per sector
# FPDE starts at offset where attribute != 0 and != 0xFE (deleted) and != 0xFF (free)
print("=== All 8 directory entries in T17/S8 ===")
for i in range(8):
    entry = d[i*32:(i+1)*32]
    attr = entry[0]
    # Filename is bytes 3-8 in LDOS format (after attr, EOF offset, and flag bytes)
    # Actually LDOS FPDE: offset 0 = attr, 1 = month(hi) | eof_sect(low), ...
    # Let try interpreting as: name at offset 3 (6 chars)
    name_bytes = entry[3:9]
    try:
        name = ''.join(chr(b) if 0x20 <= b < 0x7F else f'\\x{b:02X}' for b in name_bytes)
    except:
        name = '???'
    # First extent: bytes 13-14 typically  
    print(f"  Entry {i}: attr=0x{attr:02X}  raw[0:16]={' '.join(f'{b:02X}' for b in entry[:16])}")
    print(f"           raw[16:32]={' '.join(f'{b:02X}' for b in entry[16:32])}")

print()
print("=== T20 ALL sectors — first 16 bytes each ===")
for s in range(10):
    off = (20 * SPT + s) * SPB
    if off + 16 > len(disk):
        print(f"T20/S{s}: BEYOND EOF")
        break
    data = disk[off:off+16]
    hex16 = ' '.join(f'{b:02X}' for b in data)
    printable = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in data)
    print(f"T20/S{s}: {hex16}  |{printable}|")

print()
print("=== T20/S1 first 64 bytes (Z80 code? ===")
data = read_sector(20, 1)
print(' '.join(f'{b:02X}' for b in data[:64]))

print()
print("=== T20/S2 first 64 bytes ===")
data = read_sector(20, 2)
print(' '.join(f'{b:02X}' for b in data[:64]))

print()
print("=== T20/S3 first 64 bytes ===")
data = read_sector(20, 3)
print(' '.join(f'{b:02X}' for b in data[:64]))

print()
# Find which sectors of T20 are actually referenced by the directory
# The LDOS FDE uses 'granules' — on JV1 SSSD, 1 granule = 5 sectors (or similar)
# Let's look at what LDOS reads: T17/S5=seek target, ReadSec=T20/S5
# This means the FDE has the file starting at granule on physical track 20, extents pointing to S5
# The file extent: each extent descriptor gives track + sectors
# Let's look at what actual directory says about SYS5

# Actually let's also check T17/S5 (the OTHER directory entry read)
print("=== T17/S4 (first directory read, via T00/S4 seek) ===")
d4 = read_sector(17, 4)
for i in range(8):
    entry = d4[i*32:(i+1)*32]
    attr = entry[0]
    raw = ' '.join(f'{b:02X}' for b in entry[:16])
    print(f"  Entry {i}: attr=0x{attr:02X}  {raw}")
    
print()
print("=== T00/S0 boot sector first 64 bytes ===")
data = read_sector(0, 0)
print(' '.join(f'{b:02X}' for b in data[:64]))

print()
print("=== T00/S2 (read after RAM probe) first 64 bytes ===")
data = read_sector(0, 2)
print(' '.join(f'{b:02X}' for b in data[:64]))

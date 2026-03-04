#!/usr/bin/env python3
"""Inspect LDOS JV1 disk image structure."""
import sys

SECTORS = 10
SECTOR_SIZE = 256

def read_sector(data, track, sector):
    off = (track * SECTORS + sector) * SECTOR_SIZE
    return data[off:off+SECTOR_SIZE]

def dump_sector(data, track, sector, label=""):
    sec = read_sector(data, track, sector)
    h16 = ' '.join(f'{b:02X}' for b in sec[:16])
    a16 = ''.join(chr(b) if 32 <= b < 127 else '.' for b in sec[:16])
    print(f'  T{track:02d}/S{sector}: {h16}  |{a16}|  {label}')

with open('disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

print(f'Disk size: {len(data)} bytes = {len(data)//SECTOR_SIZE//SECTORS} tracks')

print()
print('=== TRACK 0 (boot) ===')
for s in range(4):
    dump_sector(data, 0, s)

print()
print('=== TRACK 17 (directory) ===')
labels = ['GAT','HIT','Dir0','Dir1','Dir2','Dir3','Dir4','Dir5','Dir6','Dir7']
for s in range(10):
    dump_sector(data, 17, s, labels[s] if s < len(labels) else '')

# Look for file names in directory sectors (sectors 2-9 of track 17)
print()
print('=== DIRECTORY ENTRIES (T17 S2-S9) ===')
for s in range(2, 10):
    sec = read_sector(data, 17, s)
    # Each directory entry is 48 bytes
    for e in range(256 // 48):
        off = e * 48
        entry = sec[off:off+48]
        if entry[0] == 0xFF:  # deleted
            continue
        if entry[0] == 0x00:  # empty
            continue
        name = ''.join(chr(b & 0x7F) for b in entry[0:8]).rstrip()
        ext  = ''.join(chr(b & 0x7F) for b in entry[8:11]).rstrip()
        print(f'  S{s} E{e}: {name}/{ext}  byte0=0x{entry[0]:02X}')

# Check what the boot sector contains
print()
print('=== BOOT SECTOR (T0/S0) full dump ===')
sec = read_sector(data, 0, 0)
for i in range(0, min(64, len(sec)), 16):
    h = ' '.join(f'{b:02X}' for b in sec[i:i+16])
    a = ''.join(chr(b) if 32 <= b < 127 else '.' for b in sec[i:i+16])
    print(f'  {i:03X}: {h}  |{a}|')

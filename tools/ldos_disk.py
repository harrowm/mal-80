#!/usr/bin/env python3
"""Analyse LDOS 5.3.1 JV1 disk image."""
import sys, struct

disk = open('/Users/malcolm/mal-80/disks/ld1-531.dsk', 'rb').read()
SPT = 10   # sectors per track
BPS = 256  # bytes per sector

def sector(track, sec):
    off = (track * SPT + sec) * BPS
    return disk[off:off+BPS]

def hexdump(data, width=16):
    for i in range(0, len(data), width):
        chunk = data[i:i+width]
        h = ' '.join(f'{b:02X}' for b in chunk)
        a = ''.join(chr(b) if 32<=b<127 else '.' for b in chunk)
        print(f'  {i:04X}: {h:{width*3-1}s}  {a}')

# -------------------------------------------------------------------------
# Directory (track 17)
# -------------------------------------------------------------------------
print("=== Track 17 - GAT (sector 0, first 16 bytes) ===")
hexdump(sector(17, 0)[:32])

print()
print("=== Track 17 - HIT (sector 1, first 32 bytes) ===")
hexdump(sector(17, 1)[:32])

print()
print("=== LDOS Directory entries (track 17, sectors 2-9) ===")
for sec in range(2, 10):
    s = sector(17, sec)
    for entry_off in range(0, BPS, 32):
        e = s[entry_off:entry_off+32]
        if e[0] == 0x00 or e[0] == 0xFF:
            continue  # empty slot
        # Directory entry format in LDOS:
        # [0..7]   filename (ASCII, high-bit = extension start marker)
        # [8..10]  extension (3 chars)
        # [13]     attributes
        # [14..15] EOF record  
        # [16..17] logical record length
        # [18..19] number of record positions
        # [20..21] granule allocation linked list (or starting granule)
        name = ''
        for i in range(0, 11):
            c = e[i] & 0x7F
            if c < 32 or c > 126: c = ord('?')
            if e[i] & 0x80 and i > 0:  # high bit = extension separator
                name += '/'
            name += chr(c)
        print(f'  T17/S{sec} off={entry_off:02X}: {name!r:16s}  {" ".join(f"{b:02X}" for b in e)}')

# -------------------------------------------------------------------------
# Find track/sector pointer to SYS0 (LSYSRES/SYS) which is the overlay loader
# -------------------------------------------------------------------------
print()
print("=== Scanning all sectors for 0x50B0 load address (4C 50 = LD ? 0x504C? or 50 B0 little-endian) ===")
# Load address in LDOS CMD header is at byte offset 2 (little-endian 16-bit)
for track in range(35):
    for sec in range(SPT):
        s = sector(track, sec)
        # Check if this is a module header (starts with 0x01 or module sig)
        # LDOS module header: typically 0x01 cmd_type + load_addr_lo + load_addr_hi + ...
        # OR: SYS file header sector has 0x1F as first byte
        if s[0] == 0x01 and s[1] == 0xB0 and s[2] == 0x50:
            print(f"  Found 0x50B0 load addr at T{track:02d}/S{sec}: {s[:16].hex()}")
        # LDOS module header format (from LDOS source): byte 0=type, byte 1-2=load addr lo/hi
        if len(s) >= 3 and s[1] == 0xB0 and s[2] == 0x50:
            print(f"  Possible 0x50B0 ref at T{track:02d}/S{sec} off 1: {s[:16].hex()}")
        # Check raw bytes 0x50B0 anywhere in sector (maybe as a word)
        for off in range(len(s)-1):
            if s[off] == 0xB0 and s[off+1] == 0x50:
                print(f"  Raw 0x50B0 at T{track:02d}/S{sec} off={off}: ctx={s[max(0,off-2):off+4].hex()}")

print()
print("=== SYS0 area (track 9, first sector) ===")
hexdump(sector(9, 0)[:64])
print()
print("=== SYS0 sector 1 ===")
hexdump(sector(9, 1)[:64])

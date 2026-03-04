#!/usr/bin/env python3
import sys

with open('/Users/malcolm/mal-80/disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

def sector_off(t, s):
    return (t * 10 + s) * 256

print("=== T20/S5 module header (first 64 bytes) ===")
off = sector_off(20, 5)
for row in range(4):
    chunk = data[off + row*16 : off + row*16 + 16]
    printable = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
    print(f"  {row*16:02X}: {chunk.hex(' ')}  |{printable}|")

print()
print("=== T20/S5 copyright parsed ===")
# byte 0 is 0x1F (control), bytes 1..0x32 are the copyright text
# byte at 0x33 is the first module data byte
copyright_len = data[off]
print(f"Byte 0 (tag/len?): 0x{copyright_len:02X} = {copyright_len}")
copyright_text = data[off+1 : off+0x33]
print(f"Copyright ({len(copyright_text)} bytes): {copyright_text}")
print(f"First module data byte (off 0x33): 0x{data[off+0x33]:02X}")
print(f"Module data at 0x33-0x3F: {data[off+0x33:off+0x40].hex(' ')}")

print()
print("=== T17/S0 GAT (Granule Allocation Table) ===")
gat = sector_off(17, 0)
# GAT: one byte per granule; bit=0 means free, bit=1 means used
# Two granules per track: granule N = track N//2, half N%2
# Track 20: granules 40, 41
for row in range(8):
    chunk = data[gat + row*16 : gat + row*16 + 16]
    printable = ''.join(f"G{row*16+i}={chunk[i]:02X} " for i in range(16))
    print(f"  {printable}")

print()
print("GAT bytes for granules 38-42 (tracks 19-21):")
for g in range(38, 43):
    t, h = g//2, g%2
    byte = data[gat + g]
    print(f"  Granule {g} (T{t}/half{h}): GAT[{g}] = 0x{byte:02X} = {byte:08b}")

print()
print("=== T17/S8 directory entry for SYS files ===")
dir8 = sector_off(17, 8)
# Each directory entry is 32 bytes, 8 per sector
for ent in range(3):
    entry = data[dir8 + ent*32 : dir8 + ent*32 + 32]
    if entry[0] == 0:
        break
    name = ''.join(chr(b) if 32 <= b < 127 else '?' for b in entry[5:16])
    print(f"Entry {ent}: attr=0x{entry[0]:02X}")
    print(f"  Name bytes: {entry[5:16].hex(' ')} = '{name}'")
    print(f"  Full entry: {entry.hex(' ')}")
    # LDOS directory format:
    # Bytes 14-28: extent records (5 bytes each: granule_count, last_sector_byte_count[2], granules[up to N])
    print(f"  Extent data (bytes 14+): {entry[14:].hex(' ')}")
    print()

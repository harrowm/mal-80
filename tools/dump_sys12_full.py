#!/usr/bin/env python3
"""
Dump SYS12 (and nearby SYS modules) content from the LDOS disk.
Also disassemble first 64 bytes of SYS12 at 0x4E00.
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))

DISK = "disks/ld1-531.dsk"
with open(DISK, "rb") as f:
    disk = bytearray(f.read())

def sec(t, s):
    return disk[(t * 10 + s) * 256 : (t * 10 + s + 1) * 256]

def hexdump(data, base=0, n=64):
    for i in range(0, n, 16):
        chunk = data[i:i+16]
        hex_p = ' '.join(f'{b:02X}' for b in chunk)
        asc_p = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in chunk)
        print(f"  {base+i:04X}: {hex_p:<47}  {asc_p}")

# Simple inline disassembler for the first 64 bytes of SYS12
# (just a hex+ASCII dump for now — see sector structure)
print("=" * 60)
print("Directory T17/S2-S9 (raw 32-byte entries)")
print("=" * 60)
for s in range(2, 10):
    b = sec(17, s)
    for e in range(0, 256, 32):
        ent = b[e:e+32]
        attr = ent[0]
        if attr == 0xFF:
            continue  # empty
        name = ''.join(chr(x) if 0x20 <= x < 0x7F else '?' for x in ent[0:8])
        ext  = ''.join(chr(x) if 0x20 <= x < 0x7F else '?' for x in ent[8:11])
        grans = [ent[13+i] for i in range(12) if ent[13+i] != 0xFF]
        eof_sec   = ent[25]
        eof_bytes = ent[26]
        print(f"  T17/S{s}[{e:02X}]: attr=0x{attr:02X} name=[{name}].[{ext}]  grans={grans}  eof_sec={eof_sec} eof_bytes={eof_bytes}")

# Granule to track/sector mapping: granule N → track=N//2, first_sector=(N%2)*5
def gran_to_track_sec(g):
    return g // 2, (g % 2) * 5

# We know SYS12 loads at 0x4E00.
# Find it in directory. The sector with load=0x4E00 is on a data track.
# From previous trace: T09 sectors include 0x4E00-range items.
# Let's also just look for load=0x4E00 in all sectors.
print()
print("=" * 60)
print("All sectors with LE load address 0x4000-0x5FFF (low LDOS range)")
print("=" * 60)
for t in range(35):
    for s in range(10):
        b = sec(t, s)
        # skip dir/config tracks
        if t == 17: continue
        load_le = b[0] | (b[1] << 8)
        if 0x4000 <= load_le <= 0x5FFF:
            print(f"  T{t:02d}/S{s}  load=0x{load_le:04X}  bytes[2..7]={' '.join(f'{b[i]:02X}' for i in range(2,8))}")

print()
print("=" * 60)
print("SYS12: looking for 0x4E00 sector then dumping it")
print("=" * 60)
for t in range(35):
    for s in range(10):
        if t == 17: continue
        b = sec(t, s)
        load_le = b[0] | (b[1] << 8)
        if load_le == 0x4E00:
            print(f"Found SYS12 at T{t:02d}/S{s}  load=0x{load_le:04X}")
            hexdump(b[2:], 0x4E00, 64)

print()
print("=" * 60)
print("SYS0 module byte-stream (T09/S0 full decode)")
print("=" * 60)
b = sec(9, 0)
hexdump(b, 0, 256)

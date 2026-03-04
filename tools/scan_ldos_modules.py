#!/usr/bin/env python3
"""
scan_ldos_modules.py — Parse a JV1 LDOS disk image and report each system
module's baked-in load address, so we know what HIGH$ the disk was built for.

JV1 layout: 35 tracks × 10 sectors/track × 256 bytes/sector = 89,600 bytes.

LDOS directory occupies T1–T5 (track 1, sectors 0-9 through track 5, 0-9).
Each directory entry is 32 bytes:
  [0]     status (0=free, 0x10=active file, 0x40=active system)
  [1..8]  filename (8 chars, space-padded)
  [9..10] extension (2 chars)
  [11]    attribute byte (0x02 = /SYS flag in LDOS)
  [12..13] file size in bytes (little-endian)
  [14..15] year/month/day (LDOS date format)
  [16..17] first granule pointer (track / sector*2 packed)
  [18..31] (more extent info)

Each LDOS module sector has a 2-byte header at offset 0:
  [0..1]  load address (little-endian) — where this 254-byte chunk goes in RAM
  [2..255] module data

Usage: python3 tools/scan_ldos_modules.py disks/ld1-531.dsk
"""

import sys
import os
import struct

TRACKS   = 35
SPT      = 10   # sectors per track
SSIZE    = 256  # bytes per sector

def read_sector(data: bytes, track: int, sector: int) -> bytes:
    offset = (track * SPT + sector) * SSIZE
    if offset + SSIZE > len(data):
        return b'\x00' * SSIZE
    return data[offset:offset + SSIZE]

def track_sector_from_granule(granule: int):
    """LDOS granule → (track, first_sector).  Each granule = 5 sectors."""
    track  = granule // 2
    sector = (granule % 2) * 5
    return track, sector

def read_file_sectors(data: bytes, first_gran: int, file_size: int):
    """Walk LDOS granule chain and yield sector payloads."""
    # LDOS GAT (Granule Allocation Table) lives at T17/S0.
    # HIT (Hash Index Table) lives at T17/S1.
    # We'll just trust the extent map in the directory entry for now and
    # do a simple linear read: granules are contiguous in simple cases.
    sectors = []
    remaining = file_size
    gran = first_gran
    while remaining > 0 and gran < 0xC0:  # 0xC0+ = end-of-chain in LDOS
        track, sector = track_sector_from_granule(gran)
        for s in range(5):
            if remaining <= 0:
                break
            sec = read_sector(data, track, sector + s)
            sectors.append(sec)
            remaining -= SSIZE
        # Next granule: read from GAT at T17/S0
        gat = read_sector(data, 17, 0)
        gran = gat[gran] if gran < len(gat) else 0xFF
    return sectors

def parse_directory(data: bytes):
    """Return list of (name, ext, attr, size, first_gran) for active entries."""
    entries = []
    for track in range(1, 6):
        for sector in range(SPT):
            sec = read_sector(data, track, sector)
            for i in range(0, SSIZE, 32):
                entry = sec[i:i+32]
                if len(entry) < 32:
                    continue
                status = entry[0]
                if status not in (0x10, 0x40):   # active file or system
                    continue
                name = entry[1:9].decode('ascii', errors='replace').rstrip()
                ext  = entry[9:11].decode('ascii', errors='replace').rstrip()
                attr = entry[11]
                size = struct.unpack_from('<H', entry, 12)[0]
                # First three extent fields each hold (gran, last_sector_size)
                first_gran = entry[16]
                entries.append((name, ext, attr, size, first_gran))
    return entries

def scan_module_load_addrs(data: bytes, name: str, ext: str,
                            size: int, first_gran: int):
    """
    Read a /SYS file's sectors and collect the 2-byte load address from each.
    Each 256-byte sector has: lo, hi = bytes 0,1; then 254 bytes of code.
    Returns a sorted list of (load_addr, sector_index).
    """
    sectors = read_file_sectors(data, first_gran, size)
    addrs = []
    for i, sec in enumerate(sectors):
        if len(sec) < 2:
            continue
        load_addr = struct.unpack_from('<H', sec, 0)[0]
        addrs.append((load_addr, i))
    return addrs

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'disks/ld1-531.dsk'
    if not os.path.exists(path):
        print(f"File not found: {path}")
        sys.exit(1)

    with open(path, 'rb') as f:
        data = f.read()

    print(f"Disk: {path}  ({len(data)} bytes, "
          f"{len(data) // (SPT * SSIZE)} tracks)\n")

    entries = parse_directory(data)
    print(f"Found {len(entries)} active directory entries.\n")

    all_load_addrs = []

    print(f"{'File':<14} {'Attr':>4}  {'Size':>6}  {'Load addresses (each sector)'}")
    print("-" * 72)

    for name, ext, attr, size, first_gran in sorted(entries):
        full = f"{name}/{ext}" if ext else name
        if size == 0 or first_gran >= 0xC0:
            print(f"  {full:<14} {attr:#04x}  {size:>6}  (no sectors)")
            continue

        addrs = scan_module_load_addrs(data, name, ext, size, first_gran)
        if not addrs:
            print(f"  {full:<14} {attr:#04x}  {size:>6}  (empty)")
            continue

        lo = min(a for a, _ in addrs)
        hi = max(a for a, _ in addrs)
        all_load_addrs.extend(a for a, _ in addrs)

        # Flag files that land in high RAM
        region = "HIGH" if hi > 0x7FFF else "low "
        print(f"  {full:<14} {attr:#04x}  {size:>6}  0x{lo:04X}–0x{hi:04X}  [{region}]")

    if all_load_addrs:
        abs_lo  = min(all_load_addrs)
        abs_hi  = max(all_load_addrs)
        # Each sector covers 254 bytes of code (2-byte header excluded)
        top_of_last = abs_hi + 254
        print()
        print(f"Lowest  module load addr : 0x{abs_lo:04X}")
        print(f"Highest module load addr : 0x{abs_hi:04X}")
        print(f"Top of last module chunk : ~0x{top_of_last:04X}  "
              f"(= estimated HIGH$ ceiling)")
        print()
        if abs_hi > 0x7FFF:
            print(">>> Disk requires HIGH RAM (>0x7FFF) — built for 16KB+expansion model")
        else:
            print(">>> All modules fit below 0x8000 — built for 16KB-only model")

if __name__ == '__main__':
    main()

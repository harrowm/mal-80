#!/usr/bin/env python3
"""
scan_load_addrs.py — Brute-force scan every sector of a JV1 disk image for
LDOS module sector headers.

Each LDOS /SYS module sector has bytes 0-1 = load address (little-endian),
followed by 254 bytes of Z80 code/data.  The load address is always in the
range 0x4000–0xFFFF.  We collect every unique address found and report the
memory layout the disk was built for.

Also dump the raw directory area (T1–T5) for reference.
"""

import sys
import os
import struct

TRACKS = 35
SPT    = 10
SSIZE  = 256

def read_sector(data, track, sector):
    off = (track * SPT + sector) * SSIZE
    return data[off:off + SSIZE] if off + SSIZE <= len(data) else b'\x00' * SSIZE

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'disks/ld1-531.dsk'
    data = open(path, 'rb').read()
    print(f"Disk: {path}  {len(data)} bytes  {len(data)//(SPT*SSIZE)} tracks\n")

    # ── Raw directory dump (T1–T2 worth of sectors) ──────────────────────────
    print("=== Raw directory sectors T1/S0 – T1/S4 (first 32 bytes each) ===")
    for s in range(5):
        sec = read_sector(data, 1, s)
        hex_bytes = ' '.join(f'{b:02X}' for b in sec[:32])
        asc = ''.join(chr(b) if 32 <= b < 127 else '.' for b in sec[:32])
        print(f"  T01/S{s}: {hex_bytes}  |{asc}|")
    print()

    # ── Scan every sector for load-address header ─────────────────────────────
    # Heuristic: first 2 bytes = load addr in [0x4000, 0xFFFF],
    # and the address is 256-byte aligned (module sectors are load-addr aligned).
    hits = {}   # load_addr → list of (track, sector)

    for track in range(TRACKS):
        for sector in range(SPT):
            sec = read_sector(data, track, sector)
            if len(sec) < 2:
                continue
            addr = struct.unpack_from('<H', sec, 0)[0]
            # Must be in LDOS RAM range and 256-byte (or 254-byte chunk) aligned
            if 0x4000 <= addr <= 0xFFFF:
                hits.setdefault(addr, []).append((track, sector))

    print("=== Module load addresses found across all sectors ===")
    print(f"  (sectors whose first 2 bytes = a plausible RAM address)\n")

    sorted_addrs = sorted(hits.keys())
    for addr in sorted_addrs:
        locs = hits[addr]
        region = "HIGH" if addr > 0x7FFF else "low "
        loc_str = ', '.join(f'T{t:02d}/S{s}' for t, s in locs[:4])
        if len(locs) > 4:
            loc_str += f' (+{len(locs)-4} more)'
        print(f"  0x{addr:04X}  [{region}]  found in: {loc_str}")

    print()
    if sorted_addrs:
        lo = min(sorted_addrs)
        hi = max(sorted_addrs)
        print(f"Address range: 0x{lo:04X} – 0x{hi:04X}")
        if hi > 0x7FFF:
            print(f"Highest address: 0x{hi:04X} → disk requires expansion RAM above 0x7FFF")
            print(f"Estimated HIGH$ (top of last 256-byte chunk): 0x{hi+0x100:04X}")
        else:
            print(f"All addresses ≤ 0x7FFF → disk built for 16KB-only (no expansion)")

    # ── Also show T0 boot track summary ──────────────────────────────────────
    print("\n=== Track 0 boot sectors (first 4 bytes each) ===")
    for s in range(SPT):
        sec = read_sector(data, 0, s)
        print(f"  T00/S{s}: {sec[0]:02X} {sec[1]:02X} {sec[2]:02X} {sec[3]:02X} ...")

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""
ldos_disk_map.py — Parse a JV1 LDOS 5.3.1 disk image and print a
comprehensive layout summary suitable for pasting into documentation.

LDOS JV1 disk structure:
  35 tracks x 10 sectors/track x 256 bytes/sector = 89,600 bytes
  Track 17 = system track  (GAT / HIT / directory)
  Track  0 = boot track    (sectors 0-2 = boot loader, rest varies)
  Tracks 1-16,18-34 = data area

LDOS directory format (T17/S2..S9, 8 entries × 32 bytes per sector):
  [0]    flags: 0x10=active file, 0x20=active /SYS attr, 0x08=active dir,
                0xFE=end-of-directory
  [1..8] filename, padded with spaces (NOT null-terminated)
  [9..10] extension (2 chars, e.g. 'SY')
  [11]   password attribute byte
  [12..13] EOF byte offset of last sector (LE); combine with extent table
           for true file size
  [14..15] date (month/day packed)
  [16..31] up to 4 extents, each 4 bytes: (gran_lo, gran_hi, lsn_lo, lsn_hi)
           gran = starting granule (0xFF = no extent)
           A "granule" in LDOS = 5 consecutive sectors on one track.
           Granule n → track = n//2, first_sector = (n%2)*5

LDOS module sector format (for /SYS files that are loaded into RAM):
  [0..1] load address LE  — where these 254 bytes go in Z80 RAM
  [2..255] Z80 code/data

Usage:
  python3 tools/ldos_disk_map.py [disk.dsk]
"""

import struct
import sys
import os

TRACKS    = 35
SPT       = 10
SSIZE     = 256
SYS_TRACK = 17

def read_sector(data, track, sector):
    off = (track * SPT + sector) * SSIZE
    if off + SSIZE > len(data):
        return b'\x00' * SSIZE
    return data[off : off + SSIZE]

def granule_to_ts(gran):
    """Granule number → (track, first_sector_on_that_track)."""
    return gran // 2, (gran % 2) * 5

def read_dir_entries(data):
    """Return list of raw 32-byte directory entries that are active."""
    entries = []
    for s in range(2, 10):
        sector = read_sector(data, SYS_TRACK, s)
        for i in range(0, SSIZE, 32):
            e = sector[i:i+32]
            if len(e) < 32:
                continue
            flag = e[0]
            if flag == 0xFE:                  # end of directory
                return entries
            if flag in (0x10, 0x20, 0x08):    # active entry
                entries.append(e)
    return entries

def parse_entry(e):
    """Parse a 32-byte directory entry."""
    flag = e[0]
    name = e[1:9].decode('latin1', errors='replace').rstrip()
    ext  = e[9:11].decode('latin1', errors='replace').rstrip()
    passwd_attr = e[11]
    eof_lsn  = struct.unpack_from('<H', e, 12)[0]  # last sector size hint
    # date: [14]=month, [15]=day (years stored elsewhere or omitted)
    month = e[14]
    day   = e[15]
    # extents at [16..31]: 4 × (gran, lsn_count) each 4 bytes
    extents = []
    for x in range(4):
        base = 16 + x * 4
        gran = e[base]
        if gran in (0xFF, 0x00):
            break
        lsn_end = e[base + 2]   # count of sectors used in last granule (0=full)
        extents.append((gran, lsn_end))
    return {
        'flag': flag,
        'name': name,
        'ext':  ext,
        'attr': passwd_attr,
        'eof':  eof_lsn,
        'month': month,
        'day':   day,
        'extents': extents,
    }

def file_sectors(data, extents):
    """Yield (track, sector, sector_data) for every sector of a file."""
    for gran, lsn_end in extents:
        track, start_sec = granule_to_ts(gran)
        # Each granule has 5 sectors; if lsn_end>0 it's the last sector count
        n_sectors = lsn_end if lsn_end > 0 else 5
        n_sectors = min(n_sectors, 5)
        for s in range(start_sec, start_sec + n_sectors):
            if s < SPT:
                yield track, s, read_sector(data, track, s)

def module_load_addrs(data, extents):
    """For a /SYS file: return sorted list of (load_addr, track, sector)."""
    addrs = []
    for track, sector, sec_data in file_sectors(data, extents):
        if len(sec_data) >= 2:
            addr = struct.unpack_from('<H', sec_data, 0)[0]
            if 0x4000 <= addr <= 0xFFFF:
                addrs.append((addr, track, sector))
    return sorted(addrs)

def gat_summary(data):
    """Return dict of gran → gat_value for allocated granules."""
    gat = read_sector(data, SYS_TRACK, 0)
    allocated = {}
    for g in range(192):
        v = gat[g]
        if v not in (0x00, 0xFF, 0xFE, 0xC0):
            allocated[g] = v
    return gat, allocated

def boot_track_summary(data):
    """Return (track, sector, load_addr or None, first8_bytes) for T00."""
    rows = []
    for s in range(SPT):
        sec = read_sector(data, 0, s)
        addr = struct.unpack_from('<H', sec, 0)[0]
        if 0x4000 <= addr <= 0xFFFF:
            la = f"0x{addr:04X}"
            region = "HIGH" if addr > 0x7FFF else "low "
        else:
            la = "  —   "
            region = "    "
        b8 = ' '.join(f'{x:02X}' for x in sec[:8])
        rows.append((s, la, region, b8))
    return rows

# ─────────────────────────────────────────────────────────────────────────────
def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'disks/ld1-531.dsk'
    if not os.path.exists(path):
        print(f"ERROR: file not found: {path}")
        sys.exit(1)

    data = open(path, 'rb').read()

    print(f"## LDOS Disk Map: `{os.path.basename(path)}`\n")
    print(f"Format: JV1  |  {len(data)} bytes  |  "
          f"{TRACKS} tracks × {SPT} sectors × {SSIZE} bytes\n")

    # ── Memory model ──────────────────────────────────────────────────────────
    print("### Memory model\n")
    print("TRS-80 Model I LDOS 5.3.1, built for **16 KB base + 32 KB expansion = 48 KB total**.\n")
    print("| Region | Address range | Contents |")
    print("|--------|--------------|----------|")
    print("| ROM | 0x0000–0x2FFF | Level II BASIC ROM (read-only) |")
    print("| Keyboard | 0x3800–0x3BFF | Memory-mapped keyboard matrix |")
    print("| VRAM | 0x3C00–0x3FFF | Video RAM (64×16 characters) |")
    print("| LDOS kernel | 0x4000–0x4FFF | Kernel code, variables, SVC table |")
    print("| SYS modules (low) | 0x5000–0x7FFF | Transient low-RAM overlay modules |")
    print("| SYS modules (high) | 0x8000–0xFFFF | Transient high-RAM overlay modules |")
    print()

    # ── Boot track (T00) ─────────────────────────────────────────────────────
    print("### Boot track (Track 0)\n")
    print("| Sector | Load addr | Region | First 8 bytes |")
    print("|--------|----------|--------|---------------|")
    for s, la, region, b8 in boot_track_summary(data):
        print(f"| T00/S{s} | {la} | {region} | `{b8}` |")
    print()
    print("> T00/S0–S1: Primary boot loader (loads to ~0xFE00, then copies kernel to 0x4200).\n")

    # ── System track (T17) ───────────────────────────────────────────────────
    print("### System track (Track 17)\n")
    print("| Sector | Purpose |")
    print("|--------|---------|")
    print("| T17/S0 | GAT — Granule Allocation Table (192 bytes, 1 byte per granule) |")
    print("| T17/S1 | HIT — Hash Index Table (directory search accelerator) |")
    print("| T17/S2–S9 | Directory entries (8 × 32 bytes per sector = 64 entries max) |")
    print()

    # ── Directory listing ─────────────────────────────────────────────────────
    print("### Directory listing\n")
    raw_entries = read_dir_entries(data)
    entries = [parse_entry(e) for e in raw_entries]

    print("| File | Attr | Granules | Low load range | High load range | Note |")
    print("|------|------|----------|---------------|-----------------|------|")

    for en in sorted(entries, key=lambda e: e['name']+e['ext']):
        full = f"{en['name']}/{en['ext']}" if en['ext'] else en['name']
        grans = ', '.join(f"G{g}(T{granule_to_ts(g)[0]:02d}/S{granule_to_ts(g)[1]})" for g, _ in en['extents'])
        is_sys = (en['attr'] & 0x02) or en['ext'] in ('SY',)

        if en['extents'] and is_sys:
            addrs = module_load_addrs(data, en['extents'])
            lo_addrs = [a for a, t, s in addrs if a <= 0x7FFF]
            hi_addrs = [a for a, t, s in addrs if a > 0x7FFF]
            lo_str = f"0x{min(lo_addrs):04X}–0x{max(lo_addrs):04X}" if lo_addrs else "—"
            hi_str = f"0x{min(hi_addrs):04X}–0x{max(hi_addrs):04X}" if hi_addrs else "—"
            note = "/SYS module"
        else:
            lo_str = "—"
            hi_str = "—"
            note = "data/cmd"

        attr_s = f"0x{en['attr']:02X}"
        print(f"| `{full}` | {attr_s} | {grans or '—'} | {lo_str} | {hi_str} | {note} |")

    print()

    # ── System module detail ──────────────────────────────────────────────────
    print("### System module load map (sector-by-sector)\n")
    print("Each `/SYS` sector has a 2-byte LE load address followed by 254 bytes of Z80 code/data.\n")
    print("| Sector | Load addr | Region | Purpose |")
    print("|--------|----------|--------|---------|")

    # Known sector purposes from LDOS 5.3.1 source docs
    known = {
        (0,0):  "Boot loader (first stage)",
        (0,1):  "Boot loader (second stage)",
        (17,0): "GAT (Granule Allocation Table)",
        (17,1): "HIT (Hash Index Table)",
        (17,4): "System file index / config block",
    }

    # Scan all sectors that look like module headers
    for track in range(TRACKS):
        for sector in range(SPT):
            sec_data = read_sector(data, track, sector)
            addr = struct.unpack_from('<H', sec_data, 0)[0]
            if 0x4000 <= addr <= 0xFFFF:
                region = "HIGH" if addr > 0x7FFF else "low "
                purpose = known.get((track, sector), "")
                print(f"| T{track:02d}/S{sector} | 0x{addr:04X} | {region} | {purpose} |")

    print()

    # ── Memory range summary ──────────────────────────────────────────────────
    print("### Aggregate load address ranges\n")

    all_addrs = []
    for track in range(TRACKS):
        for sector in range(SPT):
            sec_data = read_sector(data, track, sector)
            addr = struct.unpack_from('<H', sec_data, 0)[0]
            if 0x4000 <= addr <= 0xFFFF:
                all_addrs.append(addr)

    if all_addrs:
        lo_all  = [a for a in all_addrs if a <= 0x7FFF]
        hi_all  = [a for a in all_addrs if a > 0x7FFF]
        print(f"- **Low RAM** (0x4000–0x7FFF): 0x{min(lo_all):04X} – 0x{max(lo_all):04X}  "
              f"({len(lo_all)} sectors)")
        print(f"- **High RAM** (0x8000–0xFFFF): 0x{min(hi_all):04X} – 0x{max(hi_all):04X}  "
              f"({len(hi_all)} sectors)")
        print(f"- **Highest load address**: 0x{max(all_addrs):04X} "
              f"(+ 254 bytes → top at ~0x{max(all_addrs)+254:04X})")
        print(f"- **HIGH\\$** (as set by SYS12 init): `0xFFFF`  "
              f"— disk was built for full 48 KB")
    print()

    # ── Key LDOS variables ────────────────────────────────────────────────────
    print("### Key LDOS variable addresses (from LDOS 5.3.1 source)\n")
    print("| Address | Name | Description |")
    print("|---------|------|-------------|")
    print("| 0x4000 | JMPDO | JP to LDOS SVC dispatcher |")
    print("| 0x4003 | JMPNO | JP to LDOS NMI handler |")
    print("| 0x4006 | JMPTO | Initial JP entry after boot |")
    print("| 0x403D | RAMFLG | Bit 3 = expansion RAM present (set by boot after RAM check) |")
    print("| 0x4040 | FCBMAP | Active FCB map |")
    print("| 0x4049 | HIGH$ (lo) | Low byte of top of available RAM |")
    print("| 0x404A | HIGH$ (hi) | High byte of top of available RAM |")
    print("| 0x404B | LDRFLG | Loader flags |")
    print("| 0x50B0 | SVCJMP | SVC chain: JP opcode (0xC3) |")
    print("| 0x50B1 | SVCADR | SVC chain: target address (2 bytes) |")
    print("| 0x50B4 | SVCCNT | SVC entry count |")
    print("| 0x50B6 | SVCNXT | Next SVC chain link (2 bytes, chain ptr) |")
    print()

    # ── Boot sequence ─────────────────────────────────────────────────────────
    print("### Boot sequence\n")
    print("```")
    print("ROM at reset → reads T00/S0 → loads to 0x4200 (primary boot loader)")
    print("  └─ reads system index (T17/S4) to discover /SYS file layout")
    print("  └─ reads LDOS kernel sectors (T09/S0..S10/S6) → staging at 0x5100")
    print("  └─ copies each sector to its baked-in load address via 0x4CDB loop")
    print("       ├─ checks 0x403D bit3 to decide whether to load HIGH-RAM modules")
    print("       ├─ low-RAM sectors  → copied directly (0x4E00, 0x4F00, 0x5000, ...)")
    print("       └─ high-RAM sectors → ONLY copied if bit3 of 0x403D = 1")
    print("  └─ sets up SVC table at 0x50B0, sets HIGH$=0xFFFF")
    print("  └─ calls module init chain starting at HIGH$ address")
    print("  └─ displays 'Date (MM/DD/YY)?' prompt")
    print("  └─ LDOS ready prompt: LDOS READY")
    print("```")
    print()
    print("### Known boot bug in this emulator (as of 2026-03-03)\n")
    print("The boot loader reads `0x403D` to check if expansion RAM is installed.")
    print("At boot the ROM leaves `0x403D = 0x00` (bit 3 clear), so the loader")
    print("skips all high-RAM module copies.  High-RAM module init pointers like")
    print("`0xDEB6`, `0xFAxx`, `0xFFFF` are then called into uninitialised RAM → crash.")
    print()
    print("**Fix required**: intercept reads of `0x403D` from LDOS code (PC ≥ 0x4200)")
    print("and return bit 3 set, so the boot loader copies high-RAM modules correctly.")

if __name__ == '__main__':
    main()

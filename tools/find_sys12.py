#!/usr/bin/env python3
"""
find_sys12.py - Find and disassemble SYS/12 from the LDOS disk.
LDOS JV1: 35 tracks x 10 sectors x 256 bytes.
Directory at T17/S2-S9, 8 entries per sector (32 bytes each).
GAT at T17/S0, HIT at T17/S1.
Granule = 1 sector on a 10-sector/track JV1 disk... actually
  for LDOS: granule size = 3 sectors, track = 10 sectors = 3 grans + 1 dir sec.
  T17 = directory track (no grans).
  Granule number: track * 3 + gran_within_track (0..2), but skip T17.
Actually for LDOS JV1 with 10 secs/track:
  GAT byte[track] = granule alloc bitmap (bits 0-2 = grans 0-2 of that track, bit7=system)
  Each gran = 5 sectors in some LDOS versions, but JV1 10 secs/track:
  LDOS uses 2 grans per track at 5 sectors each: bits 0 and 1
  Actually we'll just scan sectors for SYS modules by load address.
"""

import sys

DISK = "disks/ld1-531.dsk"
SEC_SIZE = 256
SECS_PER_TRACK = 10

def read_sector(f, track, sector):
    offset = (track * SECS_PER_TRACK + sector) * SEC_SIZE
    f.seek(offset)
    return f.read(SEC_SIZE)

def hexdump(data, base_addr=0, length=None):
    if length is None:
        length = len(data)
    for i in range(0, length, 16):
        row = data[i:i+16]
        hx = ' '.join(f'{b:02X}' for b in row)
        ch = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
        print(f"  {base_addr+i:04X}: {hx:<48}  {ch}")

with open(DISK, 'rb') as f:
    # --- Step 1: Decode directory ---
    print("=== LDOS SYS Files in Directory (T17/S2-S9) ===")
    sys_files = {}
    for sec in range(2, 10):
        data = read_sector(f, 17, sec)
        for i in range(0, 256, 32):
            e = data[i:i+32]
            attr = e[0]
            if attr == 0:
                continue
            # LDOS dir entry format:
            # [0] = attribute
            # [1..8] = filename (with high-bit flags in some bytes)
            # [9..11] = extension
            # [12] = EOF sector within last granule
            # [13..23] = granule list (0xFF = end/unused)
            # [24] = ?  [25..26] = logical record length(LE)
            # [27..30] = ?
            name = ''.join(chr(b & 0x7F) for b in e[1:9]).rstrip()
            ext  = ''.join(chr(b & 0x7F) for b in e[9:12]).rstrip()
            eof_sec = e[12]
            grans = []
            for g in e[13:24]:
                if g == 0xFF:
                    break
                grans.append(g)
            if ext == 'SYS' or ext == '/SY' or 'SYS' in ext or name.startswith('SYS'):
                print(f"  T17/S{sec}[{i:02X}] attr=0x{attr:02X} '{name}'/'{ext}' eof_sec={eof_sec} grans={grans}")
                sys_files[name.strip() + '/' + ext.strip()] = grans

    # --- Step 2: Scan ALL sectors for sectors that become code at 0x4E00 ---
    # The boot stream records use PATCH records: type 0x01, N, addr_lo, addr_hi, data...
    # Rather than following the stream, let's scan what T09 onward produces.
    # We know from the boot that JP(HL)=0x4E00. Let's find which PATCH record patches 0x4E00.
    # T09/S0 is the start of the boot stream (track=9, count=3 groups from T17/S4 config).
    # The stream spans multiple sectors. Let's fast-parse the STREAM to find the 0x4E00 PATCH.

    print("\n=== Parsing LDOS boot module stream for PATCH to 0x4E00 ===")
    # Stream starts at T09/S0. The stream is a byte sequence across sectors.
    # We'll build a flat buffer from T09 onwards (a few tracks worth).
    # Then parse LDOS module record types.

    stream = bytearray()
    # T09/S0 first two bytes are NOT stream bytes - they are the load address (LE) of that
    # sector. Wait, from prior analysis: the FDC reads sectors as raw 256-byte blocks.
    # The FIRST sector T09/S0 has bytes: 05 06 53 59 53 30... = SKIP(6) then INFO...
    # So the ENTIRE sector content IS the stream (no load-addr prefix in stream sectors).
    # Actually the boot loader reads sectors into 0x5100 buffer and processes from there.
    # Let's just gather T09/S0 through T12/S9 as stream.

    for track in range(9, 14):
        for sec in range(0, 10):
            d = read_sector(f, track, sec)
            stream.extend(d)

    print(f"  Stream buffer: {len(stream)} bytes (T09/S0 through T13/S9)")

    # Parse stream records
    pos = 0
    target_found = False
    patch_map = {}  # addr -> (pos_in_stream, data)

    while pos < len(stream) - 4:
        rectype = stream[pos]
        if rectype == 0x05:  # SKIP N bytes
            n = stream[pos+1]
            pos += 2 + n
        elif rectype == 0x1F:  # INFO N bytes text
            n = stream[pos+1]
            text = bytes(stream[pos+2:pos+2+n]).decode('ascii', errors='replace')
            print(f"  @{pos:04X} INFO({n}): {text!r}")
            pos += 2 + n
        elif rectype == 0x01:  # PATCH: N, addr_lo, addr_hi, data[N-2]
            n = stream[pos+1]
            if n < 2:
                print(f"  @{pos:04X} PATCH n={n} too small, stopping")
                break
            addr = stream[pos+2] | (stream[pos+3] << 8)
            data = bytes(stream[pos+4:pos+2+n])
            data_len = n - 2
            print(f"  @{pos:04X} PATCH addr=0x{addr:04X} n={n} data_len={data_len}  [{' '.join(f'{b:02X}' for b in data[:8])}{'...' if data_len>8 else ''}]")
            patch_map[addr] = (pos, data)
            if addr <= 0x4E00 < addr + data_len:
                off = 0x4E00 - addr
                print(f"    *** 0x4E00 is within this PATCH! offset={off}")
                target_found = True
            pos += 2 + n
        elif rectype == 0x02:  # JUMP dispatch: addr_lo, addr_hi  (JP(HL) target)
            addr = stream[pos+1] | (stream[pos+2] << 8)
            print(f"  @{pos:04X} JUMP 0x{addr:04X}  ← boot dispatch target")
            pos += 3
            if addr == 0x4E00:
                print("    *** This is the SYS12 dispatch!")
                target_found = True
        elif rectype == 0x03:  # END of stream
            print(f"  @{pos:04X} END")
            break
        elif rectype == 0x00:
            pos += 1  # padding/NOP
        else:
            print(f"  @{pos:04X} UNKNOWN rectype=0x{rectype:02X}, stopping parse")
            break

    # --- Step 3: Find and dump the memory that gets loaded to 0x4E00 ---
    print("\n=== Content at virtual 0x4E00 after all PATCHes ===")
    # Build a virtual RAM image
    ram = bytearray(0x10000)
    # Apply all patches in order
    for addr, (pos, data) in sorted(patch_map.items()):
        for j, b in enumerate(data):
            if addr + j < 0x10000:
                ram[addr + j] = b
    print("  First 64 bytes at 0x4E00:")
    hexdump(ram[0x4E00:0x4E40], 0x4E00)

    # Also show interesting vector areas
    print("\n  LDOS vector area 0x4040-0x4080:")
    hexdump(ram[0x4040:0x4080], 0x4040)

    print("\n  Boot loader area 0x4200-0x4240:")
    hexdump(ram[0x4200:0x4240], 0x4200)

    # --- Step 4: Raw sector scan for sectors with data that looks like 0x4E00 code ---
    print("\n=== Scanning all sectors for load_addr=0x4E00 (LE bytes 0x00,0x4E) ===")
    for track in range(0, 35):
        for sec in range(0, 10):
            d = read_sector(f, track, sec)
            if len(d) >= 2 and d[0] == 0x00 and d[1] == 0x4E:
                print(f"  T{track:02d}/S{sec} load=0x4E00  data[2..9]={' '.join(f'{b:02X}' for b in d[2:10])}")
                hexdump(d[2:66], 0x4E00)

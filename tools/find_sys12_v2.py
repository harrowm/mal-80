#!/usr/bin/env python3
"""Scan all sectors for ones whose LE load address is 0x4E00 or nearby."""

DISK = "disks/ld1-531.dsk"
SEC_SIZE = 256
SECS_PER_TRACK = 10
TOTAL_TRACKS = 35

def hexdump(data, base_addr=0):
    for i in range(0, len(data), 16):
        row = data[i:i+16]
        hx = ' '.join(f'{b:02X}' for b in row)
        ch = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
        print(f"  {base_addr+i:04X}: {hx:<48}  {ch}")

with open(DISK, 'rb') as f:
    print("=== Scanning all sectors ===")
    all_sectors = {}
    for track in range(TOTAL_TRACKS):
        for sec in range(SECS_PER_TRACK):
            offset = (track * SECS_PER_TRACK + sec) * SEC_SIZE
            f.seek(offset)
            data = f.read(SEC_SIZE)
            load_addr = data[0] | (data[1] << 8)
            all_sectors[(track, sec)] = (load_addr, data)

    # Show sectors with load addresses in 0x4E00-0x4FFF range
    print("\n=== Sectors with load_addr in 0x4E00-0x5000 ===")
    for (t, s), (addr, data) in sorted(all_sectors.items()):
        if 0x4E00 <= addr <= 0x5000:
            print(f"  T{t:02d}/S{s} load=0x{addr:04X}  data[2..9]={' '.join(f'{d:02X}' for d in data[2:10])}")
            hexdump(data[2:66], addr)
            print()

    # Also scan for sectors whose content looks like Z80 code starting at 0x4E00
    # (first non-zero bytes suggest code, not data)
    print("=== All sectors with load_addr in 0x4000-0x5200 (low LDOS range) ===")
    for (t, s), (addr, data) in sorted(all_sectors.items(), key=lambda x: x[1][0]):
        if 0x4000 <= addr <= 0x5200:
            nonzero = sum(1 for b in data[2:] if b != 0)
            print(f"  T{t:02d}/S{s} load=0x{addr:04X}  nonzero={nonzero}  first8=[{' '.join(f'{d:02X}' for d in data[2:10])}]")

    # Also check: what does FDC actually load during boot?
    # Boot loader reads sectors to 0x5100 staging buffer.
    # Those staging sector bytes: first 2 bytes ARE the stream content.
    # But there's also another mechanism: after boot, LDOS uses FDC to load system files.
    # The sector with load_addr=0x4E00 might be part of SYS/12 file.
    # Let's find SYS/12 in the directory and trace its granules.
    
    print("\n=== Looking for SYS/12 file (attribute 0x5F, name containing '12') ===")
    # LDOS dir entry: [0]=attr, [1..8]=name (strip high bits), [9..11]=ext
    # attr 0x5F = system file visible
    for sec in range(2, 10):
        offset = (17 * SECS_PER_TRACK + sec) * SEC_SIZE
        f.seek(offset)
        data = f.read(SEC_SIZE)
        for i in range(0, 256, 32):
            e = data[i:i+32]
            attr = e[0]
            if attr == 0:
                continue
            name_raw = bytes(e[1:9])
            ext_raw = bytes(e[9:12])
            name = ''.join(chr(b & 0x7F) for b in name_raw).strip()
            ext  = ''.join(chr(b & 0x7F) for b in ext_raw).strip()
            # Granule list at e[13..] up to 0xFF
            grans = []
            for k in range(13, 32):
                g = e[k]
                if g == 0xFF:
                    break
                grans.append(g)
            print(f"  T17/S{sec}[{i:02X}] attr=0x{attr:02X} name={name!r} ext={ext!r} grans={grans[:10]}")
            if '12' in name or (ext == '/SY' or 'SY' in ext):
                print(f"    ^^^ POTENTIAL SYS/12")

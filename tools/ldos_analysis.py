#!/usr/bin/env python3
"""Deeper LDOS disk analysis - search for error strings, map file extents."""

SECTORS = 10
SECTOR_SIZE = 256

with open('disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

def read_sector(track, sector):
    off = (track * SECTORS + sector) * SECTOR_SIZE
    return data[off:off+SECTOR_SIZE]

# Search all sectors for ASCII strings containing 'ERROR' or 'UNKNOWN'
print('=== Searching for error strings ===')
search_terms = [b'ERROR', b'UNKNOWN', b'SYSTEM', b'MEMORY SIZE', b'RESTART']
for track in range(35):
    for sector in range(10):
        sec = read_sector(track, sector)
        for term in search_terms:
            if term in sec:
                off = sec.index(term)
                ctx = sec[max(0,off-8):off+len(term)+8]
                printable = ''.join(chr(b) if 32 <= b < 127 else '.' for b in ctx)
                print(f'  T{track:02d}/S{sector}: {term.decode()} at +{off}: ...{printable}...')

# Show the file allocation tables
print()
print('=== GAT (T17/S0) - Granule allocation ===')
gat = read_sector(17, 0)
# First 35 bytes = track allocation bitmap (1 bit per granule)
# Tracks 0-17 = system, 18-34 = user
for t in range(35):
    print(f'  Track {t:02d}: 0x{gat[t]:02X} = {gat[t]:08b}', end='')
    if gat[t] == 0xFF:
        print(' (full/reserved)', end='')
    elif gat[t] == 0x00:
        print(' (free)', end='')
    print()

print()
print('=== Directory entries - file locations ===')
for s in range(2, 10):
    sec = read_sector(17, s)
    for e in range(256 // 48):
        off = e * 48
        entry = sec[off:off+48]
        if entry[0] in (0x00, 0xFF):
            continue
        # Parse LDOS directory entry
        flag = entry[0]
        # File name at bytes 5-12 (with bit7 set on last char of name)
        # Actually: bytes 0=attr, 1-4=extent info, 5-12=filename, 13-15=ext
        name_bytes = entry[5:13]
        ext_bytes = entry[13:16]
        name = ''.join(chr(b & 0x7F) for b in name_bytes).rstrip()
        ext  = ''.join(chr(b & 0x7F) for b in ext_bytes).rstrip()
        # Extent data: pairs of (granule, count) at bytes 16-47
        extents = []
        for i in range(0, 32, 2):
            gran = entry[16+i]
            cnt  = entry[17+i]
            if gran == 0xFF:
                break
            extents.append((gran, cnt))
        print(f'  S{s}E{e}: {name}/{ext} flag=0x{flag:02X} extents={extents}')

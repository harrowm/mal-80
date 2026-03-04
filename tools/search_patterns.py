#!/usr/bin/env python3
"""Search T09/T10 sectors for key LDOS address patterns."""
import sys, os
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

disk = open('disks/ld1-531.dsk', 'rb').read()
SPT = 10
BPS = 256

def sector_offset(t, s):
    return (t * SPT + s) * BPS

def dump_context(label, data, i, extra=6):
    start = max(0, i - 4)
    end = min(len(data), i + extra + 4)
    ctx = ' '.join(f'{b:02X}' for b in data[start:end])
    print(f'  {label}: ..{ctx}..')

search_patterns = {
    'LD (0x50B6),HL = 22 B6 50': bytes([0x22, 0xB6, 0x50]),
    'LD HL,(0x50B6) = 2A B6 50': bytes([0x2A, 0xB6, 0x50]),
    '0x441E LE       = 1E 44': bytes([0x1E, 0x44]),
    '0x50B6 LE       = B6 50': bytes([0xB6, 0x50]),
    '0xF473 LE       = 73 F4': bytes([0x73, 0xF4]),
}

print('Searching T09 and T10 sectors:')
for t in [9, 10]:
    for s in range(10):
        off = sector_offset(t, s)
        data = disk[off:off+BPS]
        for label, pat in search_patterns.items():
            for i in range(len(data) - len(pat) + 1):
                if data[i:i+len(pat)] == pat:
                    dump_context(f'T{t:02d}/S{s} off=0x{i:02X} "{label}"', data, i)

print('\nSearching all other tracks for 22 B6 50 or 2A B6 50:')
for t in range(35):
    if t in [9, 10, 17]:
        continue
    for s in range(10):
        off = sector_offset(t, s)
        data = disk[off:off+BPS]
        for pat, name in [(bytes([0x22,0xB6,0x50]), 'LD(0x50B6),HL'), (bytes([0x2A,0xB6,0x50]), 'LD HL,(0x50B6)')]:
            for i in range(len(data) - len(pat) + 1):
                if data[i:i+len(pat)] == pat:
                    dump_context(f'T{t:02d}/S{s} off=0x{i:02X} "{name}"', data, i)

#!/usr/bin/env python3
"""Search T09/T10 for key patterns including error code 0xFA and address 0x5084."""
import sys, os
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

disk = open('disks/ld1-531.dsk', 'rb').read()
SPT = 10
BPS = 256

def sector_offset(t, s):
    return (t * SPT + s) * BPS

def dump_around(data, i, label, pre=6, post=12):
    start = max(0, i - pre)
    end = min(len(data), i + post)
    ctx = ' '.join(f'{b:02X}' for b in data[start:end])
    print(f'  {label}: [{ctx}]')

# T09/T10 sequential load: T09/S0 -> 0x5100, S1->0x5200, ..., S9->0x5A00
# T10/S0 -> 0x5B00, ..., S6 -> 0x6100
def ram_addr(t, s, offset):
    base = 0x5100
    sector_num = (t - 9) * 10 + s
    return base + sector_num * 256 + offset

print('=== Searching T09/T10 for 3E FA = LD A,0xFA (error code) ===')
for t in [9, 10]:
    for s in range(10):
        off = sector_offset(t, s)
        data = disk[off:off+BPS]
        for i in range(len(data)-1):
            if data[i] == 0x3E and data[i+1] == 0xFA:
                ra = ram_addr(t, s, i)
                dump_around(data, i, f'T{t:02d}/S{s} off=0x{i:02X} RAM=0x{ra:04X} "LD A,0xFA"')

print()
print('=== What is at 0x441E? (T09/T10 sequential, 0x441E = T09/S3+offset 0x1E = 0x3A1E+0x1E) ===')
# 0x441E = 0x441E - 0x5100 = -0xCE2 (before T09 start) -- WRONG! 0x441E < 0x5100
# So 0x441E is NOT in the T09/T10 sequential load area.
# It must come from scatter or from a different source.
addr_441e_offset = 0x441E - 0x5100
print(f'  0x441E - 0x5100 = {addr_441e_offset} (negative, so 0x441E is BELOW T09 load area 0x5100+)')
print(f'  0x441E is in region 0x4000-0x50FF which must be filled by SCATTER from T09/T10')

print()
print('=== T10/S3 context around offset 0x8F ===')
off = sector_offset(10, 3)
data = disk[off:off+BPS]
print('  Bytes at 0x80-0xA0:')
chunk = data[0x80:0xA8]
print('  ' + ' '.join(f'{b:02X}' for b in chunk))
# Try to identify what instruction precedes 0x8B
print(f'  (offset 0x8B = LD HL,0x3F17; offset 0x8F = LD DE,0x50B6)')
print(f'  RAM address: T10/S3[0x8B] = 0x5{(3*256+0x8B):04X} ... but wait, T10 loads to T09+10 sectors ahead')

# Actually: T09 has sectors 0-9 (10 sectors), T10 has S0-S9 (10 sectors)
# T09/S0 -> 0x5100, ..., T09/S9 -> 0x5A00
# T10/S0 -> 0x5B00, T10/S3 -> 0x5E00
# T10/S3[0x8B] -> 0x5E8B
print(f'  T10/S3[0x8B] -> RAM 0x{0x5B00 + 3*256 + 0x8B:04X}')

print()
print('=== Search for 0x5084 reference = 84 50 in T09/T10 ===')
for t in [9, 10]:
    for s in range(10):
        off = sector_offset(t, s)
        data = disk[off:off+BPS]
        for i in range(len(data)-1):
            if data[i] == 0x84 and data[i+1] == 0x50:
                ra = ram_addr(t, s, i)
                dump_around(data, i, f'T{t:02d}/S{s} off=0x{i:02X} RAM≈0x{ra:04X} "84 50"')

print()
print('=== Search for CALL 0x5084 = CD 84 50 in T09/T10 ===')
for t in [9, 10]:
    for s in range(10):
        off = sector_offset(t, s)
        data = disk[off:off+BPS]
        for i in range(len(data)-2):
            if data[i] == 0xCD and data[i+1] == 0x84 and data[i+2] == 0x50:
                ra = ram_addr(t, s, i)
                dump_around(data, i, f'T{t:02d}/S{s} off=0x{i:02X} RAM≈0x{ra:04X} "CALL 0x5084"')

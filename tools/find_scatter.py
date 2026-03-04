#!/usr/bin/env python3
"""Find T09/T10 scatter destinations by matching known RAM content."""
import sys, os, struct
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

disk = open('disks/ld1-531.dsk', 'rb').read()
SPT = 10
BPS = 256

def sector_offset(t, s):
    return (t * SPT + s) * BPS

# Known RAM content at known addresses - from [LDOS@4BE4] dump:
# RAM[0x4BE4..0x4BF8]: CD 00 00 F5 3E 00 32 15 43 F1 C9 3D 00 20 58 3C C9 E5 67 78 32
known_ram = {
    0x4BE4: bytes([0xCD, 0x00, 0x00, 0xF5, 0x3E, 0x00, 0x32, 0x15, 0x43, 0xF1, 0xC9]),
}

# From dump T10/S4[0x3F] = 22 20 40 -> RAM 0x4F53
# T10/S4 scatter base = 0x4F53 - 0x3F = 0x4F14
# T10/S4 covers RAM 0x4F14 to 0x5013

# Search ALL T09/T10 sectors for the known RAM bytes
print('=== Finding scatter destinations for each T09/T10 sector ===')
print('Strategy: search for unique known-RAM byte patterns in sector data')
print()

# T10/S4 is confirmed at 0x4F14. Let me find others.
# Known RAM bytes at specific locations -> deduce sector scatter base

# From 4BEE code: CD 00 00 F5 3E 00 32 15 43 F1 C9
target_4be4 = bytes([0xCD, 0x00, 0x00, 0xF5, 0x3E, 0x00, 0x32, 0x15, 0x43, 0xF1, 0xC9])
# From 4CDB: the copy loop. Let me put some nearby distinct bytes
# From [LDOS@4F53] dump RAM[0x4F48]: "21 48 40 7E E6 F1 B1 77 21 17 3F 22 20 40"
target_4f48 = bytes([0x21, 0x48, 0x40, 0x7E, 0xE6, 0xF1, 0xB1, 0x77, 0x21, 0x17, 0x3F, 0x22, 0x20, 0x40])

targets = [
    (0x4BE4, target_4be4, 'RAM 0x4BE4: CD 00 00 F5 3E 00 32 15 43'),
    (0x4F48, target_4f48, 'RAM 0x4F48: 21 48 40 7E E6 F1 B1 77...'),
]

for ram_addr, target_bytes, desc in targets:
    print(f'Looking for: {desc}')
    found = False
    for t in [9, 10]:
        for s in range(10):
            off = sector_offset(t, s)
            data = disk[off:off+BPS]
            for i in range(len(data) - len(target_bytes) + 1):
                if data[i:i+len(target_bytes)] == target_bytes:
                    scatter_base = ram_addr - i
                    print(f'  FOUND in T{t:02d}/S{s} at offset 0x{i:02X}')
                    print(f'  Scatter base: RAM 0x{scatter_base:04X} (T{t:02d}/S{s}[0] -> 0x{scatter_base:04X})')
                    print(f'  Sector covers: RAM 0x{scatter_base:04X}-0x{scatter_base+255:04X}')
                    # What's at 0x5084 relative to this sector?
                    if scatter_base <= 0x5084 <= scatter_base + 255:
                        off5084 = 0x5084 - scatter_base
                        d = data[off5084:off5084+16]
                        print(f'  -> 0x5084 is at offset 0x{off5084:02X} in this sector!')
                        print(f'  -> bytes at 0x5084: {" ".join(f"{x:02X}" for x in d)}')
                    found = True
    if not found:
        print('  NOT FOUND in T09/T10')
    print()

print()
print('=== Finding which sector contains 0x5084 ===')
# Let us search for LD A,0xFA (3E FA) in any sector that scatter-loads near 0x5084
# If 0x5084 has some known instruction... we need to be cleverer.
# From CALL 0x5084 at 0x4F59: it's called from the module installer.
# The function at 0x5084 probably starts at 0x5084.
# What are the first bytes of 0x5084?
# From T10/S5 offset 0xBD: [0D C8 18 DB 01 7E 84 50 05 7D 80 80 80 6F 06 03 7E 23]
# 7E 84 50 is NOT a CALL 0x5084 -- 7E is LD A,(HL) and 84 50 are next two bytes (LD H or ADD H, LD D?)
# Looking for the actual function code: ADD HL byte patterns
# Let me search for 3E FA in all tracks
print('=== 3E FA (LD A,0xFA) in ALL tracks: ===')
for t in range(35):
    if t == 17: continue
    for s in range(10):
        off = sector_offset(t, s)
        data = disk[off:off+BPS]
        for i in range(len(data)-1):
            if data[i] == 0x3E and data[i+1] == 0xFA:
                ctx = ' '.join(f'{x:02X}' for x in data[max(0,i-4):i+8])
                print(f'  T{t:02d}/S{s} offset 0x{i:02X}: [{ctx}]')

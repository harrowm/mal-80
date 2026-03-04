#!/usr/bin/env python3
"""Find LDOS overlay placement variable in boot sectors."""

disk = open('/Users/malcolm/mal-80/disks/ld1-531.dsk', 'rb').read()
SPT = 10
BPS = 256

def sector(t, s): return disk[(t*SPT+s)*BPS:(t*SPT+s+1)*BPS]

# Examine boot sector T00/S0
boot = sector(0, 0)
print("=== BOOT SECTOR T00/S0 (full 256 bytes) ===")
for i in range(0, 256, 16):
    h = ' '.join(f'{b:02X}' for b in boot[i:i+16])
    a = ''.join(chr(b) if 32<=b<127 else '.' for b in boot[i:i+16])
    print(f'  {i:04X}: {h}  {a}')

# Find any reference to 0x50xx addresses in the boot sector
print()
print("=== Refs to 0x50xx in boot sector ===")
for i in range(len(boot)-1):
    v = boot[i] | (boot[i+1]<<8)  # little-endian word
    if 0x5000 <= v <= 0x5FFF:
        ctx = boot[max(0,i-2):i+4]
        print(f'  offset {i:02X}: word=0x{v:04X}  ctx={ctx.hex()}')

# Also check SYS0 header sectors
print()
print("=== SYS0 T09/S0 (full first sector) ===")
sys0 = sector(9, 0)
for i in range(0, 256, 16):
    h = ' '.join(f'{b:02X}' for b in sys0[i:i+16])
    a = ''.join(chr(b) if 32<=b<127 else '.' for b in sys0[i:i+16])
    print(f'  {i:04X}: {h}  {a}')

# Find refs to 0x50xx in all of track 9
print()
print("=== Refs to 0x50xx in track 9 ===")
t9 = disk[9*SPT*BPS:(9+1)*SPT*BPS]
for i in range(len(t9)-1):
    v = t9[i] | (t9[i+1]<<8)
    if 0x5000 <= v <= 0x5FFF:
        sec = i // BPS
        off = i % BPS
        ctx = t9[max(0,i-2):i+4]
        print(f'  T09/S{sec} off={off:02X}: word=0x{v:04X}  ctx={ctx.hex()}')

# Write watchpoint data: look at all writes to 0x4040-0x406F range
# by scanning SYS0 for initializer patterns
print()
print("=== Refs to 0x40xx system area in boot sector (looking for mem-top writes) ===")
for i in range(len(boot)-1):
    v = boot[i] | (boot[i+1]<<8)
    if 0x4040 <= v <= 0x506F:
        ctx = boot[max(0,i-2):i+4]
        print(f'  boot off={i:02X}: word=0x{v:04X}  ctx={ctx.hex()}')

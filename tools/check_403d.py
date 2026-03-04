#!/usr/bin/env python3
rom = open('roms/level2.rom','rb').read()
# LDIR: ROM 0x06D2 -> RAM 0x4000, BC=0x0036 (54 bytes)
base = 0x06D2
print("ROM init block (LDIR to 0x4000):")
for offset in range(0x36):
    addr = 0x4000 + offset
    val = rom[base + offset]
    marker = ' <-- 0x403D expanded-mem BYTE' if addr == 0x403D else ''
    if addr >= 0x403A or addr <= 0x4003:
        print(f'  ROM[{base+offset:04X}] -> RAM[{addr:04X}] = 0x{val:02X}{marker}')

# Also dump ROM at 0x0221 context
print("\nROM 0x0210-0x022C bytes:")
for i in range(0x210, 0x22C):
    print(f'  ROM[{i:04X}] = 0x{rom[i]:02X}')

# Look for memory test code that sets bit 3 of 0x403D
# AND H is 0xA4, OR L is 0xB5
# The pattern CALL 0x0221 with H=?? L=0x08 would set bit 3
print("\nSearching ROM for CALL 0x0221 patterns:")
import struct
for i in range(len(rom)-5):
    if rom[i:i+3] == bytes([0xCD, 0x21, 0x02]):
        # Found CALL 0x0221, look at what HL was set to before
        ctx_start = max(0, i-8)
        ctx = rom[ctx_start:i+3]
        print(f'  {i:04X}: CALL 0x0221   context: ' + ' '.join(f'{b:02X}' for b in ctx))

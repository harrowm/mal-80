#!/usr/bin/env python3
"""
Disassemble SYS12 code captured from RAM at 0x4E00.
Data from [SYS12] RAM dump in emulator log.
"""

# Raw bytes of 0x4E00-0x4EFF from emulator log
raw = bytes([
    0xF3,0xED,0x56,0x31,0xDE,0x41,0x21,0xFF,0xFF,0x7E,0x47,0x2F,0x77,0xBE,0x70,0x28,  # 4E00
    0x06,0x7C,0xD6,0x40,0x67,0x18,0xF2,0x22,0x49,0x40,0x22,0x03,0x44,0x3E,0x00,0xD3,  # 4E10
    0xFE,0xAF,0x32,0xE4,0x37,0x2A,0x16,0x40,0x22,0xB8,0x43,0x21,0x00,0x3C,0x2F,0x77,  # 4E20
    0xBE,0x36,0x20,0x2A,0x1E,0x40,0x20,0x06,0x21,0x78,0x43,0x22,0x1E,0x40,0x22,0xBA,  # 4E30
    0x43,0x2A,0x26,0x40,0x22,0xBC,0x43,0x21,0xD8,0x43,0x36,0x00,0x2C,0x20,0xFB,0xFD,  # 4E40
    0xE5,0xE1,0x7C,0xB5,0x28,0x10,0x11,0x00,0x47,0x01,0x0A,0x00,0xED,0xB0,0x21,0x2F,  # 4E50
    0x44,0xCB,0xC6,0x2E,0x10,0x01,0x2E,0xBC,0x26,0x42,0x7E,0xE6,0x03,0x47,0x21,0x03,  # 4E60
    0x47,0x7E,0xE6,0x7C,0xB0,0x77,0x3A,0x02,0x42,0x32,0x09,0x47,0x3A,0xEC,0x37,0x21,  # 4E70
    0x4C,0x40,0x36,0xC0,0xFB,0x11,0x02,0x00,0x4A,0x21,0x00,0x42,0xCD,0x77,0x47,0xC0,  # 4E80
    0x2E,0x01,0x7E,0x36,0x00,0xF5,0x3A,0xC2,0x42,0xB7,0xC2,0x89,0x4F,0x3A,0x06,0x43,  # 4E90
    0xEE,0x50,0xCA,0xBD,0x4F,0xFE,0x0D,0x38,0x4B,0x21,0x17,0x3F,0x11,0xB6,0x50,0x01,  # 4EA0
    0x30,0x08,0xCD,0x1B,0x43,0x30,0xF2,0x1A,0xFE,0x0C,0x30,0x02,0xC6,0x64,0xD6,0x50,  # 4EB0
    0xFE,0x20,0x30,0xE5,0x32,0x66,0x44,0xE6,0x03,0x3E,0x1C,0x20,0x06,0x21,0x48,0x40,  # 4EC0
    0xCB,0xFE,0x3C,0x21,0x03,0x42,0x77,0x3A,0x1A,0x43,0x3D,0xFE,0x0C,0x30,0xCA,0x2B,  # 4ED0
    0x85,0x6F,0x13,0x1A,0x32,0x07,0x43,0x3D,0xBE,0x30,0xBE,0x13,0x1A,0xF6,0x50,0x32,  # 4EE0
    0x06,0x43,0xEE,0x50,0x32,0x46,0x40,0x47,0x3A,0x66,0x44,0xF5,0xE6,0x03,0x21,0x03,  # 4EF0
])

BASE = 0x4E00

def disasm(data, base):
    """Simple Z80 disassembler for key opcodes."""
    i = 0
    while i < len(data):
        addr = base + i
        b = data[i]
        
        # Two-byte prefix opcodes
        if b == 0xCB or b == 0xDD or b == 0xFD or b == 0xED:
            if i + 1 >= len(data):
                print(f"  {addr:04X}: {b:02X}           PREFIX {b:02X}")
                i += 1
                continue
            b2 = data[i+1]
            if b == 0xED:
                if b2 == 0x56:
                    print(f"  {addr:04X}: ED 56        IM 1")
                    i += 2; continue
                elif b2 == 0xB0:
                    print(f"  {addr:04X}: ED B0        LDIR")
                    i += 2; continue
                else:
                    print(f"  {addr:04X}: ED {b2:02X}       ED prefix")
                    i += 2; continue
            elif b == 0xCB:
                ops = {0xC6:'SET 0,(HL)', 0xFE:'SET 7,(HL)'}
                print(f"  {addr:04X}: CB {b2:02X}       {ops.get(b2, f'CB {b2:02X}')}")
                i += 2; continue
            elif b == 0xFD:
                if b2 == 0xE5:
                    print(f"  {addr:04X}: FD E5        PUSH IY")
                    i += 2; continue
                elif b2 == 0xE1:
                    print(f"  {addr:04X}: FD E1        POP IY")
                    i += 2; continue
                else:
                    print(f"  {addr:04X}: FD {b2:02X}       FD prefix")
                    i += 2; continue
            elif b == 0xDD:
                print(f"  {addr:04X}: DD {b2:02X}       DD prefix")
                i += 2; continue

        # 3-byte instructions
        elif b == 0x31:  # LD SP,nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 31 {data[i+1]:02X} {data[i+2]:02X}  LD SP,0x{nn:04X}")
            i += 3; continue
        elif b == 0x21:  # LD HL,nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 21 {data[i+1]:02X} {data[i+2]:02X}  LD HL,0x{nn:04X}")
            i += 3; continue
        elif b == 0x11:  # LD DE,nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 11 {data[i+1]:02X} {data[i+2]:02X}  LD DE,0x{nn:04X}")
            i += 3; continue
        elif b == 0x01:  # LD BC,nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 01 {data[i+1]:02X} {data[i+2]:02X}  LD BC,0x{nn:04X}")
            i += 3; continue
        elif b == 0x22:  # LD (nn),HL
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 22 {data[i+1]:02X} {data[i+2]:02X}  LD (0x{nn:04X}),HL")
            i += 3; continue
        elif b == 0x2A:  # LD HL,(nn)
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 2A {data[i+1]:02X} {data[i+2]:02X}  LD HL,(0x{nn:04X})")
            i += 3; continue
        elif b == 0x3A:  # LD A,(nn)
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 3A {data[i+1]:02X} {data[i+2]:02X}  LD A,(0x{nn:04X})")
            i += 3; continue
        elif b == 0x32:  # LD (nn),A
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: 32 {data[i+1]:02X} {data[i+2]:02X}  LD (0x{nn:04X}),A")
            i += 3; continue
        elif b == 0xCD:  # CALL nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: CD {data[i+1]:02X} {data[i+2]:02X}  CALL 0x{nn:04X}")
            i += 3; continue
        elif b == 0xC2:  # JP NZ,nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: C2 {data[i+1]:02X} {data[i+2]:02X}  JP NZ,0x{nn:04X}")
            i += 3; continue
        elif b == 0xCA:  # JP Z,nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: CA {data[i+1]:02X} {data[i+2]:02X}  JP Z,0x{nn:04X}")
            i += 3; continue
        elif b == 0xC3:  # JP nn
            nn = data[i+1] | (data[i+2] << 8)
            print(f"  {addr:04X}: C3 {data[i+1]:02X} {data[i+2]:02X}  JP 0x{nn:04X}")
            i += 3; continue
        elif b == 0xD3:  # OUT (n),A
            print(f"  {addr:04X}: D3 {data[i+1]:02X}       OUT (0x{data[i+1]:02X}),A")
            i += 2; continue
        elif b == 0x3E:  # LD A,n
            print(f"  {addr:04X}: 3E {data[i+1]:02X}       LD A,0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0x26:  # LD H,n
            print(f"  {addr:04X}: 26 {data[i+1]:02X}       LD H,0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0x2E:  # LD L,n
            print(f"  {addr:04X}: 2E {data[i+1]:02X}       LD L,0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0x18:  # JR e
            disp = data[i+1]
            if disp >= 128: disp -= 256
            target = addr + 2 + disp
            print(f"  {addr:04X}: 18 {data[i+1]:02X}       JR 0x{target:04X}  ({disp:+d})")
            i += 2; continue
        elif b == 0x28:  # JR Z,e
            disp = data[i+1]
            if disp >= 128: disp -= 256
            target = addr + 2 + disp
            print(f"  {addr:04X}: 28 {data[i+1]:02X}       JR Z,0x{target:04X}  ({disp:+d})")
            i += 2; continue
        elif b == 0x20:  # JR NZ,e
            disp = data[i+1]
            if disp >= 128: disp -= 256
            target = addr + 2 + disp
            print(f"  {addr:04X}: 20 {data[i+1]:02X}       JR NZ,0x{target:04X}  ({disp:+d})")
            i += 2; continue
        elif b == 0x30:  # JR NC,e
            disp = data[i+1]
            if disp >= 128: disp -= 256
            target = addr + 2 + disp
            print(f"  {addr:04X}: 30 {data[i+1]:02X}       JR NC,0x{target:04X}  ({disp:+d})")
            i += 2; continue
        elif b == 0x38:  # JR C,e
            disp = data[i+1]
            if disp >= 128: disp -= 256
            target = addr + 2 + disp
            print(f"  {addr:04X}: 38 {data[i+1]:02X}       JR C,0x{target:04X}  ({disp:+d})")
            i += 2; continue
        elif b == 0xE6:  # AND n
            print(f"  {addr:04X}: E6 {data[i+1]:02X}       AND 0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0xD6:  # SUB n
            print(f"  {addr:04X}: D6 {data[i+1]:02X}       SUB 0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0xFE:  # CP n
            print(f"  {addr:04X}: FE {data[i+1]:02X}       CP 0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0xEE:  # XOR n
            print(f"  {addr:04X}: EE {data[i+1]:02X}       XOR 0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0xF6:  # OR n
            print(f"  {addr:04X}: F6 {data[i+1]:02X}       OR 0x{data[i+1]:02X}")
            i += 2; continue
        elif b == 0x36:  # LD (HL),n
            print(f"  {addr:04X}: 36 {data[i+1]:02X}       LD (HL),0x{data[i+1]:02X}")
            i += 2; continue

        # 1-byte opcodes
        simple = {
            0xF3: 'DI', 0xFB: 'EI', 0x76: 'HALT', 0xC9: 'RET',
            0x7E: 'LD A,(HL)', 0x77: 'LD (HL),A',
            0x47: 'LD B,A', 0x4F: 'LD C,A', 0x57: 'LD D,A', 0x5F: 'LD E,A',
            0x67: 'LD H,A', 0x6F: 'LD L,A',
            0x78: 'LD A,B', 0x79: 'LD A,C', 0x7A: 'LD A,D', 0x7B: 'LD A,E',
            0x7C: 'LD A,H', 0x7D: 'LD A,L',
            0x2F: 'CPL', 0xAF: 'XOR A', 0xB7: 'OR A', 0xB5: 'OR L',
            0xA7: 'AND A', 0xB0: 'OR B',
            0x23: 'INC HL', 0x2C: 'INC L', 0x3C: 'INC A', 0x3D: 'DEC A',
            0x13: 'INC DE', 0x0B: 'DEC BC',
            0xF5: 'PUSH AF', 0xF1: 'POP AF', 0xE5: 'PUSH HL', 0xE1: 'POP HL',
            0xC5: 'PUSH BC', 0xC1: 'POP BC', 0xD5: 'PUSH DE', 0xD1: 'POP DE',
            0xBE: 'CP (HL)', 0x1A: 'LD A,(DE)', 0x0A: 'LD A,(BC)',
            0xEB: 'EX DE,HL', 0xC0: 'RET NZ', 0xC8: 'RET Z',
            0xE9: 'JP (HL)',
        }
        if b in simple:
            print(f"  {addr:04X}: {b:02X}           {simple[b]}")
            i += 1; continue

        # Fallback
        print(f"  {addr:04X}: {b:02X}           ???  0x{b:02X}")
        i += 1

print("=== SYS12 disassembly (0x4E00-0x4EFF) ===")
disasm(raw, BASE)

#!/usr/bin/env python3
import struct, sys

rom = open('roms/level2.rom', 'rb').read()

def disasm(start, end):
    i = start
    while i < end:
        b = rom[i]
        if   b == 0x7C: print(f'  {i:04X}: LD A,H'); i+=1
        elif b == 0x7D: print(f'  {i:04X}: LD A,L'); i+=1
        elif b == 0xE6: print(f'  {i:04X}: AND 0x{rom[i+1]:02X}'); i+=2
        elif b == 0xF6: print(f'  {i:04X}: OR  0x{rom[i+1]:02X}'); i+=2
        elif b == 0xA0: print(f'  {i:04X}: AND B'); i+=1
        elif b == 0x67: print(f'  {i:04X}: LD H,A'); i+=1
        elif b == 0x6F: print(f'  {i:04X}: LD L,A'); i+=1
        elif b == 0xC9: print(f'  {i:04X}: RET'); i+=1
        elif b == 0xFE: print(f'  {i:04X}: CP  0x{rom[i+1]:02X}'); i+=2
        elif b == 0xD2: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: JP NC,  0x{a:04X}'); i+=3
        elif b == 0xDA: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: JP C,   0x{a:04X}'); i+=3
        elif b == 0xC2: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: JP NZ,  0x{a:04X}'); i+=3
        elif b == 0xCA: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: JP Z,   0x{a:04X}'); i+=3
        elif b == 0xCD: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: CALL    0x{a:04X}'); i+=3
        elif b == 0xC3: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: JP      0x{a:04X}'); i+=3
        elif b == 0x3A: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD A,(0x{a:04X})'); i+=3
        elif b == 0x3E: print(f'  {i:04X}: LD A,0x{rom[i+1]:02X}'); i+=2
        elif b == 0x4F: print(f'  {i:04X}: LD C,A'); i+=1
        elif b == 0x47: print(f'  {i:04X}: LD B,A'); i+=1
        elif b == 0x57: print(f'  {i:04X}: LD D,A'); i+=1
        elif b == 0x5F: print(f'  {i:04X}: LD E,A'); i+=1
        elif b == 0xB9: print(f'  {i:04X}: CP C'); i+=1
        elif b == 0x18: off=rom[i+1]; dst=(i+2+(off if off<128 else off-256))&0xFFFF; print(f'  {i:04X}: JR      0x{dst:04X}  (off={off:02X})'); i+=2
        elif b == 0x28: off=rom[i+1]; dst=(i+2+(off if off<128 else off-256))&0xFFFF; print(f'  {i:04X}: JR Z,   0x{dst:04X}  (off={off:02X})'); i+=2
        elif b == 0x20: off=rom[i+1]; dst=(i+2+(off if off<128 else off-256))&0xFFFF; print(f'  {i:04X}: JR NZ,  0x{dst:04X}  (off={off:02X})'); i+=2
        elif b == 0x38: off=rom[i+1]; dst=(i+2+(off if off<128 else off-256))&0xFFFF; print(f'  {i:04X}: JR C,   0x{dst:04X}  (off={off:02X})'); i+=2
        elif b == 0x30: off=rom[i+1]; dst=(i+2+(off if off<128 else off-256))&0xFFFF; print(f'  {i:04X}: JR NC,  0x{dst:04X}  (off={off:02X})'); i+=2
        elif b == 0x21: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD HL,0x{a:04X}'); i+=3
        elif b == 0x22: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD (0x{a:04X}),HL'); i+=3
        elif b == 0x2A: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD HL,(0x{a:04X})'); i+=3
        elif b == 0x36: print(f'  {i:04X}: LD (HL),0x{rom[i+1]:02X}'); i+=2
        elif b == 0x77: print(f'  {i:04X}: LD (HL),A'); i+=1
        elif b == 0x7E: print(f'  {i:04X}: LD A,(HL)'); i+=1
        elif b == 0xBE: print(f'  {i:04X}: CP (HL)'); i+=1
        elif b == 0x46: print(f'  {i:04X}: LD B,(HL)'); i+=1
        elif b == 0x4E: print(f'  {i:04X}: LD C,(HL)'); i+=1
        elif b == 0x56: print(f'  {i:04X}: LD D,(HL)'); i+=1
        elif b == 0x5E: print(f'  {i:04X}: LD E,(HL)'); i+=1
        elif b == 0x23: print(f'  {i:04X}: INC HL'); i+=1
        elif b == 0x2B: print(f'  {i:04X}: DEC HL'); i+=1
        elif b == 0x03: print(f'  {i:04X}: INC BC'); i+=1
        elif b == 0x13: print(f'  {i:04X}: INC DE'); i+=1
        elif b == 0x2C: print(f'  {i:04X}: INC L'); i+=1
        elif b == 0x79: print(f'  {i:04X}: LD A,C'); i+=1
        elif b == 0x78: print(f'  {i:04X}: LD A,B'); i+=1
        elif b == 0x7A: print(f'  {i:04X}: LD A,D'); i+=1
        elif b == 0x7B: print(f'  {i:04X}: LD A,E'); i+=1
        elif b == 0x01: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD BC,0x{a:04X}'); i+=3
        elif b == 0x11: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD DE,0x{a:04X}'); i+=3
        elif b == 0x31: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD SP,0x{a:04X}'); i+=3
        elif b == 0x32: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: LD (0x{a:04X}),A'); i+=3
        elif b == 0xB1: print(f'  {i:04X}: OR C'); i+=1
        elif b == 0xB3: print(f'  {i:04X}: OR E'); i+=1
        elif b == 0xB7: print(f'  {i:04X}: OR A'); i+=1
        elif b == 0xAF: print(f'  {i:04X}: XOR A'); i+=1
        elif b == 0xA7: print(f'  {i:04X}: AND A'); i+=1
        elif b == 0xEB: print(f'  {i:04X}: EX DE,HL'); i+=1
        elif b == 0x09: print(f'  {i:04X}: ADD HL,BC'); i+=1
        elif b == 0x19: print(f'  {i:04X}: ADD HL,DE'); i+=1
        elif b == 0x29: print(f'  {i:04X}: ADD HL,HL'); i+=1
        elif b == 0xC0: print(f'  {i:04X}: RET NZ'); i+=1
        elif b == 0xC8: print(f'  {i:04X}: RET Z'); i+=1
        elif b == 0xD0: print(f'  {i:04X}: RET NC'); i+=1
        elif b == 0xD8: print(f'  {i:04X}: RET C'); i+=1
        elif b == 0xF5: print(f'  {i:04X}: PUSH AF'); i+=1
        elif b == 0xC5: print(f'  {i:04X}: PUSH BC'); i+=1
        elif b == 0xD5: print(f'  {i:04X}: PUSH DE'); i+=1
        elif b == 0xE5: print(f'  {i:04X}: PUSH HL'); i+=1
        elif b == 0xF1: print(f'  {i:04X}: POP AF'); i+=1
        elif b == 0xC1: print(f'  {i:04X}: POP BC'); i+=1
        elif b == 0xD1: print(f'  {i:04X}: POP DE'); i+=1
        elif b == 0xE1: print(f'  {i:04X}: POP HL'); i+=1
        elif b == 0x0E: print(f'  {i:04X}: LD C,0x{rom[i+1]:02X}'); i+=2
        elif b == 0x06: print(f'  {i:04X}: LD B,0x{rom[i+1]:02X}'); i+=2
        elif b == 0x0C: print(f'  {i:04X}: INC C'); i+=1
        elif b == 0x04: print(f'  {i:04X}: INC B'); i+=1
        elif b == 0x16: print(f'  {i:04X}: LD D,0x{rom[i+1]:02X}'); i+=2
        elif b == 0x1E: print(f'  {i:04X}: LD E,0x{rom[i+1]:02X}'); i+=2
        elif b == 0xC4: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: CALL NZ, 0x{a:04X}'); i+=3
        elif b == 0xCC: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: CALL Z,  0x{a:04X}'); i+=3
        elif b == 0xD4: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: CALL NC, 0x{a:04X}'); i+=3
        elif b == 0xDC: a=struct.unpack_from('<H',rom,i+1)[0]; print(f'  {i:04X}: CALL C,  0x{a:04X}'); i+=3
        elif b == 0xE9: print(f'  {i:04X}: JP (HL)'); i+=1
        elif b == 0xDD:
            b2 = rom[i+1]
            if   b2 == 0x21: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD IX,0x{a:04X}'); i+=4
            elif b2 == 0x36: off=rom[i+2]; v=rom[i+3]; print(f'  {i:04X}: LD (IX+{off}),0x{v:02X}'); i+=4
            elif b2 == 0x46: off=rom[i+2]; print(f'  {i:04X}: LD B,(IX+{off})'); i+=3
            elif b2 == 0x4E: off=rom[i+2]; print(f'  {i:04X}: LD C,(IX+{off})'); i+=3
            elif b2 == 0x56: off=rom[i+2]; print(f'  {i:04X}: LD D,(IX+{off})'); i+=3
            elif b2 == 0x5E: off=rom[i+2]; print(f'  {i:04X}: LD E,(IX+{off})'); i+=3
            elif b2 == 0x66: off=rom[i+2]; print(f'  {i:04X}: LD H,(IX+{off})'); i+=3
            elif b2 == 0x6E: off=rom[i+2]; print(f'  {i:04X}: LD L,(IX+{off})'); i+=3
            elif b2 == 0x77: off=rom[i+2]; print(f'  {i:04X}: LD (IX+{off}),A'); i+=3
            elif b2 == 0x7E: off=rom[i+2]; print(f'  {i:04X}: LD A,(IX+{off})'); i+=3
            elif b2 == 0x09: print(f'  {i:04X}: ADD IX,BC'); i+=2
            elif b2 == 0x19: print(f'  {i:04X}: ADD IX,DE'); i+=2
            elif b2 == 0x29: print(f'  {i:04X}: ADD IX,HL'); i+=2  # wrong but rare
            elif b2 == 0x23: print(f'  {i:04X}: INC IX'); i+=2
            elif b2 == 0x2B: print(f'  {i:04X}: DEC IX'); i+=2
            elif b2 == 0xE1: print(f'  {i:04X}: POP IX'); i+=2
            elif b2 == 0xE5: print(f'  {i:04X}: PUSH IX'); i+=2
            elif b2 == 0xE9: print(f'  {i:04X}: JP (IX)'); i+=2
            elif b2 == 0x86: off=rom[i+2]; print(f'  {i:04X}: ADD A,(IX+{off})'); i+=3
            elif b2 == 0xBE: off=rom[i+2]; print(f'  {i:04X}: CP (IX+{off})'); i+=3
            elif b2 == 0xB6: off=rom[i+2]; print(f'  {i:04X}: OR (IX+{off})'); i+=3
            elif b2 == 0xA6: off=rom[i+2]; print(f'  {i:04X}: AND (IX+{off})'); i+=3
            elif b2 == 0x22: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD (0x{a:04X}),IX'); i+=4
            elif b2 == 0x2A: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD IX,(0x{a:04X})'); i+=4
            else: print(f'  {i:04X}: DD {b2:02X} ...  (DD ext)'); i+=2
        elif b == 0xED:
            b2 = rom[i+1]
            if   b2 == 0xB0: print(f'  {i:04X}: LDIR'); i+=2
            elif b2 == 0xB8: print(f'  {i:04X}: LDDR'); i+=2
            elif b2 == 0x43: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD (0x{a:04X}),BC'); i+=4
            elif b2 == 0x4B: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD BC,(0x{a:04X})'); i+=4
            elif b2 == 0x53: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD (0x{a:04X}),DE'); i+=4
            elif b2 == 0x5B: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD DE,(0x{a:04X})'); i+=4
            elif b2 == 0x73: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD (0x{a:04X}),SP'); i+=4
            elif b2 == 0x7B: a=struct.unpack_from('<H',rom,i+2)[0]; print(f'  {i:04X}: LD SP,(0x{a:04X})'); i+=4
            elif b2 == 0x56: print(f'  {i:04X}: IM 1'); i+=2
            elif b2 == 0x5E: print(f'  {i:04X}: IM 2'); i+=2
            elif b2 == 0x46: print(f'  {i:04X}: IM 0'); i+=2
            elif b2 == 0x47: print(f'  {i:04X}: LD I,A'); i+=2
            elif b2 == 0x57: print(f'  {i:04X}: LD A,I'); i+=2
            elif b2 == 0x4D: print(f'  {i:04X}: RETI'); i+=2
            elif b2 == 0x45: print(f'  {i:04X}: RETN'); i+=2
            else: print(f'  {i:04X}: ED {b2:02X}  (ED ext)'); i+=2
        elif b == 0x10: off=rom[i+1]; dst=(i+2+(off if off<128 else off-256))&0xFFFF; print(f'  {i:04X}: DJNZ    0x{dst:04X}  (off={off:02X})'); i+=2
        elif b == 0xFB: print(f'  {i:04X}: EI'); i+=1
        elif b == 0xF3: print(f'  {i:04X}: DI'); i+=1
        elif b == 0x76: print(f'  {i:04X}: HALT'); i+=1
        elif b == 0xDB: print(f'  {i:04X}: IN A,(0x{rom[i+1]:02X})'); i+=2
        elif b == 0xD3: print(f'  {i:04X}: OUT (0x{rom[i+1]:02X}),A'); i+=2
        elif b == 0xE3: print(f'  {i:04X}: EX (SP),HL'); i+=1
        elif b == 0xF9: print(f'  {i:04X}: LD SP,HL'); i+=1
        elif b == 0xCF: print(f'  {i:04X}: RST 08'); i+=1
        elif b == 0xD7: print(f'  {i:04X}: RST 10'); i+=1
        elif b == 0xDF: print(f'  {i:04X}: RST 18'); i+=1
        elif b == 0xE7: print(f'  {i:04X}: RST 20'); i+=1
        elif b == 0xEF: print(f'  {i:04X}: RST 28'); i+=1
        elif b == 0xFF: print(f'  {i:04X}: RST 38'); i+=1
        elif b == 0x00: print(f'  {i:04X}: NOP'); i+=1
        elif b == 0x08: print(f'  {i:04X}: EX AF,AF'); i+=1
        elif b == 0xD9: print(f'  {i:04X}: EXX'); i+=1
        elif b == 0x37: print(f'  {i:04X}: SCF'); i+=1
        elif b == 0x3F: print(f'  {i:04X}: CCF'); i+=1
        elif b == 0x2F: print(f'  {i:04X}: CPL'); i+=1
        elif b == 0x3D: print(f'  {i:04X}: DEC A'); i+=1
        elif b == 0x3C: print(f'  {i:04X}: INC A'); i+=1
        elif b == 0x05: print(f'  {i:04X}: DEC B'); i+=1
        elif b == 0x0D: print(f'  {i:04X}: DEC C'); i+=1
        elif b == 0x2D: print(f'  {i:04X}: DEC L'); i+=1
        elif b == 0x25: print(f'  {i:04X}: DEC H'); i+=1
        elif b == 0x15: print(f'  {i:04X}: DEC D'); i+=1
        elif b == 0x1D: print(f'  {i:04X}: DEC E'); i+=1
        elif b == 0x0B: print(f'  {i:04X}: DEC BC'); i+=1
        elif b == 0x1B: print(f'  {i:04X}: DEC DE'); i+=1
        elif b == 0x2B: print(f'  {i:04X}: DEC HL'); i+=1
        elif b == 0x3B: print(f'  {i:04X}: DEC SP'); i+=1
        elif b == 0x17: print(f'  {i:04X}: RLA'); i+=1
        elif b == 0x07: print(f'  {i:04X}: RLCA'); i+=1
        elif b == 0x1F: print(f'  {i:04X}: RRA'); i+=1
        elif b == 0x0F: print(f'  {i:04X}: RRCA'); i+=1
        elif b == 0x80: print(f'  {i:04X}: ADD A,B'); i+=1
        elif b == 0x81: print(f'  {i:04X}: ADD A,C'); i+=1
        elif b == 0xC6: print(f'  {i:04X}: ADD A,0x{rom[i+1]:02X}'); i+=2
        elif b == 0xD6: print(f'  {i:04X}: SUB 0x{rom[i+1]:02X}'); i+=2
        elif b == 0x90: print(f'  {i:04X}: SUB B'); i+=1
        elif b == 0x91: print(f'  {i:04X}: SUB C'); i+=1
        elif b == 0x97: print(f'  {i:04X}: SUB A'); i+=1
        elif b == 0x87: print(f'  {i:04X}: ADD A,A'); i+=1
        elif b == 0xB8: print(f'  {i:04X}: CP B'); i+=1
        elif b == 0xBB: print(f'  {i:04X}: CP E'); i+=1
        elif b == 0xBC: print(f'  {i:04X}: CP H'); i+=1
        elif b == 0xBD: print(f'  {i:04X}: CP L'); i+=1
        elif b == 0xBF: print(f'  {i:04X}: CP A'); i+=1
        elif b == 0x60: print(f'  {i:04X}: LD H,B'); i+=1
        elif b == 0x61: print(f'  {i:04X}: LD H,C'); i+=1
        elif b == 0x62: print(f'  {i:04X}: LD H,D'); i+=1
        elif b == 0x63: print(f'  {i:04X}: LD H,E'); i+=1
        elif b == 0x68: print(f'  {i:04X}: LD L,B'); i+=1
        elif b == 0x69: print(f'  {i:04X}: LD L,C'); i+=1
        elif b == 0x6A: print(f'  {i:04X}: LD L,D'); i+=1
        elif b == 0x6B: print(f'  {i:04X}: LD L,E'); i+=1
        elif b == 0x48: print(f'  {i:04X}: LD C,B'); i+=1
        elif b == 0x40: print(f'  {i:04X}: LD B,B'); i+=1
        elif b == 0x41: print(f'  {i:04X}: LD B,C'); i+=1
        elif b == 0x42: print(f'  {i:04X}: LD B,D'); i+=1
        elif b == 0x43: print(f'  {i:04X}: LD B,E'); i+=1
        elif b == 0x44: print(f'  {i:04X}: LD B,H'); i+=1
        elif b == 0x45: print(f'  {i:04X}: LD B,L'); i+=1
        elif b == 0x50: print(f'  {i:04X}: LD D,H'); i+=1  # wrong, LD D,B
        elif b == 0xCB:
            b2 = rom[i+1]
            if   b2 == 0x5F: print(f'  {i:04X}: BIT 3,A'); i+=2
        else:
            print(f'  {i:04X}: [{b:02X}]'); i+=1

start = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x0440
end   = int(sys.argv[2], 16) if len(sys.argv) > 2 else start + 0x80
disasm(start, end)

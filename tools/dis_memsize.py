#!/usr/bin/env python3
"""Disassemble TRS-80 ROM memory-size detection routines."""
import sys

rom = open('roms/level2.rom','rb').read()

def dis(start, n=40):
    i = start
    cnt = 0
    two = {0x01:'LD BC,**', 0x11:'LD DE,**', 0x21:'LD HL,**', 0x22:'LD (**),HL',
           0x2A:'LD HL,(**)', 0x31:'LD SP,**', 0x32:'LD (**),A', 0x3A:'LD A,(**)',
           0xC2:'JP NZ,**', 0xC3:'JP **', 0xC4:'CALL NZ,**', 0xCA:'JP Z,**',
           0xCC:'CALL Z,**', 0xCD:'CALL **', 0xC0:'RET NZ', 0xDA:'JP C,**',
           0xD2:'JP NC,**', 0xEA:'JP PE,**', 0xFA:'JP M,**', 0xF2:'JP P,**'}
    one = {0x01:'ld a,*', 0x06:'LD B,*', 0x0E:'LD C,*', 0x16:'LD D,*',
           0x1E:'LD E,*', 0x18:'JR *', 0x20:'JR NZ,*', 0x28:'JR Z,*',
           0x30:'JR NC,*', 0x38:'JR C,*', 0x36:'LD (HL),*', 0x3E:'LD A,*',
           0xD3:'OUT (*),A', 0xDB:'IN A,(*)', 0xE6:'AND *', 0xEE:'XOR *',
           0xF6:'OR *', 0xFE:'CP *', 0x26:'LD H,*', 0x2E:'LD L,*'}
    names = {0x00:'NOP', 0x02:'LD (BC),A', 0x03:'INC BC', 0x04:'INC B',
             0x0A:'LD A,(BC)', 0x0B:'DEC BC', 0x12:'LD (DE),A',0x13:'INC DE',
             0x17:'RLA', 0x19:'ADD HL,DE', 0x1A:'LD A,(DE)', 0x1F:'RRA',
             0x23:'INC HL', 0x2B:'DEC HL', 0x2F:'CPL', 0x3C:'INC A', 0x3D:'DEC A',
             0x41:'LD B,C', 0x47:'LD B,A', 0x4F:'LD C,A', 0x57:'LD D,A',
             0x5F:'LD E,A', 0x67:'LD H,A', 0x6F:'LD L,A', 0x77:'LD (HL),A',
             0x78:'LD A,B', 0x79:'LD A,C', 0x7A:'LD A,D', 0x7B:'LD A,E',
             0x7C:'LD A,H', 0x7D:'LD A,L', 0x7E:'LD A,(HL)',
             0xA0:'AND B', 0xA4:'AND H', 0xA5:'AND L', 0xA7:'AND A',
             0xA8:'XOR B', 0xAF:'XOR A', 0xB0:'OR B', 0xB4:'OR H',
             0xB5:'OR L', 0xB7:'OR A', 0xBE:'CP (HL)',
             0xC1:'POP BC', 0xC5:'PUSH BC', 0xC8:'RET Z', 0xC9:'RET',
             0xD0:'RET NC', 0xD1:'POP DE', 0xD5:'PUSH DE', 0xD8:'RET C', 0xD9:'EXX',
             0xE1:'POP HL', 0xE5:'PUSH HL', 0xEB:'EX DE,HL',
             0xF0:'RET P', 0xF1:'POP AF', 0xF5:'PUSH AF', 0xF8:'RET M', 0xF9:'LD SP,HL',
             0x08:"EX AF,AF'", 0xCB:'[CB]', 0xDD:'[DD]', 0xED:'[ED]', 0xFD:'[FD]'}
    while cnt < n and i < len(rom):
        b = rom[i]
        if b in two:
            addr = rom[i+1] | (rom[i+2]<<8)
            nm = two[b].replace('**', f'0x{addr:04X}')
            bstr = f'{b:02X} {rom[i+1]:02X} {rom[i+2]:02X}'
            print(f'  0x{i:04X}: {bstr:10s}  {nm}')
            i += 3
        elif b in one:
            v = rom[i+1]
            if 'JR' in one[b]:
                if v >= 128: v -= 256
                tgt = i + 2 + v
                nm = one[b].replace('*', f'0x{tgt:04X}')
            else:
                nm = one[b].replace('*', f'0x{v:02X}')
            bstr = f'{b:02X} {rom[i+1]:02X}'
            print(f'  0x{i:04X}: {bstr:10s}  {nm}')
            i += 2
        else:
            nm = names.get(b, f'???({b:02X})')
            print(f'  0x{i:04X}: {b:02X}          {nm}')
            i += 1
        cnt += 1

print("=== ROM 0x04B0 — pre-boot / clear bit3 ===")
dis(0x04B0, 30)
print()
print("=== ROM 0x04E0 — memory size check ===")
dis(0x04E0, 40)
print()
print("=== ROM 0x0540 — another 0x403D ref ===")
dis(0x0540, 15)

#!/usr/bin/env python3
"""
disasm_bootldr.py — Disassemble T00/S0 (boot loader at 0x4200)
and T00/S2 (second-stage at 0x4777) directly from the JV1 disk image.

T00/S0 is loaded to 0x4200 by the Level 2 ROM at PC=0x06C5.
T00/S2 is loaded to somewhere in the 0x4000-0x5FFF workspace by the second-stage.
"""

import os, sys

DISK = os.path.join(os.path.dirname(__file__), '..', 'disks', 'ld1-531.dsk')
SPT  = 10
BPS  = 256

def read_sector(d, t, s):
    off = (t * SPT + s) * BPS
    return bytearray(d[off:off+BPS])

def disasm(data, base, length=None, label=""):
    if length is None:
        length = len(data)
    if label:
        print(f"\n{'='*64}")
        print(f"  {label}  (base=0x{base:04X})")
        print(f"{'='*64}")

    def byte(pc, off=0):
        i = pc + off
        return data[i] if i < len(data) else 0

    def word(pc, off=0):
        return byte(pc, off) | (byte(pc, off+1) << 8)

    def sword(pc, off=0):
        v = byte(pc, off)
        return v - 256 if v >= 128 else v

    def reg8(r):
        return ["B","C","D","E","H","L","(HL)","A"][r & 7]

    def reg16(r):
        return ["BC","DE","HL","SP"][r & 3]

    def cc(r):
        return ["NZ","Z","NC","C","PO","PE","P","M"][r & 7]

    pc = 0
    while pc < length:
        b0 = byte(pc)
        raw = []
        mn = f"DB 0x{b0:02X}"
        sz = 1

        # Decode
        if b0 in (0x00,): mn, sz = "NOP", 1
        elif b0 == 0x08: mn, sz = "EX AF,AF'", 1
        elif b0 == 0x76: mn, sz = "HALT", 1
        elif b0 == 0xEB: mn, sz = "EX DE,HL", 1
        elif b0 == 0xD9: mn, sz = "EXX", 1
        elif b0 == 0xE9: mn, sz = "JP (HL)", 1
        elif b0 == 0xF9: mn, sz = "LD SP,HL", 1
        elif b0 == 0xF3: mn, sz = "DI", 1
        elif b0 == 0xFB: mn, sz = "EI", 1
        elif b0 == 0x27: mn, sz = "DAA", 1
        elif b0 == 0x2F: mn, sz = "CPL", 1
        elif b0 == 0x37: mn, sz = "SCF", 1
        elif b0 == 0x3F: mn, sz = "CCF", 1
        elif b0 == 0xE3: mn, sz = "EX (SP),HL", 1
        elif b0 == 0xC9: mn, sz = "RET", 1
        elif b0 & 0xFF == 0x07: mn, sz = ["RLCA","RRCA","RLA","RRA"][(b0>>3)&3], 1
        elif b0 == 0xA7: mn, sz = "AND A", 1
        elif b0 == 0xAF: mn, sz = "XOR A", 1
        elif b0 == 0xB7: mn, sz = "OR A", 1
        elif b0 == 0xBF: mn, sz = "CP A", 1
        elif b0 & 0xCF == 0x01: mn, sz = f"LD {reg16((b0>>4))},0x{word(pc,1):04X}", 3
        elif b0 & 0xCF == 0x09: mn, sz = f"ADD HL,{reg16((b0>>4))}", 1
        elif b0 & 0xCF == 0x03: mn, sz = f"INC {reg16((b0>>4))}", 1
        elif b0 & 0xCF == 0x0B: mn, sz = f"DEC {reg16((b0>>4))}", 1
        elif b0 & 0xCF == 0xC1: mn, sz = f"POP {['BC','DE','HL','AF'][(b0>>4)&3]}", 1
        elif b0 & 0xCF == 0xC5: mn, sz = f"PUSH {['BC','DE','HL','AF'][(b0>>4)&3]}", 1
        elif b0 & 0xC7 == 0x04: mn, sz = f"INC {reg8(b0>>3)}", 1
        elif b0 & 0xC7 == 0x05: mn, sz = f"DEC {reg8(b0>>3)}", 1
        elif b0 & 0xC7 == 0x06: mn, sz = f"LD {reg8(b0>>3)},0x{byte(pc,1):02X}", 2
        elif b0 & 0xC0 == 0x40 and b0 != 0x76:
            mn, sz = f"LD {reg8(b0>>3)},{reg8(b0)}", 1
        elif b0 & 0xF8 == 0x80: mn, sz = f"ADD A,{reg8(b0)}", 1
        elif b0 & 0xF8 == 0x88: mn, sz = f"ADC A,{reg8(b0)}", 1
        elif b0 & 0xF8 == 0x90: mn, sz = f"SUB {reg8(b0)}", 1
        elif b0 & 0xF8 == 0x98: mn, sz = f"SBC A,{reg8(b0)}", 1
        elif b0 & 0xF8 == 0xA0: mn, sz = f"AND {reg8(b0)}", 1
        elif b0 & 0xF8 == 0xA8: mn, sz = f"XOR {reg8(b0)}", 1
        elif b0 & 0xF8 == 0xB0: mn, sz = f"OR {reg8(b0)}", 1
        elif b0 & 0xF8 == 0xB8: mn, sz = f"CP {reg8(b0)}", 1
        elif b0 == 0x02: mn, sz = "LD (BC),A", 1
        elif b0 == 0x0A: mn, sz = "LD A,(BC)", 1
        elif b0 == 0x12: mn, sz = "LD (DE),A", 1
        elif b0 == 0x1A: mn, sz = "LD A,(DE)", 1
        elif b0 == 0x22: mn, sz = f"LD (0x{word(pc,1):04X}),HL", 3
        elif b0 == 0x2A: mn, sz = f"LD HL,(0x{word(pc,1):04X})", 3
        elif b0 == 0x32: mn, sz = f"LD (0x{word(pc,1):04X}),A", 3
        elif b0 == 0x3A: mn, sz = f"LD A,(0x{word(pc,1):04X})", 3
        elif b0 == 0x36: mn, sz = f"LD (HL),0x{byte(pc,1):02X}", 2
        elif b0 == 0xC6: mn, sz = f"ADD A,0x{byte(pc,1):02X}", 2
        elif b0 == 0xCE: mn, sz = f"ADC A,0x{byte(pc,1):02X}", 2
        elif b0 == 0xD6: mn, sz = f"SUB 0x{byte(pc,1):02X}", 2
        elif b0 == 0xDE: mn, sz = f"SBC A,0x{byte(pc,1):02X}", 2
        elif b0 == 0xE6: mn, sz = f"AND 0x{byte(pc,1):02X}", 2
        elif b0 == 0xEE: mn, sz = f"XOR 0x{byte(pc,1):02X}", 2
        elif b0 == 0xF6: mn, sz = f"OR 0x{byte(pc,1):02X}", 2
        elif b0 == 0xFE: mn, sz = f"CP 0x{byte(pc,1):02X}", 2
        elif b0 == 0xC3: mn, sz = f"JP 0x{word(pc,1):04X}", 3
        elif b0 == 0xCD: mn, sz = f"CALL 0x{word(pc,1):04X}", 3
        elif b0 == 0x18: mn, sz = f"JR 0x{(base+pc+2+sword(pc,1))&0xFFFF:04X}", 2
        elif b0 == 0x10: mn, sz = f"DJNZ 0x{(base+pc+2+sword(pc,1))&0xFFFF:04X}", 2
        elif b0 & 0xE7 == 0x20:
            cond = ["NZ","Z","NC","C"][(b0>>3)&3]
            mn, sz = f"JR {cond},0x{(base+pc+2+sword(pc,1))&0xFFFF:04X}", 2
        elif b0 & 0xC7 == 0xC2: mn, sz = f"JP {cc((b0>>3))},0x{word(pc,1):04X}", 3
        elif b0 & 0xC7 == 0xC4: mn, sz = f"CALL {cc((b0>>3))},0x{word(pc,1):04X}", 3
        elif b0 & 0xC7 == 0xC0: mn, sz = f"RET {cc((b0>>3))}", 1
        elif b0 & 0xC7 == 0xC7: mn, sz = f"RST 0x{b0&0x38:02X}", 1
        elif b0 == 0xDB: mn, sz = f"IN A,(0x{byte(pc,1):02X})", 2
        elif b0 == 0xD3: mn, sz = f"OUT (0x{byte(pc,1):02X}),A", 2
        elif b0 == 0xCB:
            b1 = byte(pc, 1)
            ops = ["RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"]
            if b1 < 0x40: mn = f"{ops[(b1>>3)&7]} {reg8(b1)}"
            elif b1 < 0x80: mn = f"BIT {(b1>>3)&7},{reg8(b1)}"
            elif b1 < 0xC0: mn = f"RES {(b1>>3)&7},{reg8(b1)}"
            else: mn = f"SET {(b1>>3)&7},{reg8(b1)}"
            sz = 2
        elif b0 == 0xED:
            b1 = byte(pc, 1)
            ed = {
                0x40:"IN B,(C)",0x48:"IN C,(C)",0x50:"IN D,(C)",0x58:"IN E,(C)",
                0x60:"IN H,(C)",0x68:"IN L,(C)",0x78:"IN A,(C)",
                0x41:"OUT (C),B",0x49:"OUT (C),C",0x51:"OUT (C),D",0x59:"OUT (C),E",
                0x61:"OUT (C),H",0x69:"OUT (C),L",0x79:"OUT (C),A",
                0x47:"LD I,A",0x4F:"LD R,A",0x57:"LD A,I",0x5F:"LD A,R",
                0x56:"IM 1",0x5E:"IM 2",0x46:"IM 0",
                0x4D:"RETI",0x45:"RETN",
                0xA0:"LDI",0xA8:"LDD",0xB0:"LDIR",0xB8:"LDDR",
                0xA1:"CPI",0xA9:"CPD",0xB1:"CPIR",0xB9:"CPDR",
                0xA2:"INI",0xB2:"INIR",0xAA:"IND",0xBA:"INDR",
                0xA3:"OUTI",0xB3:"OTIR",0xAB:"OUTD",0xBB:"OTDR",
                0x42:"SBC HL,BC",0x52:"SBC HL,DE",0x62:"SBC HL,HL",0x72:"SBC HL,SP",
                0x4A:"ADC HL,BC",0x5A:"ADC HL,DE",0x6A:"ADC HL,HL",0x7A:"ADC HL,SP",
                0x44:"NEG",
            }
            if b1 in ed:
                mn, sz = ed[b1], 2
            elif b1 in (0x43,0x53,0x63,0x73):
                rr = ["BC","DE","HL","SP"][(b1>>4)-4]
                mn, sz = f"LD (0x{word(pc,2):04X}),{rr}", 4
            elif b1 in (0x4B,0x5B,0x6B,0x7B):
                rr = ["BC","DE","HL","SP"][(b1>>4)-4]
                mn, sz = f"LD {rr},(0x{word(pc,2):04X})", 4
            else:
                mn, sz = f"ED 0x{b1:02X}", 2
        elif b0 == 0xDD:
            b1 = byte(pc, 1)
            if b1 == 0x21: mn, sz = f"LD IX,0x{word(pc,2):04X}", 4
            elif b1 == 0x22: mn, sz = f"LD (0x{word(pc,2):04X}),IX", 4
            elif b1 == 0x2A: mn, sz = f"LD IX,(0x{word(pc,2):04X})", 4
            elif b1 == 0xE5: mn, sz = "PUSH IX", 2
            elif b1 == 0xE1: mn, sz = "POP IX", 2
            elif b1 == 0xE9: mn, sz = "JP (IX)", 2
            elif b1 == 0x23: mn, sz = "INC IX", 2
            elif b1 == 0x2B: mn, sz = "DEC IX", 2
            elif b1 in (0x09,0x19,0x29,0x39):
                rr = ["BC","DE","IX","SP"][(b1>>4)&3]
                mn, sz = f"ADD IX,{rr}", 2
            else:
                d = byte(pc, 2); ds = d-256 if d > 127 else d
                ds_str = f"{ds:+d}"
                r8_map = {0x7E:"A",0x77:"A",0x5E:"E",0x4E:"C",0x56:"D",0x66:"H",0x6E:"L",0x46:"B"}
                if b1 == 0x7E: mn, sz = f"LD A,(IX{ds_str})", 3
                elif b1 == 0x77: mn, sz = f"LD (IX{ds_str}),A", 3
                elif b1 == 0x46: mn, sz = f"LD B,(IX{ds_str})", 3
                elif b1 == 0x4E: mn, sz = f"LD C,(IX{ds_str})", 3
                elif b1 == 0x56: mn, sz = f"LD D,(IX{ds_str})", 3
                elif b1 == 0x5E: mn, sz = f"LD E,(IX{ds_str})", 3
                elif b1 == 0x66: mn, sz = f"LD H,(IX{ds_str})", 3
                elif b1 == 0x6E: mn, sz = f"LD L,(IX{ds_str})", 3
                elif b1 == 0x36: mn, sz = f"LD (IX{ds_str}),0x{byte(pc,3):02X}", 4
                elif b1 == 0x34: mn, sz = f"INC (IX{ds_str})", 3
                elif b1 == 0x35: mn, sz = f"DEC (IX{ds_str})", 3
                elif b1 == 0xBE: mn, sz = f"CP (IX{ds_str})", 3
                elif b1 == 0xB6: mn, sz = f"OR (IX{ds_str})", 3
                elif b1 == 0xA6: mn, sz = f"AND (IX{ds_str})", 3
                elif b1 == 0x86: mn, sz = f"ADD A,(IX{ds_str})", 3
                else: mn, sz = f"DD 0x{b1:02X}", 2
        elif b0 == 0xFD:
            b1 = byte(pc, 1)
            if b1 == 0x21: mn, sz = f"LD IY,0x{word(pc,2):04X}", 4
            elif b1 == 0x22: mn, sz = f"LD (0x{word(pc,2):04X}),IY", 4
            elif b1 == 0x2A: mn, sz = f"LD IY,(0x{word(pc,2):04X})", 4
            elif b1 == 0xE5: mn, sz = "PUSH IY", 2
            elif b1 == 0xE1: mn, sz = "POP IY", 2
            elif b1 == 0xE9: mn, sz = "JP (IY)", 2
            elif b1 == 0x23: mn, sz = "INC IY", 2
            elif b1 == 0x2B: mn, sz = "DEC IY", 2
            else:
                d = byte(pc, 2); ds = d-256 if d > 127 else d
                ds_str = f"{ds:+d}"
                if b1 == 0x7E: mn, sz = f"LD A,(IY{ds_str})", 3
                elif b1 == 0x77: mn, sz = f"LD (IY{ds_str}),A", 3
                elif b1 == 0x5E: mn, sz = f"LD E,(IY{ds_str})", 3
                elif b1 == 0x4E: mn, sz = f"LD C,(IY{ds_str})", 3
                elif b1 == 0x56: mn, sz = f"LD D,(IY{ds_str})", 3
                elif b1 == 0xBE: mn, sz = f"CP (IY{ds_str})", 3
                elif b1 == 0xB6: mn, sz = f"OR (IY{ds_str})", 3
                elif b1 == 0xA6: mn, sz = f"AND (IY{ds_str})", 3
                else: mn, sz = f"FD 0x{b1:02X}", 2

        raw_bytes = " ".join(f"{data[pc+i]:02X}" for i in range(min(sz, len(data)-pc)))
        print(f"  {base+pc:04X}: {raw_bytes:<14} {mn}")
        pc += sz

with open(DISK, 'rb') as f:
    disk = f.read()

# T00/S0 — boot loader, loaded to 0x4200 by ROM
t00s0 = read_sector(disk, 0, 0)
disasm(t00s0, 0x4200, 256, "T00/S0 — Boot Loader (loaded to 0x4200 by ROM)")

# T00/S2 — second-stage header / init code read by 0x4777
t00s2 = read_sector(disk, 0, 2)
disasm(t00s2, 0x4200, 256, "T00/S2 — Second-stage (first sector read by 0x4777)")

# T17/S8 — config sector (tells 0x4777 what to load)
t17s8 = read_sector(disk, 17, 8)
print(f"\n{'='*64}")
print(f"  T17/S8 — directory config sector")
print(f"{'='*64}")
for row in range(0, 32, 16):
    raw = " ".join(f"{t17s8[row+i]:02X}" for i in range(16))
    asc = "".join(chr(t17s8[row+i]) if 0x20 <= t17s8[row+i] < 0x7F else "." for i in range(16))
    print(f"  {row:04X}: {raw}  {asc}")

#!/usr/bin/env python3
"""
disasm_from_log.py — Extract [4777ENTRY] bytes from log.txt and disassemble.
Disassembles the actual in-RAM code at 0x4600-0x4900 when CALL 0x4777 fires.
"""
import sys, os, re

LOG = os.path.join(os.path.dirname(__file__), '..', 'log.txt')

# Parse [4777ENTRY] lines to get RAM bytes
ram = {}
with open(LOG) as f:
    for line in f:
        m = re.match(r'\[4777ENTRY\]\s+([0-9A-Fa-f]{4}):((?:\s+[0-9A-Fa-f]{2}){16})', line)
        if m:
            addr = int(m.group(1), 16)
            vals = [int(x, 16) for x in m.group(2).split()]
            for i, v in enumerate(vals):
                ram[addr + i] = v

if not ram:
    print("No [4777ENTRY] data found in log.txt")
    sys.exit(1)

lo = min(ram); hi = max(ram)
print(f"Extracted RAM: 0x{lo:04X}–0x{hi:04X} ({hi-lo+1} bytes)")

data_arr = bytearray(hi - lo + 1)
for addr, val in ram.items():
    data_arr[addr - lo] = val

# Reuse the disassembler from disasm_bootldr.py embedded here
def disasm(data, base, start_pc=None, length=None):
    if start_pc is None: start_pc = base
    if length is None: length = len(data)

    def byte(pc, off=0):
        i = pc - base + off
        return data[i] if 0 <= i < len(data) else 0
    def word(pc, off=0):
        return byte(pc, off) | (byte(pc, off+1) << 8)
    def sword(pc, off=0):
        v = byte(pc, off)
        return v - 256 if v >= 128 else v
    def reg8(r): return ["B","C","D","E","H","L","(HL)","A"][r & 7]
    def reg16(r): return ["BC","DE","HL","SP"][r & 3]
    def cc(r): return ["NZ","Z","NC","C","PO","PE","P","M"][r & 7]

    pc = start_pc
    count = 0
    while pc <= base + length - 1:
        b0 = byte(pc)
        mn = f"DB 0x{b0:02X}"
        sz = 1

        if b0 == 0x00: mn, sz = "NOP", 1
        elif b0 == 0x76: mn, sz = "HALT", 1
        elif b0 == 0xEB: mn, sz = "EX DE,HL", 1
        elif b0 == 0xD9: mn, sz = "EXX", 1
        elif b0 == 0xE9: mn, sz = "JP (HL)", 1
        elif b0 == 0xF9: mn, sz = "LD SP,HL", 1
        elif b0 == 0xF3: mn, sz = "DI", 1
        elif b0 == 0xFB: mn, sz = "EI", 1
        elif b0 == 0x27: mn, sz = "DAA", 1
        elif b0 == 0x2F: mn, sz = "CPL", 1
        elif b0 == 0xC9: mn, sz = "RET", 1
        elif b0 == 0xE3: mn, sz = "EX (SP),HL", 1
        elif b0 == 0xAF: mn, sz = "XOR A", 1
        elif b0 == 0xA7: mn, sz = "AND A", 1
        elif b0 == 0xB7: mn, sz = "OR A", 1
        elif b0 == 0xBF: mn, sz = "CP A", 1
        elif b0 & 0xCF == 0x01: mn, sz = f"LD {reg16(b0>>4)},0x{word(pc,1):04X}", 3
        elif b0 & 0xCF == 0x09: mn, sz = f"ADD HL,{reg16(b0>>4)}", 1
        elif b0 & 0xCF == 0x03: mn, sz = f"INC {reg16(b0>>4)}", 1
        elif b0 & 0xCF == 0x0B: mn, sz = f"DEC {reg16(b0>>4)}", 1
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
        elif b0 == 0xDE: mn, sz = f"SBC A,0x{byte(pc,1):02X}", 2
        elif b0 == 0xE6: mn, sz = f"AND 0x{byte(pc,1):02X}", 2
        elif b0 == 0xEE: mn, sz = f"XOR 0x{byte(pc,1):02X}", 2
        elif b0 == 0xF6: mn, sz = f"OR 0x{byte(pc,1):02X}", 2
        elif b0 == 0xFE: mn, sz = f"CP 0x{byte(pc,1):02X}", 2
        elif b0 == 0xD6: mn, sz = f"SUB 0x{byte(pc,1):02X}", 2
        elif b0 == 0xCE: mn, sz = f"ADC A,0x{byte(pc,1):02X}", 2
        elif b0 == 0xC3: mn, sz = f"JP 0x{word(pc,1):04X}", 3
        elif b0 == 0xCD: mn, sz = f"CALL 0x{word(pc,1):04X}", 3
        elif b0 == 0x18: mn, sz = f"JR 0x{(pc+2+sword(pc,1))&0xFFFF:04X}", 2
        elif b0 == 0x10: mn, sz = f"DJNZ 0x{(pc+2+sword(pc,1))&0xFFFF:04X}", 2
        elif b0 & 0xE7 == 0x20:
            cond = ["NZ","Z","NC","C"][(b0>>3)&3]
            mn, sz = f"JR {cond},0x{(pc+2+sword(pc,1))&0xFFFF:04X}", 2
        elif b0 & 0xC7 == 0xC2: mn, sz = f"JP {cc(b0>>3)},0x{word(pc,1):04X}", 3
        elif b0 & 0xC7 == 0xC4: mn, sz = f"CALL {cc(b0>>3)},0x{word(pc,1):04X}", 3
        elif b0 & 0xC7 == 0xC0: mn, sz = f"RET {cc(b0>>3)}", 1
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
                0x44:"NEG",0x4C:"NEG",
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
        elif b0 in (0xDD, 0xFD):
            reg = "IX" if b0 == 0xDD else "IY"
            b1 = byte(pc, 1)
            if b1 == 0x21: mn, sz = f"LD {reg},0x{word(pc,2):04X}", 4
            elif b1 == 0x22: mn, sz = f"LD (0x{word(pc,2):04X}),{reg}", 4
            elif b1 == 0x2A: mn, sz = f"LD {reg},(0x{word(pc,2):04X})", 4
            elif b1 == 0xE5: mn, sz = f"PUSH {reg}", 2
            elif b1 == 0xE1: mn, sz = f"POP {reg}", 2
            elif b1 == 0xE9: mn, sz = f"JP ({reg})", 2
            elif b1 == 0x23: mn, sz = f"INC {reg}", 2
            elif b1 == 0x2B: mn, sz = f"DEC {reg}", 2
            elif b1 in (0x09,0x19,0x29,0x39):
                rr = ["BC","DE",reg,"SP"][(b1>>4)&3]
                mn, sz = f"ADD {reg},{rr}", 2
            else:
                d = byte(pc, 2); ds = d-256 if d > 127 else d
                ds_str = f"{ds:+d}"
                if b1 == 0x7E: mn, sz = f"LD A,({reg}{ds_str})", 3
                elif b1 == 0x77: mn, sz = f"LD ({reg}{ds_str}),A", 3
                elif b1 == 0x70: mn, sz = f"LD ({reg}{ds_str}),B", 3
                elif b1 == 0x71: mn, sz = f"LD ({reg}{ds_str}),C", 3
                elif b1 == 0x72: mn, sz = f"LD ({reg}{ds_str}),D", 3
                elif b1 == 0x73: mn, sz = f"LD ({reg}{ds_str}),E", 3
                elif b1 == 0x74: mn, sz = f"LD ({reg}{ds_str}),H", 3
                elif b1 == 0x75: mn, sz = f"LD ({reg}{ds_str}),L", 3
                elif b1 == 0x46: mn, sz = f"LD B,({reg}{ds_str})", 3
                elif b1 == 0x4E: mn, sz = f"LD C,({reg}{ds_str})", 3
                elif b1 == 0x56: mn, sz = f"LD D,({reg}{ds_str})", 3
                elif b1 == 0x5E: mn, sz = f"LD E,({reg}{ds_str})", 3
                elif b1 == 0x66: mn, sz = f"LD H,({reg}{ds_str})", 3
                elif b1 == 0x6E: mn, sz = f"LD L,({reg}{ds_str})", 3
                elif b1 == 0x36: mn, sz = f"LD ({reg}{ds_str}),0x{byte(pc,3):02X}", 4
                elif b1 == 0x34: mn, sz = f"INC ({reg}{ds_str})", 3
                elif b1 == 0x35: mn, sz = f"DEC ({reg}{ds_str})", 3
                elif b1 == 0xBE: mn, sz = f"CP ({reg}{ds_str})", 3
                elif b1 == 0xB6: mn, sz = f"OR ({reg}{ds_str})", 3
                elif b1 == 0xA6: mn, sz = f"AND ({reg}{ds_str})", 3
                elif b1 == 0x86: mn, sz = f"ADD A,({reg}{ds_str})", 3
                elif b1 == 0x8E: mn, sz = f"ADC A,({reg}{ds_str})", 3
                elif b1 == 0x96: mn, sz = f"SUB ({reg}{ds_str})", 3
                elif b1 == 0xBE: mn, sz = f"CP ({reg}{ds_str})", 3
                elif b1 & 0xC0 == 0x40 and b1 != 0x76:
                    mn, sz = f"LD {reg8(b1>>3)},({reg}{ds_str})", 3
                elif b1 == 0xCB:
                    b3 = byte(pc, 3)  # DDCB offset opcode
                    ops = ["RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"]
                    if b3 < 0x40: mn = f"{ops[(b3>>3)&7]} ({reg}{ds_str}),{reg8(b3)}"
                    elif b3 < 0x80: mn = f"BIT {(b3>>3)&7},({reg}{ds_str})"
                    elif b3 < 0xC0: mn = f"RES {(b3>>3)&7},({reg}{ds_str})"
                    else: mn = f"SET {(b3>>3)&7},({reg}{ds_str})"
                    sz = 4
                else:
                    mn, sz = f"{reg} 0x{b1:02X}", 2
        elif b0 == 0x08: mn, sz = "EX AF,AF'", 1
        elif b0 == 0x37: mn, sz = "SCF", 1
        elif b0 == 0x3F: mn, sz = "CCF", 1

        raw_bytes = [byte(pc, i) for i in range(min(sz, 8))]
        rb = " ".join(f"{v:02X}" for v in raw_bytes)
        print(f"  {pc:04X}: {rb:<17} {mn}")
        pc += sz
        count += 1

# Disassemble from 0x4777 (the second-stage entry point)
print(f"\n{'='*64}")
print(f"  Second-stage at 0x4777  (CALL from SYS12 at 0x4E8C)")
print(f"{'='*64}")
disasm(data_arr, lo, start_pc=0x4777, length=0x4900-lo+1)

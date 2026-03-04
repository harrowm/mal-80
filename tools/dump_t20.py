#!/usr/bin/env python3
"""
dump_t20.py — Extract and parse T20/S5-S8 ("Copyrigh" module) from ld1-531.dsk
and disassemble the surrounding second-stage loader from a RAM dump.

JV1 format: sector offset = (track * 10 + sector) * 256
"""

import sys
import struct
import os

DISK = os.path.join(os.path.dirname(__file__), '..', 'disks', 'ld1-531.dsk')
SECTORS_PER_TRACK = 10
BYTES_PER_SECTOR  = 256

def read_sector(disk_data, track, sector):
    off = (track * SECTORS_PER_TRACK + sector) * BYTES_PER_SECTOR
    return disk_data[off:off + BYTES_PER_SECTOR]

# ---------------------------------------------------------------------------
# Minimal Z80 disassembler (covers the common instructions we see in loaders)
# ---------------------------------------------------------------------------
def disasm_one(mem, pc):
    """Returns (mnemonic_string, bytes_consumed)."""
    def byte(off=0): return mem[pc + off] if pc+off < len(mem) else 0
    def word(off=0): return byte(off) | (byte(off+1) << 8)
    def sword(off=0):
        v = byte(off); return v - 256 if v >= 128 else v

    b = byte(0)
    # Simple opcode table (extend as needed)
    simple = {
        0x00: ("NOP", 1), 0x07: ("RLCA", 1), 0x08: ("EX AF,AF'", 1),
        0x09: ("ADD HL,BC", 1), 0x0F: ("RRCA", 1),
        0x17: ("RLA", 1), 0x1F: ("RRA", 1),
        0x27: ("DAA", 1), 0x2F: ("CPL", 1),
        0x37: ("SCF", 1), 0x3F: ("CCF", 1),
        0x76: ("HALT", 1), 0xE9: ("JP (HL)", 1),
        0xEB: ("EX DE,HL", 1), 0xF3: ("DI", 1), 0xFB: ("EI", 1),
        0xE3: ("EX (SP),HL", 1), 0xF9: ("LD SP,HL", 1),
        0xC9: ("RET", 1), 0xD9: ("EXX", 1),
        0xE1: ("POP HL", 1), 0xD1: ("POP DE", 1),
        0xC1: ("POP BC", 1), 0xF1: ("POP AF", 1),
        0xE5: ("PUSH HL", 1), 0xD5: ("PUSH DE", 1),
        0xC5: ("PUSH BC", 1), 0xF5: ("PUSH AF", 1),
        0xA7: ("AND A", 1), 0xB7: ("OR A", 1), 0xAF: ("XOR A", 1),
        0xBF: ("CP A", 1),  0x2B: ("DEC HL", 1), 0x3B: ("DEC SP", 1),
        0x23: ("INC HL", 1), 0x33: ("INC SP", 1),
        0x13: ("INC DE", 1), 0x0B: ("DEC BC", 1),
        0x03: ("INC BC", 1), 0x1B: ("DEC DE", 1),
        0x7E: ("LD A,(HL)", 1), 0x77: ("LD (HL),A", 1),
        0x46: ("LD B,(HL)", 1), 0x4E: ("LD C,(HL)", 1),
        0x56: ("LD D,(HL)", 1), 0x5E: ("LD E,(HL)", 1),
        0x66: ("LD H,(HL)", 1), 0x6E: ("LD L,(HL)", 1),
        0x47: ("LD B,A", 1), 0x4F: ("LD C,A", 1),
        0x57: ("LD D,A", 1), 0x5F: ("LD E,A", 1),
        0x67: ("LD H,A", 1), 0x6F: ("LD L,A", 1),
        0x78: ("LD A,B", 1), 0x79: ("LD A,C", 1),
        0x7A: ("LD A,D", 1), 0x7B: ("LD A,E", 1),
        0x7C: ("LD A,H", 1), 0x7D: ("LD A,L", 1),
        0x40: ("LD B,B", 1), 0x41: ("LD B,C", 1), 0x42: ("LD B,D", 1),
        0x43: ("LD B,E", 1), 0x44: ("LD B,H", 1), 0x45: ("LD B,L", 1),
        0x48: ("LD C,B", 1), 0x60: ("LD H,B", 1), 0x61: ("LD H,C", 1),
        0x62: ("LD H,D", 1), 0x63: ("LD H,E", 1), 0x64: ("LD H,H", 1),
        0x65: ("LD H,L", 1), 0x68: ("LD L,B", 1), 0x69: ("LD L,C", 1),
        0x6A: ("LD L,D", 1), 0x6B: ("LD L,E", 1), 0x6C: ("LD L,H", 1),
        0x6D: ("LD L,L", 1),
        0xB8: ("CP B", 1), 0xB9: ("CP C", 1), 0xBA: ("CP D", 1),
        0xBB: ("CP E", 1), 0xBC: ("CP H", 1), 0xBD: ("CP L", 1),
        0xA0: ("AND B", 1), 0xA1: ("AND C", 1), 0xB0: ("OR B", 1),
        0xB1: ("OR C", 1), 0xB2: ("OR D", 1), 0xB3: ("OR E", 1),
        0xA8: ("XOR B", 1), 0xA9: ("XOR C", 1),
        0x87: ("ADD A,A", 1), 0x80: ("ADD A,B", 1), 0x81: ("ADD A,C", 1),
        0x82: ("ADD A,D", 1), 0x83: ("ADD A,E", 1), 0x84: ("ADD A,H", 1),
        0x85: ("ADD A,L", 1), 0x86: ("ADD A,(HL)", 1),
        0x97: ("SUB A", 1), 0x90: ("SUB B", 1), 0x91: ("SUB C", 1),
        0x92: ("SUB D", 1), 0x93: ("SUB E", 1), 0x94: ("SUB H", 1),
        0x95: ("SUB L", 1), 0x96: ("SUB (HL)", 1),
        0x04: ("INC B", 1), 0x0C: ("INC C", 1), 0x14: ("INC D", 1),
        0x1C: ("INC E", 1), 0x24: ("INC H", 1), 0x2C: ("INC L", 1),
        0x3C: ("INC A", 1), 0x34: ("INC (HL)", 1),
        0x05: ("DEC B", 1), 0x0D: ("DEC C", 1), 0x15: ("DEC D", 1),
        0x1D: ("DEC E", 1), 0x25: ("DEC H", 1), 0x2D: ("DEC L", 1),
        0x3D: ("DEC A", 1), 0x35: ("DEC (HL)", 1),
        0x07: ("RLCA", 1), 0x0F: ("RRCA", 1),
        0x02: ("LD (BC),A", 1), 0x12: ("LD (DE),A", 1),
        0x0A: ("LD A,(BC)", 1), 0x1A: ("LD A,(DE)", 1),
    }
    if b in simple:
        return simple[b]

    if b == 0x01: return (f"LD BC,0x{word(1):04X}", 3)
    if b == 0x11: return (f"LD DE,0x{word(1):04X}", 3)
    if b == 0x21: return (f"LD HL,0x{word(1):04X}", 3)
    if b == 0x31: return (f"LD SP,0x{word(1):04X}", 3)
    if b == 0x06: return (f"LD B,0x{byte(1):02X}", 2)
    if b == 0x0E: return (f"LD C,0x{byte(1):02X}", 2)
    if b == 0x16: return (f"LD D,0x{byte(1):02X}", 2)
    if b == 0x1E: return (f"LD E,0x{byte(1):02X}", 2)
    if b == 0x26: return (f"LD H,0x{byte(1):02X}", 2)
    if b == 0x2E: return (f"LD L,0x{byte(1):02X}", 2)
    if b == 0x3E: return (f"LD A,0x{byte(1):02X}", 2)
    if b == 0x36: return (f"LD (HL),0x{byte(1):02X}", 2)
    if b == 0xC6: return (f"ADD A,0x{byte(1):02X}", 2)
    if b == 0xD6: return (f"SUB 0x{byte(1):02X}", 2)
    if b == 0xE6: return (f"AND 0x{byte(1):02X}", 2)
    if b == 0xF6: return (f"OR 0x{byte(1):02X}", 2)
    if b == 0xEE: return (f"XOR 0x{byte(1):02X}", 2)
    if b == 0xFE: return (f"CP 0x{byte(1):02X}", 2)
    if b == 0xCE: return (f"ADC A,0x{byte(1):02X}", 2)
    if b == 0xDE: return (f"SBC A,0x{byte(1):02X}", 2)
    if b == 0x2A: return (f"LD HL,(0x{word(1):04X})", 3)
    if b == 0x3A: return (f"LD A,(0x{word(1):04X})", 3)
    if b == 0x22: return (f"LD (0x{word(1):04X}),HL", 3)
    if b == 0x32: return (f"LD (0x{word(1):04X}),A", 3)
    if b == 0xC3: return (f"JP 0x{word(1):04X}", 3)
    if b == 0xCD: return (f"CALL 0x{word(1):04X}", 3)
    if b == 0x18: return (f"JR 0x{(pc+2+sword(1))&0xFFFF:04X}", 2)
    if b == 0x20: return (f"JR NZ,0x{(pc+2+sword(1))&0xFFFF:04X}", 2)
    if b == 0x28: return (f"JR Z,0x{(pc+2+sword(1))&0xFFFF:04X}", 2)
    if b == 0x30: return (f"JR NC,0x{(pc+2+sword(1))&0xFFFF:04X}", 2)
    if b == 0x38: return (f"JR C,0x{(pc+2+sword(1))&0xFFFF:04X}", 2)
    if b == 0x10: return (f"DJNZ 0x{(pc+2+sword(1))&0xFFFF:04X}", 2)
    # Conditional CALL/RET/JP
    cc = {0xC4:"NZ",0xCC:"Z",0xD4:"NC",0xDC:"C",0xE4:"PO",0xEC:"PE",0xF4:"P",0xFC:"M"}
    if b in cc: return (f"CALL {cc[b]},0x{word(1):04X}", 3)
    rcc = {0xC0:"NZ",0xC8:"Z",0xD0:"NC",0xD8:"C",0xE0:"PO",0xE8:"PE",0xF0:"P",0xF8:"M"}
    if b in rcc: return (f"RET {rcc[b]}", 1)
    jcc_map = {0xC2:"NZ",0xCA:"Z",0xD2:"NC",0xDA:"C",0xE2:"PO",0xEA:"PE",0xF2:"P",0xFA:"M"}
    if b in jcc_map: return (f"JP {jcc_map[b]},0x{word(1):04X}", 3)
    # RST
    if b & 0xC7 == 0xC7:
        return (f"RST 0x{b & 0x38:02X}", 1)
    # ED prefix
    if b == 0xED:
        b2 = byte(1)
        ed = {
            0x40: ("IN B,(C)", 2), 0x48: ("IN C,(C)", 2), 0x50: ("IN D,(C)", 2),
            0x58: ("IN E,(C)", 2), 0x60: ("IN H,(C)", 2), 0x68: ("IN L,(C)", 2),
            0x78: ("IN A,(C)", 2), 0x70: ("IN F,(C)", 2),
            0x41: ("OUT (C),B", 2), 0x49: ("OUT (C),C", 2), 0x51: ("OUT (C),D", 2),
            0x59: ("OUT (C),E", 2), 0x61: ("OUT (C),H", 2), 0x69: ("OUT (C),L", 2),
            0x79: ("OUT (C),A", 2), 0x47: ("LD I,A", 2), 0x4F: ("LD R,A", 2),
            0x57: ("LD A,I", 2), 0x5F: ("LD A,R", 2),
            0x56: ("IM 1", 2), 0x5E: ("IM 2", 2), 0x46: ("IM 0", 2),
            0x4D: ("RETI", 2), 0x45: ("RETN", 2),
            0xA0: ("LDI", 2), 0xA8: ("LDD", 2), 0xB0: ("LDIR", 2), 0xB8: ("LDDR", 2),
            0xA1: ("CPI", 2), 0xA9: ("CPD", 2), 0xB1: ("CPIR", 2), 0xB9: ("CPDR", 2),
            0xA2: ("INI", 2), 0xB2: ("INIR", 2), 0xAA: ("IND", 2), 0xBA: ("INDR", 2),
            0xA3: ("OUTI", 2), 0xB3: ("OTIR", 2), 0xAB: ("OUTD", 2), 0xBB: ("OTDR", 2),
            0x42: ("SBC HL,BC", 2), 0x52: ("SBC HL,DE", 2),
            0x62: ("SBC HL,HL", 2), 0x72: ("SBC HL,SP", 2),
            0x4A: ("ADC HL,BC", 2), 0x5A: ("ADC HL,DE", 2),
            0x6A: ("ADC HL,HL", 2), 0x7A: ("ADC HL,SP", 2),
            0x43: (f"LD (0x{word(2):04X}),BC", 4), 0x53: (f"LD (0x{word(2):04X}),DE", 4),
            0x63: (f"LD (0x{word(2):04X}),HL", 4), 0x73: (f"LD (0x{word(2):04X}),SP", 4),
            0x4B: (f"LD BC,(0x{word(2):04X})", 4), 0x5B: (f"LD DE,(0x{word(2):04X})", 4),
            0x6B: (f"LD HL,(0x{word(2):04X})", 4), 0x7B: (f"LD SP,(0x{word(2):04X})", 4),
        }
        if b2 in ed:
            mn, sz = ed[b2]
            return (mn, 1 + sz)
        return (f"ED 0x{b2:02X}", 2)
    # CB prefix (bit operations) — just show raw
    if b == 0xCB:
        b2 = byte(1)
        ops = ["RLC","RRC","RL","RR","SLA","SRA","SLL","SRL"]
        regs = ["B","C","D","E","H","L","(HL)","A"]
        op = ops[(b2 >> 3) & 7]
        reg = regs[b2 & 7]
        if b2 < 0x40: return (f"{op} {reg}", 2)
        if b2 < 0x80: return (f"BIT {(b2>>3)&7},{reg}", 2)
        if b2 < 0xC0: return (f"RES {(b2>>3)&7},{reg}", 2)
        return (f"SET {(b2>>3)&7},{reg}", 2)
    # DD prefix (IX)
    if b == 0xDD:
        b2 = byte(1)
        if b2 == 0x21: return (f"LD IX,0x{word(2):04X}", 4)
        if b2 == 0x22: return (f"LD (0x{word(2):04X}),IX", 4)
        if b2 == 0x2A: return (f"LD IX,(0x{word(2):04X})", 4)
        if b2 == 0xE5: return ("PUSH IX", 2)
        if b2 == 0xE1: return ("POP IX", 2)
        if b2 == 0xE9: return ("JP (IX)", 2)
        if b2 == 0x23: return ("INC IX", 2)
        if b2 == 0x2B: return ("DEC IX", 2)
        if b2 == 0x09: return ("ADD IX,BC", 2)
        if b2 == 0x19: return ("ADD IX,DE", 2)
        if b2 == 0x29: return ("ADD IX,IX", 2)
        if b2 == 0x39: return ("ADD IX,SP", 2)
        if b2 == 0x7E: d = byte(2); return (f"LD A,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x77: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),A", 3)
        if b2 == 0x46: d = byte(2); return (f"LD B,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x4E: d = byte(2); return (f"LD C,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x56: d = byte(2); return (f"LD D,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x5E: d = byte(2); return (f"LD E,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x66: d = byte(2); return (f"LD H,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x6E: d = byte(2); return (f"LD L,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x36: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),0x{byte(3):02X}", 4)
        if b2 == 0x34: d = byte(2); return (f"INC (IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x35: d = byte(2); return (f"DEC (IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x70: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),B", 3)
        if b2 == 0x71: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),C", 3)
        if b2 == 0x72: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),D", 3)
        if b2 == 0x73: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),E", 3)
        if b2 == 0x74: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),H", 3)
        if b2 == 0x75: d = byte(2); return (f"LD (IX{d-256 if d>127 else d:+d}),L", 3)
        if b2 == 0x86: d = byte(2); return (f"ADD A,(IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0xBE: d = byte(2); return (f"CP (IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0xB6: d = byte(2); return (f"OR (IX{d-256 if d>127 else d:+d})", 3)
        if b2 == 0xA6: d = byte(2); return (f"AND (IX{d-256 if d>127 else d:+d})", 3)
        return (f"DD 0x{b2:02X}", 2)
    # FD prefix (IY)
    if b == 0xFD:
        b2 = byte(1)
        if b2 == 0x21: return (f"LD IY,0x{word(2):04X}", 4)
        if b2 == 0x22: return (f"LD (0x{word(2):04X}),IY", 4)
        if b2 == 0x2A: return (f"LD IY,(0x{word(2):04X})", 4)
        if b2 == 0xE5: return ("PUSH IY", 2)
        if b2 == 0xE1: return ("POP IY", 2)
        if b2 == 0xE9: return ("JP (IY)", 2)
        if b2 == 0x23: return ("INC IY", 2)
        if b2 == 0x2B: return ("DEC IY", 2)
        if b2 == 0x09: return ("ADD IY,BC", 2)
        if b2 == 0x19: return ("ADD IY,DE", 2)
        if b2 == 0x29: return ("ADD IY,IY", 2)
        if b2 == 0x39: return ("ADD IY,SP", 2)
        if b2 == 0x7E: d = byte(2); return (f"LD A,(IY{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x77: d = byte(2); return (f"LD (IY{d-256 if d>127 else d:+d}),A", 3)
        if b2 == 0x5E: d = byte(2); return (f"LD E,(IY{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x4E: d = byte(2); return (f"LD C,(IY{d-256 if d>127 else d:+d})", 3)
        if b2 == 0x56: d = byte(2); return (f"LD D,(IY{d-256 if d>127 else d:+d})", 3)
        if b2 == 0xB6: d = byte(2); return (f"OR (IY{d-256 if d>127 else d:+d})", 3)
        if b2 == 0xA6: d = byte(2); return (f"AND (IY{d-256 if d>127 else d:+d})", 3)
        if b2 == 0xBE: d = byte(2); return (f"CP (IY{d-256 if d>127 else d:+d})", 3)
        return (f"FD 0x{b2:02X}", 2)
    # IN/OUT
    if b == 0xDB: return (f"IN A,(0x{byte(1):02X})", 2)
    if b == 0xD3: return (f"OUT (0x{byte(1):02X}),A", 2)
    return (f"DB 0x{b:02X}", 1)

def disasm(data, base, length=256, label=""):
    if label:
        print(f"\n{'='*60}")
        print(f"  Disassembly: {label}  (base=0x{base:04X})")
        print(f"{'='*60}")
    pc = 0
    while pc < length:
        raw_hex = " ".join(f"{data[pc+i]:02X}" for i in range(min(4, length-pc)))
        try:
            mn, sz = disasm_one(data, pc)
        except Exception:
            mn, sz = f"?? 0x{data[pc]:02X}", 1
        print(f"  {base+pc:04X}: {raw_hex:<12} {mn}")
        pc += sz

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    with open(DISK, 'rb') as f:
        disk = f.read()

    print(f"Disk size: {len(disk)} bytes  ({len(disk)//256} sectors)")
    print()

    # ---- Dump T20/S5-S8 raw ----
    for s in range(5, 9):
        sec = read_sector(disk, 20, s)
        print(f"--- T20/S{s} raw bytes ---")
        for row in range(0, 256, 16):
            hex_part = " ".join(f"{b:02X}" for b in sec[row:row+16])
            asc_part = "".join(chr(b) if 0x20 <= b < 0x7F else '.' for b in sec[row:row+16])
            print(f"  {(0x4200+row):04X}: {hex_part:<48}  {asc_part}")
        print()

        # Interpret first 2 bytes as load address (both endians)
        le_addr = sec[0] | (sec[1] << 8)
        be_addr = (sec[0] << 8) | sec[1]
        deleted = (s == 5 and sec[0] == 0x1F) or (20 == 17 and s >= 2)
        print(f"  Byte[0..1] as LE addr: 0x{le_addr:04X}  ({('HIGH' if le_addr > 0x7FFF else 'low')})")
        print(f"  Byte[0..1] as BE addr: 0x{be_addr:04X}  ({('HIGH' if be_addr > 0x7FFF else 'low')})")
        if sec[0] == 0x1F:
            name = bytes(sec[2:10]).decode('ascii', errors='replace').rstrip('\x00')
            load_be = (sec[10] << 8) | sec[11]
            load_le = sec[10] | (sec[11] << 8)
            print(f"  HEADER format: len=0x{sec[1]:02X} name='{name}' load_be=0x{load_be:04X} load_le=0x{load_le:04X}")
        print()

    # ---- Figure out where the "Copyrigh" module code sectors land in a real boot ----
    # T20/S6: if it's a LDOS /SYS-format sector, bytes[0..1] = load address (big-endian per loader?)
    # Try treating all of T20/S6-S8 as continuous code following the header
    print("--- T20/S6 interpreted as raw Z80 code (base=0xCB47 BE from header?) ---")
    s6 = read_sector(disk, 20, 6)
    # The load address for code sectors in LDOS /SYS format: big-endian in bytes[0..1]
    load6_be = (s6[0] << 8) | s6[1]
    load6_le = s6[0] | (s6[1] << 8)
    print(f"  First 2 bytes: 0x{s6[0]:02X} 0x{s6[1]:02X}  → BE=0x{load6_be:04X}  LE=0x{load6_le:04X}")
    # Disassemble treating the entire sector as code at both possible base addresses
    print(f"\n  Disasm assuming base=0x{load6_be:04X} (BE):")
    disasm(s6[2:], load6_be + 2, min(64, 254))
    print(f"\n  Disasm assuming base=0x{load6_le:04X} (LE), code starts at offset 2:")
    disasm(s6[2:], load6_le + 2, min(64, 254))

    # ---- Disassemble T00/S2 (first sector read by second-stage at 0x4777) ----
    print("\n--- T00/S2 raw bytes (first sector second-stage reads) ---")
    s00_2 = read_sector(disk, 0, 2)
    for row in range(0, 64, 16):
        hex_part = " ".join(f"{b:02X}" for b in s00_2[row:row+16])
        print(f"  {row:02X}: {hex_part}")
    disasm(s00_2, 0x0000, 64, "T00/S2 as code from offset 0")

    # ---- Also look at what's on T00/S0 (boot sector) first 32 bytes ----
    print("\n--- T17/S8 (2nd stage config disk sector?) first 64 bytes ---")
    t17s8 = read_sector(disk, 17, 8)
    for row in range(0, 64, 16):
        hex_part = " ".join(f"{b:02X}" for b in t17s8[row:row+16])
        print(f"  {(0x5F00+row):04X}: {hex_part}")

if __name__ == '__main__':
    main()

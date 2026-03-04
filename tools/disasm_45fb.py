#!/usr/bin/env python3
"""
Disassemble code at 0x45FB from [4777RET] dump in log.txt.
This is the LDOS second-stage module installer entry point.
"""
import sys
import re

log_path = sys.argv[1] if len(sys.argv) > 1 else "log.txt"

mem = {}
with open(log_path) as f:
    for line in f:
        m = re.match(r'\[4777RET\]\s+([0-9A-Fa-f]+):\s+(.+)', line)
        if m:
            addr = int(m.group(1), 16)
            for i, b in enumerate(m.group(2).split()):
                mem[addr + i] = int(b, 16)

print(f"Loaded {len(mem)} bytes from [4777RET] dump")
print(f"Address range: {min(mem):#06x} - {max(mem):#06x}")
print()

def g(pc, offset):
    return mem.get(pc + offset, 0)

def disasm(start, maxinsns=80):
    pc = start
    count = 0
    while count < maxinsns and pc in mem:
        b = mem[pc]
        insn = None
        length = 1

        if b == 0x00: insn = 'NOP'
        elif b == 0x76: insn = 'HALT'
        elif b == 0xF3: insn = 'DI'
        elif b == 0xFB: insn = 'EI'
        elif b == 0xEB: insn = 'EX DE,HL'
        elif b == 0xE3: insn = 'EX (SP),HL'
        elif b == 0x08: insn = "EX AF,AF'"
        elif b == 0xD9: insn = 'EXX'
        elif b == 0x07: insn = 'RLCA'
        elif b == 0x0F: insn = 'RRCA'
        elif b == 0x17: insn = 'RLA'
        elif b == 0x1F: insn = 'RRA'
        elif b == 0x37: insn = 'SCF'
        elif b == 0x3F: insn = 'CCF'
        elif b == 0x2F: insn = 'CPL'
        elif b == 0x27: insn = 'DAA'
        elif b == 0xF9: insn = 'LD SP,HL'

        # 8-bit loads
        elif b == 0x3E: insn = f'LD A,{g(pc,1):#04x}'; length = 2
        elif b == 0x06: insn = f'LD B,{g(pc,1):#04x}'; length = 2
        elif b == 0x0E: insn = f'LD C,{g(pc,1):#04x}'; length = 2
        elif b == 0x16: insn = f'LD D,{g(pc,1):#04x}'; length = 2
        elif b == 0x1E: insn = f'LD E,{g(pc,1):#04x}'; length = 2
        elif b == 0x26: insn = f'LD H,{g(pc,1):#04x}'; length = 2
        elif b == 0x2E: insn = f'LD L,{g(pc,1):#04x}'; length = 2
        elif b == 0x3A: insn = f'LD A,({g(pc,2)<<8|g(pc,1):#06x})'; length = 3
        elif b == 0x32: insn = f'LD ({g(pc,2)<<8|g(pc,1):#06x}),A'; length = 3
        elif b == 0x7E: insn = 'LD A,(HL)'
        elif b == 0x7F: insn = 'LD A,A'
        elif b == 0x78: insn = 'LD A,B'
        elif b == 0x79: insn = 'LD A,C'
        elif b == 0x7A: insn = 'LD A,D'
        elif b == 0x7B: insn = 'LD A,E'
        elif b == 0x7C: insn = 'LD A,H'
        elif b == 0x7D: insn = 'LD A,L'
        elif b == 0x47: insn = 'LD B,A'
        elif b == 0x4F: insn = 'LD C,A'
        elif b == 0x57: insn = 'LD D,A'
        elif b == 0x5F: insn = 'LD E,A'
        elif b == 0x67: insn = 'LD H,A'
        elif b == 0x6F: insn = 'LD L,A'
        elif b == 0x77: insn = 'LD (HL),A'
        elif b == 0x70: insn = 'LD (HL),B'
        elif b == 0x71: insn = 'LD (HL),C'
        elif b == 0x72: insn = 'LD (HL),D'
        elif b == 0x73: insn = 'LD (HL),E'
        elif b == 0x36: insn = f'LD (HL),{g(pc,1):#04x}'; length = 2
        elif b == 0x12: insn = 'LD (DE),A'
        elif b == 0x02: insn = 'LD (BC),A'
        elif b == 0x1A: insn = 'LD A,(DE)'
        elif b == 0x0A: insn = 'LD A,(BC)'
        elif b == 0x46: insn = 'LD B,(HL)'
        elif b == 0x4E: insn = 'LD C,(HL)'
        elif b == 0x56: insn = 'LD D,(HL)'
        elif b == 0x5E: insn = 'LD E,(HL)'
        elif b == 0x66: insn = 'LD H,(HL)'
        elif b == 0x6E: insn = 'LD L,(HL)'
        elif b == 0x40: insn = 'LD B,B'
        elif b == 0x41: insn = 'LD B,C'
        elif b == 0x42: insn = 'LD B,D'
        elif b == 0x43: insn = 'LD B,E'
        elif b == 0x44: insn = 'LD B,H'
        elif b == 0x45: insn = 'LD B,L'
        elif b == 0x48: insn = 'LD C,B'
        elif b == 0x49: insn = 'LD C,C'
        elif b == 0x4A: insn = 'LD C,D'
        elif b == 0x4B: insn = 'LD C,E'
        elif b == 0x4C: insn = 'LD C,H'
        elif b == 0x4D: insn = 'LD C,L'
        elif b == 0x50: insn = 'LD D,B'
        elif b == 0x51: insn = 'LD D,C'
        elif b == 0x52: insn = 'LD D,D'
        elif b == 0x53: insn = 'LD D,E'
        elif b == 0x54: insn = 'LD D,H'
        elif b == 0x55: insn = 'LD D,L'
        elif b == 0x58: insn = 'LD E,B'
        elif b == 0x59: insn = 'LD E,C'
        elif b == 0x5A: insn = 'LD E,D'
        elif b == 0x5B: insn = 'LD E,E'
        elif b == 0x5C: insn = 'LD E,H'
        elif b == 0x5D: insn = 'LD E,L'
        elif b == 0x60: insn = 'LD H,B'
        elif b == 0x61: insn = 'LD H,C'
        elif b == 0x62: insn = 'LD H,D'
        elif b == 0x63: insn = 'LD H,E'
        elif b == 0x64: insn = 'LD H,H'
        elif b == 0x65: insn = 'LD H,L'
        elif b == 0x68: insn = 'LD L,B'
        elif b == 0x69: insn = 'LD L,C'
        elif b == 0x6A: insn = 'LD L,D'
        elif b == 0x6B: insn = 'LD L,E'
        elif b == 0x6C: insn = 'LD L,H'
        elif b == 0x6D: insn = 'LD L,L'

        # 16-bit loads
        elif b == 0x01: insn = f'LD BC,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0x11: insn = f'LD DE,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0x21: insn = f'LD HL,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0x31: insn = f'LD SP,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0x2A: insn = f'LD HL,({g(pc,2)<<8|g(pc,1):#06x})'; length = 3
        elif b == 0x22: insn = f'LD ({g(pc,2)<<8|g(pc,1):#06x}),HL'; length = 3

        # Stack
        elif b == 0xF5: insn = 'PUSH AF'
        elif b == 0xC5: insn = 'PUSH BC'
        elif b == 0xD5: insn = 'PUSH DE'
        elif b == 0xE5: insn = 'PUSH HL'
        elif b == 0xF1: insn = 'POP AF'
        elif b == 0xC1: insn = 'POP BC'
        elif b == 0xD1: insn = 'POP DE'
        elif b == 0xE1: insn = 'POP HL'

        # Jumps
        elif b == 0xC3: insn = f'JP {g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xCA: insn = f'JP Z,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xC2: insn = f'JP NZ,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xDA: insn = f'JP C,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xD2: insn = f'JP NC,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xEA: insn = f'JP PE,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xE2: insn = f'JP PO,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xFA: insn = f'JP M,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xF2: insn = f'JP P,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xE9: insn = 'JP (HL)'
        elif b == 0x18: o = g(pc,1); o = o-256 if o>=128 else o; insn = f'JR {pc+2+o:#06x}'; length = 2
        elif b == 0x20: o = g(pc,1); o = o-256 if o>=128 else o; insn = f'JR NZ,{pc+2+o:#06x}'; length = 2
        elif b == 0x28: o = g(pc,1); o = o-256 if o>=128 else o; insn = f'JR Z,{pc+2+o:#06x}'; length = 2
        elif b == 0x30: o = g(pc,1); o = o-256 if o>=128 else o; insn = f'JR NC,{pc+2+o:#06x}'; length = 2
        elif b == 0x38: o = g(pc,1); o = o-256 if o>=128 else o; insn = f'JR C,{pc+2+o:#06x}'; length = 2
        elif b == 0x10: o = g(pc,1); o = o-256 if o>=128 else o; insn = f'DJNZ {pc+2+o:#06x}'; length = 2

        # Calls/Ret
        elif b == 0xCD: insn = f'CALL {g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xCC: insn = f'CALL Z,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xC4: insn = f'CALL NZ,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xDC: insn = f'CALL C,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xD4: insn = f'CALL NC,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xEC: insn = f'CALL PE,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xE4: insn = f'CALL PO,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xFC: insn = f'CALL M,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xF4: insn = f'CALL P,{g(pc,2)<<8|g(pc,1):#06x}'; length = 3
        elif b == 0xC9: insn = 'RET'
        elif b == 0xC8: insn = 'RET Z'
        elif b == 0xC0: insn = 'RET NZ'
        elif b == 0xD8: insn = 'RET C'
        elif b == 0xD0: insn = 'RET NC'
        elif b == 0xE8: insn = 'RET PE'
        elif b == 0xE0: insn = 'RET PO'
        elif b == 0xF8: insn = 'RET M'
        elif b == 0xF0: insn = 'RET P'

        # RST
        elif b in (0xC7,0xCF,0xD7,0xDF,0xE7,0xEF,0xF7,0xFF):
            insn = f'RST {b & 0x38:#04x}'

        # ALU
        elif b == 0x87: insn = 'ADD A,A'
        elif b == 0x80: insn = 'ADD A,B'
        elif b == 0x81: insn = 'ADD A,C'
        elif b == 0x82: insn = 'ADD A,D'
        elif b == 0x83: insn = 'ADD A,E'
        elif b == 0x84: insn = 'ADD A,H'
        elif b == 0x85: insn = 'ADD A,L'
        elif b == 0x86: insn = 'ADD A,(HL)'
        elif b == 0xC6: insn = f'ADD A,{g(pc,1):#04x}'; length = 2
        elif b == 0x8F: insn = 'ADC A,A'
        elif b == 0x88: insn = 'ADC A,B'
        elif b == 0x89: insn = 'ADC A,C'
        elif b == 0x8A: insn = 'ADC A,D'
        elif b == 0x8B: insn = 'ADC A,E'
        elif b == 0x8C: insn = 'ADC A,H'
        elif b == 0x8D: insn = 'ADC A,L'
        elif b == 0x8E: insn = 'ADC A,(HL)'
        elif b == 0xCE: insn = f'ADC A,{g(pc,1):#04x}'; length = 2
        elif b == 0x97: insn = 'SUB A'
        elif b == 0x90: insn = 'SUB B'
        elif b == 0x91: insn = 'SUB C'
        elif b == 0x92: insn = 'SUB D'
        elif b == 0x93: insn = 'SUB E'
        elif b == 0x94: insn = 'SUB H'
        elif b == 0x95: insn = 'SUB L'
        elif b == 0x96: insn = 'SUB (HL)'
        elif b == 0xD6: insn = f'SUB {g(pc,1):#04x}'; length = 2
        elif b == 0x9F: insn = 'SBC A,A'
        elif b == 0x98: insn = 'SBC A,B'
        elif b == 0x99: insn = 'SBC A,C'
        elif b == 0x9A: insn = 'SBC A,D'
        elif b == 0x9B: insn = 'SBC A,E'
        elif b == 0x9C: insn = 'SBC A,H'
        elif b == 0x9D: insn = 'SBC A,L'
        elif b == 0x9E: insn = 'SBC A,(HL)'
        elif b == 0xDE: insn = f'SBC A,{g(pc,1):#04x}'; length = 2
        elif b == 0xA7: insn = 'AND A'
        elif b == 0xA0: insn = 'AND B'
        elif b == 0xA1: insn = 'AND C'
        elif b == 0xA2: insn = 'AND D'
        elif b == 0xA3: insn = 'AND E'
        elif b == 0xA4: insn = 'AND H'
        elif b == 0xA5: insn = 'AND L'
        elif b == 0xA6: insn = 'AND (HL)'
        elif b == 0xE6: insn = f'AND {g(pc,1):#04x}'; length = 2
        elif b == 0xB7: insn = 'OR A'
        elif b == 0xB0: insn = 'OR B'
        elif b == 0xB1: insn = 'OR C'
        elif b == 0xB2: insn = 'OR D'
        elif b == 0xB3: insn = 'OR E'
        elif b == 0xB4: insn = 'OR H'
        elif b == 0xB5: insn = 'OR L'
        elif b == 0xB6: insn = 'OR (HL)'
        elif b == 0xF6: insn = f'OR {g(pc,1):#04x}'; length = 2
        elif b == 0xAF: insn = 'XOR A'
        elif b == 0xA8: insn = 'XOR B'
        elif b == 0xA9: insn = 'XOR C'
        elif b == 0xAA: insn = 'XOR D'
        elif b == 0xAB: insn = 'XOR E'
        elif b == 0xAC: insn = 'XOR H'
        elif b == 0xAD: insn = 'XOR L'
        elif b == 0xAE: insn = 'XOR (HL)'
        elif b == 0xEE: insn = f'XOR {g(pc,1):#04x}'; length = 2
        elif b == 0xBF: insn = 'CP A'
        elif b == 0xB8: insn = 'CP B'
        elif b == 0xB9: insn = 'CP C'
        elif b == 0xBA: insn = 'CP D'
        elif b == 0xBB: insn = 'CP E'
        elif b == 0xBC: insn = 'CP H'
        elif b == 0xBD: insn = 'CP L'
        elif b == 0xBE: insn = 'CP (HL)'
        elif b == 0xFE: insn = f'CP {g(pc,1):#04x}'; length = 2

        # Inc/Dec 8
        elif b == 0x3C: insn = 'INC A'
        elif b == 0x04: insn = 'INC B'
        elif b == 0x0C: insn = 'INC C'
        elif b == 0x14: insn = 'INC D'
        elif b == 0x1C: insn = 'INC E'
        elif b == 0x24: insn = 'INC H'
        elif b == 0x2C: insn = 'INC L'
        elif b == 0x34: insn = 'INC (HL)'
        elif b == 0x3D: insn = 'DEC A'
        elif b == 0x05: insn = 'DEC B'
        elif b == 0x0D: insn = 'DEC C'
        elif b == 0x15: insn = 'DEC D'
        elif b == 0x1D: insn = 'DEC E'
        elif b == 0x25: insn = 'DEC H'
        elif b == 0x2D: insn = 'DEC L'
        elif b == 0x35: insn = 'DEC (HL)'

        # Inc/Dec 16
        elif b == 0x03: insn = 'INC BC'
        elif b == 0x13: insn = 'INC DE'
        elif b == 0x23: insn = 'INC HL'
        elif b == 0x33: insn = 'INC SP'
        elif b == 0x0B: insn = 'DEC BC'
        elif b == 0x1B: insn = 'DEC DE'
        elif b == 0x2B: insn = 'DEC HL'
        elif b == 0x3B: insn = 'DEC SP'

        # ADD HL,rr
        elif b == 0x09: insn = 'ADD HL,BC'
        elif b == 0x19: insn = 'ADD HL,DE'
        elif b == 0x29: insn = 'ADD HL,HL'
        elif b == 0x39: insn = 'ADD HL,SP'

        # I/O
        elif b == 0xD3: insn = f'OUT ({g(pc,1):#04x}),A'; length = 2
        elif b == 0xDB: insn = f'IN A,({g(pc,1):#04x})'; length = 2

        # DD prefix (IX)
        elif b == 0xDD:
            b2 = g(pc, 1)
            if b2 == 0xE9: insn = 'JP (IX)'; length = 2
            elif b2 == 0xE5: insn = 'PUSH IX'; length = 2
            elif b2 == 0xE1: insn = 'POP IX'; length = 2
            elif b2 == 0xF9: insn = 'LD SP,IX'; length = 2
            elif b2 == 0x21: insn = f'LD IX,{g(pc,3)<<8|g(pc,2):#06x}'; length = 4
            elif b2 == 0x2A: insn = f'LD IX,({g(pc,3)<<8|g(pc,2):#06x})'; length = 4
            elif b2 == 0x22: insn = f'LD ({g(pc,3)<<8|g(pc,2):#06x}),IX'; length = 4
            elif b2 == 0x23: insn = 'INC IX'; length = 2
            elif b2 == 0x2B: insn = 'DEC IX'; length = 2
            elif b2 == 0x09: insn = 'ADD IX,BC'; length = 2
            elif b2 == 0x19: insn = 'ADD IX,DE'; length = 2
            elif b2 == 0x29: insn = 'ADD IX,IX'; length = 2
            elif b2 == 0x39: insn = 'ADD IX,SP'; length = 2
            elif b2 in (0x46,0x4E,0x56,0x5E,0x66,0x6E,0x7E):
                regs = {0x46:'B', 0x4E:'C', 0x56:'D', 0x5E:'E', 0x66:'H', 0x6E:'L', 0x7E:'A'}
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'LD {regs[b2]},(IX{d_s:+d})'; length = 3
            elif b2 in (0x70,0x71,0x72,0x73,0x74,0x75,0x77):
                regs = {0x70:'B', 0x71:'C', 0x72:'D', 0x73:'E', 0x74:'H', 0x75:'L', 0x77:'A'}
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'LD (IX{d_s:+d}),{regs[b2]}'; length = 3
            elif b2 == 0x36:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'LD (IX{d_s:+d}),{g(pc,3):#04x}'; length = 4
            elif b2 == 0x35:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'DEC (IX{d_s:+d})'; length = 3
            elif b2 == 0x34:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'INC (IX{d_s:+d})'; length = 3
            elif b2 == 0xCB:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                op = g(pc,3)
                insn = f'IX-CB op={op:#04x} (IX{d_s:+d})'; length = 4
            else:
                insn = f'DD {b2:#04x}'; length = 2

        # FD prefix (IY)
        elif b == 0xFD:
            b2 = g(pc, 1)
            if b2 == 0xE9: insn = 'JP (IY)'; length = 2
            elif b2 == 0xE5: insn = 'PUSH IY'; length = 2
            elif b2 == 0xE1: insn = 'POP IY'; length = 2
            elif b2 == 0xF9: insn = 'LD SP,IY'; length = 2
            elif b2 == 0x21: insn = f'LD IY,{g(pc,3)<<8|g(pc,2):#06x}'; length = 4
            elif b2 == 0x2A: insn = f'LD IY,({g(pc,3)<<8|g(pc,2):#06x})'; length = 4
            elif b2 == 0x22: insn = f'LD ({g(pc,3)<<8|g(pc,2):#06x}),IY'; length = 4
            elif b2 == 0x23: insn = 'INC IY'; length = 2
            elif b2 == 0x2B: insn = 'DEC IY'; length = 2
            elif b2 == 0x09: insn = 'ADD IY,BC'; length = 2
            elif b2 == 0x19: insn = 'ADD IY,DE'; length = 2
            elif b2 == 0x29: insn = 'ADD IY,IY'; length = 2
            elif b2 == 0x39: insn = 'ADD IY,SP'; length = 2
            elif b2 in (0x46,0x4E,0x56,0x5E,0x66,0x6E,0x7E):
                regs = {0x46:'B', 0x4E:'C', 0x56:'D', 0x5E:'E', 0x66:'H', 0x6E:'L', 0x7E:'A'}
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'LD {regs[b2]},(IY{d_s:+d})'; length = 3
            elif b2 in (0x70,0x71,0x72,0x73,0x74,0x75,0x77):
                regs = {0x70:'B', 0x71:'C', 0x72:'D', 0x73:'E', 0x74:'H', 0x75:'L', 0x77:'A'}
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'LD (IY{d_s:+d}),{regs[b2]}'; length = 3
            elif b2 == 0x36:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'LD (IY{d_s:+d}),{g(pc,3):#04x}'; length = 4
            elif b2 == 0x35:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'DEC (IY{d_s:+d})'; length = 3
            elif b2 == 0x34:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'INC (IY{d_s:+d})'; length = 3
            elif b2 == 0x86:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'ADD A,(IY{d_s:+d})'; length = 3
            elif b2 == 0xB6:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'OR (IY{d_s:+d})'; length = 3
            elif b2 == 0xBE:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'CP (IY{d_s:+d})'; length = 3
            elif b2 == 0xA6:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                insn = f'AND (IY{d_s:+d})'; length = 3
            elif b2 == 0xCB:
                d = g(pc,2); d_s = d-256 if d>=128 else d
                op = g(pc,3)
                insn = f'IY-CB op={op:#04x} (IY{d_s:+d})'; length = 4
            else:
                insn = f'FD {b2:#04x}'; length = 2

        # ED prefix
        elif b == 0xED:
            b2 = g(pc, 1)
            if b2 == 0xB0: insn = 'LDIR'; length = 2
            elif b2 == 0xB8: insn = 'LDDR'; length = 2
            elif b2 == 0xA0: insn = 'LDI'; length = 2
            elif b2 == 0xA8: insn = 'LDD'; length = 2
            elif b2 == 0xB1: insn = 'CPIR'; length = 2
            elif b2 == 0xB9: insn = 'CPDR'; length = 2
            elif b2 == 0xA1: insn = 'CPI'; length = 2
            elif b2 == 0xA9: insn = 'CPD'; length = 2
            elif b2 == 0x56: insn = 'IM 1'; length = 2
            elif b2 == 0x5E: insn = 'IM 2'; length = 2
            elif b2 == 0x46: insn = 'IM 0'; length = 2
            elif b2 == 0x47: insn = 'LD I,A'; length = 2
            elif b2 == 0x4F: insn = 'LD R,A'; length = 2
            elif b2 == 0x57: insn = 'LD A,I'; length = 2
            elif b2 == 0x5F: insn = 'LD A,R'; length = 2
            elif b2 == 0x45: insn = 'RETN'; length = 2
            elif b2 == 0x4D: insn = 'RETI'; length = 2
            elif b2 == 0x78: insn = 'IN A,(C)'; length = 2
            elif b2 == 0x79: insn = 'OUT (C),A'; length = 2
            elif b2 == 0x40: insn = 'IN B,(C)'; length = 2
            elif b2 == 0x41: insn = 'OUT (C),B'; length = 2
            elif b2 == 0x48: insn = 'IN C,(C)'; length = 2
            elif b2 == 0x49: insn = 'OUT (C),C'; length = 2
            elif b2 == 0x50: insn = 'IN D,(C)'; length = 2
            elif b2 == 0x51: insn = 'OUT (C),D'; length = 2
            elif b2 == 0x58: insn = 'IN E,(C)'; length = 2
            elif b2 == 0x59: insn = 'OUT (C),E'; length = 2
            elif b2 == 0x60: insn = 'IN H,(C)'; length = 2
            elif b2 == 0x61: insn = 'OUT (C),H'; length = 2
            elif b2 == 0x68: insn = 'IN L,(C)'; length = 2
            elif b2 == 0x69: insn = 'OUT (C),L'; length = 2
            elif b2 == 0x4B:
                insn = f'LD BC,({g(pc,3)<<8|g(pc,2):#06x})'; length = 4
            elif b2 == 0x5B:
                insn = f'LD DE,({g(pc,3)<<8|g(pc,2):#06x})'; length = 4
            elif b2 == 0x6B:
                insn = f'LD HL,({g(pc,3)<<8|g(pc,2):#06x})'; length = 4
            elif b2 == 0x7B:
                insn = f'LD SP,({g(pc,3)<<8|g(pc,2):#06x})'; length = 4
            elif b2 == 0x43:
                insn = f'LD ({g(pc,3)<<8|g(pc,2):#06x}),BC'; length = 4
            elif b2 == 0x53:
                insn = f'LD ({g(pc,3)<<8|g(pc,2):#06x}),DE'; length = 4
            elif b2 == 0x63:
                insn = f'LD ({g(pc,3)<<8|g(pc,2):#06x}),HL'; length = 4
            elif b2 == 0x73:
                insn = f'LD ({g(pc,3)<<8|g(pc,2):#06x}),SP'; length = 4
            elif b2 == 0x42: insn = 'SBC HL,BC'; length = 2
            elif b2 == 0x52: insn = 'SBC HL,DE'; length = 2
            elif b2 == 0x62: insn = 'SBC HL,HL'; length = 2
            elif b2 == 0x72: insn = 'SBC HL,SP'; length = 2
            elif b2 == 0x4A: insn = 'ADC HL,BC'; length = 2
            elif b2 == 0x5A: insn = 'ADC HL,DE'; length = 2
            elif b2 == 0x6A: insn = 'ADC HL,HL'; length = 2
            elif b2 == 0x7A: insn = 'ADC HL,SP'; length = 2
            elif b2 == 0xBB: insn = 'OTDR'; length = 2
            elif b2 == 0xB3: insn = 'OTIR'; length = 2
            else: insn = f'ED {b2:#04x}'; length = 2

        # CB prefix
        elif b == 0xCB:
            b2 = g(pc, 1)
            ops = ['RLC','RRC','RL','RR','SLA','SRA','SLL','SRL']
            regs = ['B','C','D','E','H','L','(HL)','A']
            if b2 < 0x40:
                insn = f'{ops[b2>>3]} {regs[b2&7]}'; length = 2
            elif b2 < 0x80:
                insn = f'BIT {(b2-0x40)>>3},{regs[b2&7]}'; length = 2
            elif b2 < 0xC0:
                insn = f'RES {(b2-0x80)>>3},{regs[b2&7]}'; length = 2
            else:
                insn = f'SET {(b2-0xC0)>>3},{regs[b2&7]}'; length = 2

        else:
            insn = f'DB {b:#04x}'

        if insn is None:
            insn = f'???'
        
        raw = ' '.join(f'{mem.get(pc+i, 0):02X}' for i in range(length))
        print(f'{pc:#06x}: {raw:<12} {insn}')
        pc += length
        count += 1
    if pc not in mem:
        print(f'  (no data at {pc:#06x})')

# First disassemble from 0x45FB (entry from JP in table)
print("=== 0x45FB entry (called from 0x4700 entry table JP) ===")
disasm(0x45FB, 60)

print()
print("=== 0x4538 area (what calls 0x45FB and related) ===")
disasm(0x4535, 40)

print()
print("=== 0x478F (IY entry table lookup) ===")
disasm(0x478F, 30)

print()
print("=== 0x4777 (second-stage main) ===")
disasm(0x4777, 20)

# 0x466b is where B=9 jumps to from 0x45FB
print()
print("=== 0x466b (JR NC from 0x45FB when A=B=9) ===")
disasm(0x466b, 60)

# 0x469a is the FDC read subroutine called from 0x466b
print()
print("=== 0x469a (FDC sector read called from 0x466b) ===")
disasm(0x469a, 60)

# Check what's in the 0x4750 area
print()
print("=== 0x4750 (called from 0x4552) ===")
disasm(0x4750, 30)

# Also look at 0x4547 / sub called
print()
print("=== 0x4547 (JP back from 0x4538) ===")
disasm(0x4547, 25)

# Check 0x452b (JR target)
print()
print("=== 0x452b (JR 0x452b from 0x454d) ===")
disasm(0x452b, 30)

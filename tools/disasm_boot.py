#!/usr/bin/env python3
"""
Disassemble the LDOS 5.3.1 boot sector (T00/S0), loaded at 0x4200.
Also parses T09/S0 to show the module directory records.
"""
import sys

DISK = "disks/ld1-531.dsk"

with open(DISK, "rb") as f:
    disk = bytearray(f.read())

def sector(t, s):
    return disk[(t * 10 + s) * 256 : (t * 10 + s + 1) * 256]

# ─────────────────────────────────────────────────────
# Minimal Z80 disassembler
# ─────────────────────────────────────────────────────
def disasm(mem, base, length=None):
    """Yield (addr, hex_str, mnemonic) for each instruction."""
    pc = 0
    end = length if length is not None else len(mem)

    def u8(o):  return mem[o]
    def u16(o): return mem[o] | (mem[o+1] << 8)
    def s8(o):  v = mem[o]; return v - 256 if v >= 128 else v
    def fmt_abs(o):  return f"0x{(base + pc + 2 + s8(o)) & 0xFFFF:04X}"

    r8  = ['B','C','D','E','H','L','(HL)','A']
    r16 = ['BC','DE','HL','SP']

    while pc < end:
        op   = mem[pc]
        addr = base + pc
        start_pc = pc

        def finish(size, mnem):
            nonlocal pc
            raw = ' '.join(f'{mem[start_pc+i]:02X}' for i in range(size))
            pc += size
            return (addr, raw, mnem)

        if   op == 0x00: yield finish(1, 'NOP')
        elif op == 0x01: yield finish(3, f'LD BC,0x{u16(pc+1):04X}')
        elif op == 0x02: yield finish(1, 'LD (BC),A')
        elif op == 0x03: yield finish(1, 'INC BC')
        elif op == 0x04: yield finish(1, 'INC B')
        elif op == 0x05: yield finish(1, 'DEC B')
        elif op == 0x06: yield finish(2, f'LD B,0x{u8(pc+1):02X}')
        elif op == 0x07: yield finish(1, 'RLCA')
        elif op == 0x08: yield finish(1, "EX AF,AF'")
        elif op == 0x09: yield finish(1, 'ADD HL,BC')
        elif op == 0x0A: yield finish(1, 'LD A,(BC)')
        elif op == 0x0B: yield finish(1, 'DEC BC')
        elif op == 0x0C: yield finish(1, 'INC C')
        elif op == 0x0D: yield finish(1, 'DEC C')
        elif op == 0x0E: yield finish(2, f'LD C,0x{u8(pc+1):02X}')
        elif op == 0x10: yield finish(2, f'DJNZ 0x{(addr+2+s8(pc+1))&0xFFFF:04X}')
        elif op == 0x11: yield finish(3, f'LD DE,0x{u16(pc+1):04X}')
        elif op == 0x12: yield finish(1, 'LD (DE),A')
        elif op == 0x13: yield finish(1, 'INC DE')
        elif op == 0x14: yield finish(1, 'INC D')
        elif op == 0x16: yield finish(2, f'LD D,0x{u8(pc+1):02X}')
        elif op == 0x17: yield finish(1, 'RLA')
        elif op == 0x18: yield finish(2, f'JR 0x{(addr+2+s8(pc+1))&0xFFFF:04X}')
        elif op == 0x19: yield finish(1, 'ADD HL,DE')
        elif op == 0x1A: yield finish(1, 'LD A,(DE)')
        elif op == 0x1B: yield finish(1, 'DEC DE')
        elif op == 0x1C: yield finish(1, 'INC E')
        elif op == 0x1E: yield finish(2, f'LD E,0x{u8(pc+1):02X}')
        elif op == 0x1F: yield finish(1, 'RRA')
        elif op == 0x20: yield finish(2, f'JR NZ,0x{(addr+2+s8(pc+1))&0xFFFF:04X}')
        elif op == 0x21: yield finish(3, f'LD HL,0x{u16(pc+1):04X}')
        elif op == 0x22: yield finish(3, f'LD (0x{u16(pc+1):04X}),HL')
        elif op == 0x23: yield finish(1, 'INC HL')
        elif op == 0x24: yield finish(1, 'INC H')
        elif op == 0x26: yield finish(2, f'LD H,0x{u8(pc+1):02X}')
        elif op == 0x28: yield finish(2, f'JR Z,0x{(addr+2+s8(pc+1))&0xFFFF:04X}')
        elif op == 0x29: yield finish(1, 'ADD HL,HL')
        elif op == 0x2A: yield finish(3, f'LD HL,(0x{u16(pc+1):04X})')
        elif op == 0x2F: yield finish(1, 'CPL')
        elif op == 0x30: yield finish(2, f'JR NC,0x{(addr+2+s8(pc+1))&0xFFFF:04X}')
        elif op == 0x31: yield finish(3, f'LD SP,0x{u16(pc+1):04X}')
        elif op == 0x32: yield finish(3, f'LD (0x{u16(pc+1):04X}),A')
        elif op == 0x36: yield finish(2, f'LD (HL),0x{u8(pc+1):02X}')
        elif op == 0x38: yield finish(2, f'JR C,0x{(addr+2+s8(pc+1))&0xFFFF:04X}')
        elif op == 0x3A: yield finish(3, f'LD A,(0x{u16(pc+1):04X})')
        elif op == 0x3C: yield finish(1, 'INC A')
        elif op == 0x3D: yield finish(1, 'DEC A')
        elif op == 0x3E: yield finish(2, f'LD A,0x{u8(pc+1):02X}')
        elif op == 0x40: yield finish(1, f'LD {r8[(op>>3)&7]},{r8[op&7]}')
        elif 0x40 <= op <= 0x7F and op != 0x76:
            yield finish(1, f'LD {r8[(op>>3)&7]},{r8[op&7]}')
        elif op == 0x76: yield finish(1, 'HALT')
        elif 0x80 <= op <= 0xBF:
            ops = ['ADD','ADC','SUB','SBC','AND','XOR','OR','CP']
            yield finish(1, f'{ops[(op>>3)&7]} A,{r8[op&7]}' if (op>>3)&7 not in (2,4,5,6,7) else f'{ops[(op>>3)&7]} {r8[op&7]}')
        elif op == 0xC1: yield finish(1, 'POP BC')
        elif op == 0xC3: yield finish(3, f'JP 0x{u16(pc+1):04X}')
        elif op == 0xC5: yield finish(1, 'PUSH BC')
        elif op == 0xC8: yield finish(1, 'RET Z')
        elif op == 0xC9: yield finish(1, 'RET')
        elif op == 0xCA: yield finish(3, f'JP Z,0x{u16(pc+1):04X}')
        elif op == 0xCB:
            op2 = mem[pc+1]
            bit_ops = {0x40:'BIT 0',0x48:'BIT 1',0x50:'BIT 2',0x58:'BIT 3',
                       0x60:'BIT 4',0x68:'BIT 5',0x70:'BIT 6',0x78:'BIT 7'}
            for base_b, name in bit_ops.items():
                if base_b <= op2 < base_b+8:
                    yield finish(2, f'{name},{r8[op2&7]}')
                    break
            else:
                yield finish(2, f'CB 0x{op2:02X}')
        elif op == 0xCD: yield finish(3, f'CALL 0x{u16(pc+1):04X}')
        elif op == 0xD1: yield finish(1, 'POP DE')
        elif op == 0xD3: yield finish(2, f'OUT (0x{u8(pc+1):02X}),A')
        elif op == 0xD5: yield finish(1, 'PUSH DE')
        elif op == 0xD9: yield finish(1, 'EXX')
        elif op == 0xDB: yield finish(2, f'IN A,(0x{u8(pc+1):02X})')
        elif op == 0xE1: yield finish(1, 'POP HL')
        elif op == 0xE3: yield finish(1, 'EX (SP),HL')
        elif op == 0xE5: yield finish(1, 'PUSH HL')
        elif op == 0xE6: yield finish(2, f'AND 0x{u8(pc+1):02X}')
        elif op == 0xE9: yield finish(1, 'JP (HL)')
        elif op == 0xEB: yield finish(1, 'EX DE,HL')
        elif op == 0xED:
            op2 = mem[pc+1]
            if   op2 == 0x53: yield finish(4, f'LD (0x{u16(pc+2):04X}),HL')
            elif op2 == 0x5B: yield finish(4, f'LD DE,(0x{u16(pc+2):04X})')
            elif op2 == 0x43: yield finish(4, f'LD (0x{u16(pc+2):04X}),BC')
            elif op2 == 0x4B: yield finish(4, f'LD BC,(0x{u16(pc+2):04X})')
            elif op2 == 0x63: yield finish(4, f'LD (0x{u16(pc+2):04X}),HL')
            elif op2 == 0x6B: yield finish(4, f'LD HL,(0x{u16(pc+2):04X})')
            elif op2 == 0x78: yield finish(2, 'IN A,(C)')
            elif op2 == 0xB0: yield finish(2, 'LDIR')
            elif op2 == 0xB8: yield finish(2, 'LDDR')
            else:             yield finish(2, f'ED 0x{op2:02X}')
        elif op == 0xF1: yield finish(1, 'POP AF')
        elif op == 0xF3: yield finish(1, 'DI')
        elif op == 0xF5: yield finish(1, 'PUSH AF')
        elif op == 0xFB: yield finish(1, 'EI')
        elif op == 0xFD:
            op2 = mem[pc+1]
            if   op2 == 0x21: yield finish(4, f'LD IY,0x{u16(pc+2):04X}')
            elif op2 == 0x35: yield finish(3, f'DEC (IY+0x{u8(pc+2):02X})')
            elif op2 == 0x36: yield finish(4, f'LD (IY+0x{u8(pc+2):02X}),0x{u8(pc+3):02X}')
            elif op2 == 0x46: yield finish(3, f'LD B,(IY+0x{u8(pc+2):02X})')
            elif op2 == 0x77: yield finish(3, f'LD (IY+0x{u8(pc+2):02X}),A')
            else:             yield finish(2, f'FD 0x{op2:02X}')
        elif op == 0xFE: yield finish(2, f'CP 0x{u8(pc+1):02X}')
        elif op == 0xFF: yield finish(1, 'RST 0x38')
        else:            yield finish(1, f'???  ; 0x{op:02X}')


# ─────────────────────────────────────────────────────
# Print boot sector disassembly
# ─────────────────────────────────────────────────────
boot = sector(0, 0)
print("=" * 60)
print("BOOT SECTOR T00/S0  (loaded at 0x4200 by ROM)")
print("=" * 60)
for addr, raw, mnem in disasm(boot, 0x4200):
    # Add a small comment for known landmarks
    comments = {
        0x4200: "; entry (NOP - skip load-addr bytes)",
        0x4202: "; (CP 0x11 — 2nd load-addr byte skip)",
        0x4203: "; DI",
        0x4204: "; setup SP=0x41E0",
        0x4207: "; IY=0",
        0x420B: "; HL = 0x42E4 (boot table pointer)",
        0x420E: "; CALL init_fdc (0x429E)",
        0x4211: "; select drive 0",
        0x4216: "; LD A,(0x4202) = load config byte",
        0x4219: "; save as D",
        0x421A: "; E = 0x04",
        0x421C: "; BC = 0x5100 (staging base)",
        0x421F: "; CALL read_sector_to_staging",
        0x4222: "; JR NZ to_end — check if sector loaded OK",
        0x4224: "; LD A,(0x5100) = first byte of T17/S4",
        0x4227: "; AND 0x10  — test bit 4 (48KB flag?)",
        0x4229: "; HL = 0x42E7 (48KB module table)",
        0x422C: "; JR Z to_16kb_table — use 16KB table if bit=0",
        0x422E: "; EXX swap: save HL'=0x42E7",
        0x422F: "; HL = (0x5116) = track/count from T17/S4",
        0x4232: "; D = L (= track number from T17/S4[0x16])",
        0x4233: "; A = H (= group count from T17/S4[0x17])",
        0x4241: "; EXX restore: HL=0x42E7, BC=stage-ptr",
        0x4242: "; CALL get_byte — read next byte from sector stream",
        0x4245: "; DEC A — check record type",
        0x4246: "; JR NZ → 0x425F if type != 0x01",
        0x4248: "; --- record type 0x01: patch N bytes to addr ---",
        0x4279: "; === subroutine: get_byte ===",
        0x427A: "; INC C (advance staging offset)",
        0x427B: "; JR NZ → 0x4291 (fetch byte from staged sector)",
        0x427D: "; --- C wrapped: load next sector ---",
        0x4280: "; select drive",
        0x4294: "; === subroutine: init_fdc (0x429E) ===",
    }
    comment = comments.get(addr, "")
    print(f"  {addr:04X}: {raw:<12} {mnem:<30} {comment}")

# ─────────────────────────────────────────────────────
# Parse T09/S0 module directory
# ─────────────────────────────────────────────────────
print()
print("=" * 60)
print("T09/S0 Module Directory (byte-stream parse)")
print("=" * 60)

s = sector(9, 0)
i = 0
while i < 256:
    rt = s[i]
    print(f"  [{i:02X}] record_type=0x{rt:02X}", end="")
    if rt == 0x00:
        print(" END")
        break
    elif rt == 0x05:
        # Skip N bytes
        n = s[i+1]
        skipped = bytes(s[i+2:i+2+n])
        txt = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in skipped)
        print(f" SKIP  n={n}  bytes=[{txt}]")
        i += 2 + n
    elif rt == 0x1F:
        # Info block: length, then text
        n = s[i+1]
        txt = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in s[i+2:i+2+n])
        print(f" INFO  n={n}  text=[{txt[:40]}...]")
        i += 2 + n
    elif rt == 0x01:
        # Patch: N, addr_lo, addr_hi, data[N-2]
        n  = s[i+1]
        lo = s[i+2]
        hi = s[i+3]
        addr16 = lo | (hi << 8)
        data = list(s[i+4:i+2+n])
        print(f" PATCH addr=0x{addr16:04X}  n_data={n-2}  data={[f'0x{b:02X}' for b in data]}")
        i += 2 + n
    elif rt == 0x02:
        # Load: skip 1, then 2-byte address, then N bytes?
        # 0x02 = dispatch record (read skip byte, then 2-byte addr, JP(HL))
        skip_b = s[i+1]
        lo = s[i+2]
        hi = s[i+3]
        addr16 = lo | (hi << 8)
        print(f" JUMP  skip=0x{skip_b:02X} target=0x{addr16:04X}")
        i += 4
    elif rt == 0x03:
        # End of module / terminator
        n = s[i+1]
        print(f" END_MODULE  n=0x{n:02X}")
        i += 2
    else:
        print(f" UNKNOWN  next=[{' '.join(f'{s[j]:02X}' for j in range(i+1, min(i+8, 256)))}]")
        i += 1

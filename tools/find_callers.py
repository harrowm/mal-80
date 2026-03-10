#!/usr/bin/env python3
"""Find all CALL/JP (conditional or unconditional) to one or more target addresses in memdump.bin."""
import sys
import os

data = open(os.path.join(os.path.dirname(__file__), '..', 'memdump.bin'), 'rb').read()

targets = [int(a, 16) for a in sys.argv[1:]] if len(sys.argv) > 1 else [0x4400, 0x4405]

# All 3-byte absolute jump/call opcodes
JP_OPS = {
    0xC2: 'JP NZ', 0xCA: 'JP Z',  0xD2: 'JP NC', 0xDA: 'JP C',
    0xE2: 'JP PO', 0xEA: 'JP PE', 0xF2: 'JP P',  0xFA: 'JP M',
    0xC3: 'JP',
    0xC4: 'CALL NZ', 0xCC: 'CALL Z',  0xD4: 'CALL NC', 0xDC: 'CALL C',
    0xE4: 'CALL PO', 0xEC: 'CALL PE', 0xF4: 'CALL P',  0xFC: 'CALL M',
    0xCD: 'CALL',
}

for o in range(len(data) - 2):
    op = data[o]
    if op not in JP_OPS:
        continue
    w = data[o+1] | (data[o+2] << 8)
    if w in targets:
        print(f'{JP_OPS[op]:10s} 0x{w:04X}  at 0x{o:04X}  bytes: {data[o:o+3].hex(" ")}  ctx: {data[max(0,o-3):o+6].hex(" ")}')

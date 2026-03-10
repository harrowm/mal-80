#!/usr/bin/env python3
"""
investigate_dir.py — Focused disassembly for DIR :0 ILLEGAL DRIVE NUMBER investigation.

Disassembles all unexplored regions relevant to the bug and dumps key runtime
values from memdump.bin.  Run from project root:

  python3 tools/investigate_dir.py 2>&1 | tee /tmp/dir_investigation.txt
"""

import subprocess
import tempfile
import os
import sys

MEMDUMP = os.path.join(os.path.dirname(__file__), '..', 'memdump.bin')


def disasm(data: bytes, start: int, end: int) -> str:
    chunk = data[start:min(end, len(data))]
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as t:
        t.write(chunk)
        path = t.name
    try:
        r = subprocess.run(
            ['z80dasm', '-a', '-l', '-t', '-g', hex(start), path],
            capture_output=True, text=True
        )
        lines = [l for l in r.stdout.splitlines()
                 if not l.startswith('; z80dasm') and not l.startswith('; command')]
        return '\n'.join(lines)
    finally:
        os.unlink(path)


def hexdump(data: bytes, start: int, length: int, label: str = '') -> str:
    lines = []
    if label:
        lines.append(f'; {label}')
    for i in range(start, start + length):
        if i < len(data):
            lines.append(f'  [{i:04X}] = 0x{data[i]:02X}  ({data[i]:3d})')
    return '\n'.join(lines)


def section(title: str) -> str:
    bar = '=' * 60
    return f'\n{bar}\n; {title}\n{bar}\n'


def main():
    with open(MEMDUMP, 'rb') as f:
        data = f.read()

    print(f'; memdump.bin: {len(data)} bytes\n')

    # ── 1. 0x4410 jump target (called from DIR_CMD with A=7) ─────────────────
    # 0x4410 = JP 0x45A5
    print(section('0x45A0–0x45C5: DIR_CMD init call (via JP 0x45A5 at 0x4410, A=7)'))
    print(disasm(data, 0x45A0, 0x45C5))

    # ── 2. LDOS interrupt handler ─────────────────────────────────────────────
    # ROM at 0x0038 (IM1 vector). Also LDOS might shadow 0x0038 with RAM.
    print(section('ROM 0x0030–0x0050 (RST 38h IM1 interrupt vector)'))
    print(disasm(data, 0x0030, 0x0050))

    # ── 3. 0x4044 DCT / LDOS RAM variables ───────────────────────────────────
    print(section('Key LDOS RAM values in memdump.bin'))
    print(hexdump(data, 0x403D, 1,  '0x403D DSPFLG (display-width flag, NOT RAM flag)'))
    print(hexdump(data, 0x4000, 9,  '0x4000 JMPDO/JMPNO/JMPTO SVC jump vectors'))
    print(hexdump(data, 0x4044, 16, '0x4044 DCT area'))
    print(hexdump(data, 0x4049, 2,  '0x4049 HIGH$ (LE)'))
    print(hexdump(data, 0x4700, 10, '0x4700 Drive 0 table entry (IY base)'))
    print(hexdump(data, 0x470A, 10, '0x470A Drive 1 table entry'))
    print(hexdump(data, 0x4308, 2,  '0x4308 saved_C / 0x4309 saved_drv select'))
    print(hexdump(data, 0x430E, 2,  '0x430E 0x430F IRQ-related'))
    print(hexdump(data, 0x422A, 10, '0x422A..0x226F drive data block (first 10 bytes)'))
    print(hexdump(data, 0x50B0, 8,  '0x50B0 SVC chain'))

    # ── 4. LDOS command dispatcher ────────────────────────────────────────────
    print(section('0x44B0–0x4530 (LDOS command dispatcher / init region)'))
    print(disasm(data, 0x44B0, 0x4530))

    # ── 5. IRQ handler in LDOS (0x45C0–0x45E0, previously partially seen) ────
    print(section('0x45C0–0x45E0 (LDOS timer IRQ handler — called from RST 38h)'))
    print(disasm(data, 0x45C0, 0x45E0))

    # ── 6. What is at 0x4410 exactly (the indirect call from DIR_CMD) ─────────
    print(section('0x4405–0x4425 (error handler + indirect dispatch table)'))
    print(disasm(data, 0x4405, 0x4425))

    # ── 7. Full 0x4B00-0x4B45 region in context ───────────────────────────────
    print(section('0x4892–0x48B0 (caller of VALIDATE_DRIVE @ 0x4892, broader ctx)'))
    print(disasm(data, 0x4840, 0x48B5))

    # ── 8. 0x4C25 caller context ──────────────────────────────────────────────
    print(section('0x4C00–0x4C60 (caller of VALIDATE_DRIVE @ 0x4C2B, broader ctx)'))
    print(disasm(data, 0x4BF0, 0x4C60))

    # ── 9. 0x4BF0 — patched by DIR :0 when wildcard used ─────────────────────
    print(section('0x4BE0–0x4C00 (0x4BF0 patched to RET Z by DIR wildcard code)'))
    print(disasm(data, 0x4BE0, 0x4C10))

    # ── 10. What is at RST 38 jump target in HIGH RAM? ────────────────────────
    # LDOS installs an interrupt handler. The SVC jump table at 0x50B0 might
    # chain into an IRQ handler. Check where RST 38h actually goes.
    # In ROM: 0x0038 is the IM1 handler. Memdump shows ROM there.
    rst38_bytes = data[0x0038:0x003F]
    print(section('RST 38h bytes in memdump (ROM shadow, not patched)'))
    for i, b in enumerate(rst38_bytes):
        print(f'  [0x{0x38+i:04X}] = 0x{b:02X}')
    print(disasm(data, 0x0038, 0x0060))

    # ── 11. Track sector data for T17 to confirm RECTYPE mechanism ────────────
    disk_path = os.path.join(os.path.dirname(__file__), '..', 'disks', 'ld1-531.dsk')
    if os.path.exists(disk_path):
        print(section('JV1 disk: T17/S0 first 16 bytes (directory track GAT sector)'))
        with open(disk_path, 'rb') as f:
            disk = f.read()
        offs = (17 * 10 + 0) * 256
        t17s0 = disk[offs:offs+16]
        print(f'  T17/S0 @ offset 0x{offs:05X}: {t17s0.hex(" ")}')
        # Also check what LDOS driver stores at BC (0x0A88) during probe
        print()
        print(section('Memory at 0x0A88–0x0A98 (where B=9 probe stores T17/S0 data)'))
        print(hexdump(data, 0x0A88, 16, 'sector buffer (written during probe — may be 0 in static dump)'))


if __name__ == '__main__':
    main()

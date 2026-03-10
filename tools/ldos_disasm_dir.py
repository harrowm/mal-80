#!/usr/bin/env python3
"""
ldos_disasm_dir.py — Disassemble all LDOS regions relevant to the DIR :0
"ILLEGAL DRIVE NUMBER" investigation.

Runs z80dasm on each region, collects output, and prints a unified annotated
listing.  Run from the project root:

  python3 tools/ldos_disasm_dir.py

OR to produce output suitable for pasting into an MD file:

  python3 tools/ldos_disasm_dir.py --md > /tmp/disasm_out.txt 2>&1
"""

import subprocess
import tempfile
import os
import sys
import argparse

MEMDUMP = os.path.join(os.path.dirname(__file__), '..', 'memdump.bin')

# ── Regions to disassemble ────────────────────────────────────────────────────
# (start, end, label)
REGIONS = [
    (0x4409, 0x4415, "ERROR_HANDLER — LD A,0x96 / RST 28h"),
    (0x4700, 0x4760, "DRIVE_TABLE_0x4700 — drive entries 0-4 + secondary dispatch table"),
    (0x4760, 0x47B0, "B9_DISPATCH_0x4777, SETUP_IY_0x478F"),
    (0x47A5, 0x47C0, "DRIVE_TABLE_HL_0x47A5, READ_DCT_FIELD_0x479C"),
    (0x45E0, 0x4670, "WAIT_SELECT_0x45E0..DO_SEEK_0x462A..CMD_READ_SECTOR_0x466B start"),
    (0x4670, 0x4700, "CMD_READ_SECTOR cont + BIT_COUNT_LOOP_0x46F5"),
    (0x4B45, 0x4BA0, "VALIDATE_ENTRY_0x4B45, INNER_PROBE_0x4B5E, GET_DIR_TRK_NUM_0x4B65"),
    (0x4B00, 0x4B45, "VALIDATE_DRIVE_0x4B10, GET_DIR_TRACK_0x4B37"),
    (0x4FC0, 0x5040, "DIR_CMD_0x4FC0"),
    # Unknown callers of VALIDATE_DRIVE
    (0x4880, 0x48B0, "Unknown caller @ 0x4892"),
    (0x4C25, 0x4C50, "Unknown caller @ 0x4C2B"),
    # IRQ/timer path that feeds into NMI handler
    (0x45C0, 0x45E0, "IRQ_HANDLER_0x45C0 (before WAIT_SELECT)"),
    # Drive select latch path in Bus region
]


def disasm_region(data: bytes, start: int, end: int) -> str:
    chunk = data[start:end]
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as t:
        t.write(chunk)
        path = t.name
    try:
        result = subprocess.run(
            ['z80dasm', '-a', '-l', '-t', '-g', hex(start), path],
            capture_output=True, text=True
        )
        return result.stdout
    finally:
        os.unlink(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--md', action='store_true', help='Output as markdown code block')
    args = parser.parse_args()

    with open(MEMDUMP, 'rb') as f:
        data = f.read()

    size = len(data)
    print(f"; memdump.bin size: {size} bytes (0x{size:04X})\n")

    for (start, end, label) in REGIONS:
        if start >= size:
            print(f"; SKIP {label} — out of range\n")
            continue
        real_end = min(end, size)

        header = f"; ═══════════════════════════════════════════════════════\n"
        header += f"; {label}\n"
        header += f"; Range: 0x{start:04X}–0x{real_end:04X}\n"
        header += f"; ═══════════════════════════════════════════════════════\n"
        print(header)

        out = disasm_region(data, start, real_end)
        # Strip leading z80dasm banner/org line for brevity
        lines = out.splitlines()
        filtered = [l for l in lines if not l.startswith('; z80dasm') and not l.startswith('; command')]
        print('\n'.join(filtered))
        print()


if __name__ == '__main__':
    main()

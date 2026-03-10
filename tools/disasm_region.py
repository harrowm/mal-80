#!/usr/bin/env python3
"""
disasm_region.py — disassemble a range from memdump.bin using z80dasm.

Usage:
  python3 tools/disasm_region.py 0x4500 0x4800
  python3 tools/disasm_region.py 0x4500 0x4800 --labels labels.txt

z80dasm is called with --address set to the start offset so that all
addresses in the output are absolute (matching the LDOS memory map).

Prints annotated disassembly to stdout.  Pipe to 'less -S' for paging.
"""

import subprocess
import sys
import tempfile
import os
import argparse


MEMDUMP = os.path.join(os.path.dirname(__file__), '..', 'memdump.bin')
Z80DASM = 'z80dasm'


def disasm(start: int, end: int, labels_file: str | None = None) -> str:
    """
    Extract [start..end) from memdump.bin into a temp file and run z80dasm.
    Returns disassembly output as a string.
    """
    with open(MEMDUMP, 'rb') as f:
        data = f.read()

    if start >= len(data) or end > len(data) + 1:
        raise ValueError(f"Range 0x{start:04X}–0x{end:04X} exceeds dump size 0x{len(data):04X}")

    chunk = data[start:end]

    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as tmp:
        tmp.write(chunk)
        tmp_path = tmp.name

    try:
        cmd = [Z80DASM,
               '-a',                    # print address in comment
               '-l',                    # create labels for jumps
               '-t',                    # print hex/ascii data bytes
               f'--origin={hex(start)}',
               tmp_path]
        if labels_file:
            cmd += [f'--sym-input={labels_file}']
        result = subprocess.run(cmd, capture_output=True, text=True)
        return result.stdout + (result.stderr if result.stderr else '')
    finally:
        os.unlink(tmp_path)


def main():
    parser = argparse.ArgumentParser(description='Disassemble a region of memdump.bin')
    parser.add_argument('start', help='Start address (hex, e.g. 0x4500)')
    parser.add_argument('end',   help='End address (hex, e.g. 0x4800)')
    parser.add_argument('--labels', help='Optional z80dasm symbol file')
    args = parser.parse_args()

    start = int(args.start, 16)
    end   = int(args.end, 16)

    print(disasm(start, end, args.labels))


if __name__ == '__main__':
    main()

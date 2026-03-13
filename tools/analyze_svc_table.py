#!/usr/bin/env python3
"""Analyze SVC table and motor-wait code from current memdump."""
import subprocess, tempfile, os, sys

DUMP = '/Users/malcolm/mal-80/memdump_svc93.bin'

def disasm(data, start, end):
    chunk = data[start:end]
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(chunk)
        tmp = f.name
    r = subprocess.run(['z80dasm', '-a', '-l', '-t', f'--origin=0x{start:04X}', tmp],
                       capture_output=True, text=True)
    os.unlink(tmp)
    return r.stdout

def hexr(data, start, end, label=''):
    if label:
        print(f'--- {label} ---')
    for a in range(start, end, 16):
        ch = data[a:a+16]
        h = ' '.join(f'{b:02X}' for b in ch)
        a2 = ''.join(chr(b) if 32<=b<127 else '.' for b in ch)
        print(f'  {a:04X}: {h:<48}  {a2}')

d = open(DUMP, 'rb').read()

print('=== SVC table lookup at 0x4B00-0x4B80 ===')
print(disasm(d, 0x4B00, 0x4B80))

print()
print('=== 0x4A00-0x4B20 (SVC vectors / kernel tables) ===')
hexr(d, 0x4A00, 0x4B20)

print()
# What is at 0x4876? (saw 76 48 at 0x4310-0x4311)
print('=== 0x4870-0x4900 (possible SVC handler) ===')
print(disasm(d, 0x4870, 0x4920))

print()
# What is at the SVC 0xC4 handler after DISKDIR loads?
# Look at 0x43B8-0x43C8 and self-mod CALL at 0x4BE4
print('=== 0x4BE0-0x4BF0 (self-mod CALL at 0x4BE4) ===')
hexr(d, 0x4BE0, 0x4BF0)
print('=== 0x430E-0x4320 (SVC bank state) ===')
hexr(d, 0x430E, 0x4320)

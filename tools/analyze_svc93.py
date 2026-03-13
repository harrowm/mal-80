#!/usr/bin/env python3
"""Analyze the SVC 0x93 hang in DISKDIR."""
import subprocess, tempfile, os, sys

def disasm(dump_file, start, end):
    data = open(dump_file,'rb').read()
    chunk = data[start:end]
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(chunk)
        tmp = f.name
    result = subprocess.run(
        ['z80dasm', '-a', '-l', '-t', f'--origin=0x{start:04X}', tmp],
        capture_output=True, text=True
    )
    os.unlink(tmp)
    return result.stdout

def hexdump(data, start, end, label=''):
    if label:
        print(f'--- {label} ---')
    chunk = data[start:end]
    for i in range(0, len(chunk), 16):
        h = ' '.join(f'{b:02X}' for b in chunk[i:i+16])
        a = ''.join(chr(b) if 32<=b<127 else '.' for b in chunk[i:i+16])
        print(f'  {start+i:04X}: {h:<48}  {a}')

dump = sys.argv[1] if len(sys.argv) > 1 else 'memdump_svc93.bin2'
data = open(dump,'rb').read()

print(f'=== Analyzing {dump} ===\n')

# 1. Find all RST 28h (SVC calls) in DISKDIR region
print('=== RST 28h (SVC) calls in 0x4200-0x5500 ===')
for i in range(0x4200, 0x5500):
    if data[i] == 0xEF:
        ctx = data[max(0,i-5):i+2]
        ctx_hex = ' '.join(f'{b:02X}' for b in ctx)
        # Try to identify the SVC code from preceding LD A,n
        svc_desc = ''
        if i >= 2 and data[i-2] == 0x3E:
            svc_code = data[i-1]
            svc_desc = f' -> SVC 0x{svc_code:02X}'
        print(f'  0x{i:04X}: RST 28h  | context[-5..+1]: {ctx_hex}{svc_desc}')

print()

# 2. Key memory locations
print('=== Key RAM variables ===')
hexdump(data, 0x4016, 0x4030, '0x4016-0x402F (display/svc vars)')
hexdump(data, 0x4040, 0x4056, '0x4040-0x4055 (interrupt counter + top-of-RAM)')
hexdump(data, 0x430E, 0x4320, '0x430E-0x431F (SVC state + 0x4315)')
hexdump(data, 0x43B8, 0x43C0, '0x43B8-0x43BF (saved display pointers)')
hexdump(data, 0x53F0, 0x5410, '0x53F0-0x540F (DISKDIR work area around 0x53FE)')

print()

# 3. ROM at RST 28h vector
rom_data = open('roms/level2.rom','rb').read()
print('=== ROM 0x0020-0x0040 (RST 28h = 0x0028) ===')
hexdump(rom_data, 0x0020, 0x0040)

print()

# 4. Disassemble key areas
print('=== SVC 0x93 handler at 0x4E00 ===')
print(disasm(dump, 0x4E00, 0x4F00))

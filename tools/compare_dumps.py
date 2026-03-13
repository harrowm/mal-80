#!/usr/bin/env python3
"""Compare memdump files to find what differs between working/failing DIR :0 runs."""
import subprocess, tempfile, os

def hexline(data, addr, n=16):
    chunk = data[addr:addr+n]
    h = ' '.join(f'{b:02X}' for b in chunk)
    a = ''.join(chr(b) if 32<=b<127 else '.' for b in chunk)
    return f'  {addr:04X}: {h:<48}  {a}'

def disasm_region(data, start, end):
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

d1 = open('memdump_svc93.bin1', 'rb').read()    # 8 lines then MEMORY SIZE?
d2 = open('memdump_svc93.bin2', 'rb').read()    # headers only then freeze
dc = open('memdump_svc93.bin',  'rb').read()    # latest run

print('=== 0x4E00-0x4E30 (SVC 0x93 handler entry) ===')
print('bin1 (8 lines then MEMORY SIZE?):')
for a in range(0x4E00, 0x4E30, 16): print(hexline(d1, a))
print('bin2 (headers only, then freeze):')
for a in range(0x4E00, 0x4E30, 16): print(hexline(d2, a))
print('bin  (latest):')
for a in range(0x4E00, 0x4E30, 16): print(hexline(dc, a))
print()

print('=== interrupt counter 0x4040-0x4050 ===')
print(f'bin1: {" ".join(f"{b:02X}" for b in d1[0x4040:0x4050])}')
print(f'bin2: {" ".join(f"{b:02X}" for b in d2[0x4040:0x4050])}')
print(f'bin : {" ".join(f"{b:02X}" for b in dc[0x4040:0x4050])}')
print()

print('=== High RAM top-of-stack area 0x4049 (first writable RAM checked by LDOS) ===')
for nm, d in [('bin1', d1), ('bin2', d2), ('bin ', dc)]:
    v = d[0x4049] | (d[0x404A] << 8)
    print(f'  {nm}: 0x4049={d[0x4049]:02X} 0x404A={d[0x404A]:02X}  -> word={v:04X}')
print()

print('=== SVC 0x93 handler diff (0x4E00-0x4F00) ===')
same = True
for i in range(0x4E00, 0x4F00):
    if not (d1[i] == d2[i] == dc[i]):
        same = False
        print(f'  {i:04X}: bin1={d1[i]:02X}  bin2={d2[i]:02X}  bin={dc[i]:02X}')
if same:
    print('  (identical across all three dumps)')
print()

print('=== Disasm 0x4E00-0x4E60 (bin2 = freeze case) ===')
print(disasm_region(d2, 0x4E00, 0x4E60))

print('=== 0x53E0-0x5420 (DISKDIR work area) ===')
print('bin1:')
for a in range(0x53E0, 0x5420, 16): print(hexline(d1, a))
print('bin2:')
for a in range(0x53E0, 0x5420, 16): print(hexline(d2, a))

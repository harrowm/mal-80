#!/usr/bin/env python3
"""Find where 0x4E00 (module load address) appears in the LDOS disk directory."""
with open('/Users/malcolm/mal-80/disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

def sector(t, s):
    return data[(t * 10 + s) * 256 : (t * 10 + s + 1) * 256]

targets = [
    (0x00, 0x4E, '0x4E00'),
    (0x00, 0x52, '0x5200'),
    (0xFF, 0x51, '0x51FF'),
]

print("Scanning directory tracks T09-T11 for module load address candidates:")
for t in range(9, 12):
    for s in range(10):
        sec = sector(t, s)
        for lo, hi, label in targets:
            for i in range(len(sec) - 1):
                if sec[i] == lo and sec[i+1] == hi:
                    ctx = ' '.join(f'{b:02X}' for b in sec[max(0,i-6):i+10])
                    print(f"  T{t:02d}/S{s} offset {i:3d}: {label} found  context: {ctx}")

# Look at T09/S0 first 64 bytes (start of SYS directory)
print()
print("T09/S0 first 64 bytes (start of SYS directory):")
sec = sector(9, 0)
for i in range(0, 64, 16):
    h = ' '.join(f'{b:02X}' for b in sec[i:i+16])
    a = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in sec[i:i+16])
    print(f"  {i:3d}: {h}  {a}")

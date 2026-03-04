#!/usr/bin/env python3
with open('/Users/malcolm/mal-80/disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

def sector(t, s):
    return data[(t * 10 + s) * 256 : (t * 10 + s + 1) * 256]

# SYS12 = T20/S5-S8 (4 sectors = 1024 bytes), loads to 0x4E00
sys12 = b''.join(sector(20, s) for s in [5,6,7,8])
BASE = 0x4E00

print("SYS12 first 96 bytes (loaded at 0x4E00):")
for i in range(0, 96, 16):
    h = ' '.join('%02X' % b for b in sys12[i:i+16])
    a = ''.join(chr(b) if 0x20<=b<0x7F else '.' for b in sys12[i:i+16])
    print("  %04X: %s  %s" % (BASE+i, h, a))

print()
print("Scanning SYS12 for jump targets near 0xFFFA-0xFFFF:")
for i in range(len(sys12)-1):
    lo, hi = sys12[i], sys12[i+1]
    addr = lo | (hi << 8)
    if 0xFFE0 <= addr <= 0xFFFF:
        print("  SYS12 offset %d (RAM 0x%04X): addr 0x%04X" % (i, BASE+i, addr))

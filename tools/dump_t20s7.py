import sys

data = open('disks/ld1-531.dsk','rb').read()
SPT, BYTES = 10, 256

def sector(t,s):
    return data[(t*SPT + s)*BYTES:(t*SPT + s)*BYTES + BYTES]

s7 = sector(20, 7)
print('T20/S7[0xF0..0xFF]:')
for i in range(0xF0, 0x100):
    print('  [%02X] = 0x%02X' % (i, s7[i]))

print()
print('T20/S7 reloc area [0xE0..0xFF]:')
for row in range(0xE0, 0x100, 16):
    h = ' '.join('%02X' % s7[i] for i in range(row, min(row+16, 256)))
    a = ''.join(chr(s7[i]) if 32 <= s7[i] < 127 else '.' for i in range(row, min(row+16, 256)))
    print('  %02X: %s  %s' % (row, h, a))

print()
print('T20/S6 full hex (first code sector after header):')
s6 = sector(20, 6)
for row in range(0, 256, 16):
    h = ' '.join('%02X' % s6[i] for i in range(row, row+16))
    a = ''.join(chr(s6[i]) if 32 <= s6[i] < 127 else '.' for i in range(row, row+16))
    print('  %02X: %s  %s' % (row, h, a))

# Also check what the relocation record format looks like
# The 50B6_WRITE at PC=0x4CDB: AF=73A0 HL=50B6 DE=42F5
# Writes A=0x73 to (HL)=0x50B6 — this is 0x4200+0xF5=0x42F5
# A=0x73 means T20/S7[0xF5] was the LAST byte read into A before writing
# But wait: AF=73A0 has F=0xA0 = 1010 0000 = S flag (sign) + Z flag??
# Actually F bits: 7=S, 6=Z, 5=-, 4=H, 3=-, 2=P, 1=N, 0=C
# 0xA0 = 1010 0000 = S=1, Z=0, H=0, P=1, N=0, C=0
# Hmm... so it's not reading directly. Let's check what instruction at 0x4CDB is.
print()
print('LDOS module load around PC=0x4CDB (in RAM, loaded from disk):')
print('(Need to check what LDOS loads at 0x4CDB)')

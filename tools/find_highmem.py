#!/usr/bin/env python3
"""
Analyze LDOS disk image to find where HIGH$ is stored.
We know modules load at ~0x50A0, so HIGH$ was ~0x54A0 before SYS12 loaded.
We need to find what RAM address holds this value.
"""
with open('disks/ld1-531.dsk', 'rb') as f:
    data = f.read()

def sector(t, s):
    return data[(t * 10 + s) * 256 : (t * 10 + s + 1) * 256]

# The LDOS kernel occupies T00/S0 through T00/S9, then T01/S0+, etc.
# Each sector = 256 bytes. Boot loads them starting at 0x4200.
# Build a map of kernel addresses to disk bytes.
kernel = bytearray()
for t in range(0, 18):
    for s in range(0, 10):
        kernel += sector(t, s)

def kernel_addr(addr):
    off = addr - 0x4200
    if 0 <= off < len(kernel):
        return kernel[off]
    return None

# Look for the instruction at 0x4CDB (which writes to 0x50B0+).
# It's in a loop - want to find what loads HL with the destination.
# Scan from 0x4CB0 to 0x4CDB looking for LD HL,nn (0x21 lo hi) or LD HL,(nn) (0x2A lo hi)
print("=== Instructions around 0x4C90-0x4CE0 ===")
for a in range(0x4C90, 0x4CE0):
    b = kernel_addr(a)
    if b is None:
        break
    if b == 0x21:  # LD HL,nn
        lo = kernel_addr(a+1) or 0
        hi = kernel_addr(a+2) or 0
        print(f"  0x{a:04X}: LD HL,0x{(hi<<8)|lo:04X}")
    elif b == 0x2A:  # LD HL,(nn)
        lo = kernel_addr(a+1) or 0
        hi = kernel_addr(a+2) or 0
        ptr = (hi << 8) | lo
        print(f"  0x{a:04X}: LD HL,(0x{ptr:04X})")
    elif b == 0xED and kernel_addr(a+1) == 0x4B:  # LD BC,(nn)
        lo = kernel_addr(a+2) or 0
        hi = kernel_addr(a+3) or 0
        ptr = (hi << 8) | lo
        print(f"  0x{a:04X}: LD BC,(0x{ptr:04X})")
    elif b == 0xED and kernel_addr(a+1) == 0x5B:  # LD DE,(nn)
        lo = kernel_addr(a+2) or 0
        hi = kernel_addr(a+3) or 0
        ptr = (hi << 8) | lo
        print(f"  0x{a:04X}: LD DE,(0x{ptr:04X})")

print()
print("=== Bytes at 0x4CDB: ===")
for a in range(0x4CD0, 0x4CF0):
    b = kernel_addr(a)
    c = kernel_addr(a+1) or 0
    print(f"  0x{a:04X}: 0x{b:02X} 0x{c:02X}")

print()
print("=== LDOS variable area 0x4040-0x4060 initial values (from kernel) ===")
for a in range(0x4040, 0x4070):
    b = kernel_addr(a)
    print(f"  0x{a:04X}: 0x{b:02X}  {b}")

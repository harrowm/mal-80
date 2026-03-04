#!/usr/bin/env python3
"""Raw dump of boot stream sectors to understand record format"""

with open("disks/ld1-531.dsk", "rb") as f:
    f.seek(9 * 10 * 256)   # T09/S0
    data = f.read(512)     # T09/S0 + T09/S1

print("T09/S0+S1 raw bytes (first 128):")
for i in range(0, 128, 16):
    row = data[i:i+16]
    hx = ' '.join(f'{b:02X}' for b in row)
    ch = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
    print(f"  {i:04X}: {hx:<48}  {ch}")

# Also check what the disasm_boot.py previously found via T09/S0:
# @003B PATCH addr=0x3D40 n=2 data_len=0
# That means byte @0x3B = 0x01 (PATCH), @0x3C = 2 (n), @0x3D = 0x40, @0x3E = 0x3D
# addr = 0x3D40. Then next byte @0x3F = 0x20 = unknown.
# But if PATCH format is: type,N,addr_lo,addr_hi,data[0..N-1]
# then PATCH(01 02 40 3D) has N=2, addr=0x3D40, data=[nothing] (n-2=0 bytes of data)
# next byte 0x20 = 32 = space char = next rectype??
# OR the format is: type, addr_lo, addr_hi, N, data[N]  (different order!)

# Let's dump bytes 0x38..0x50
print("\nBytes around 0x38:")
row = data[0x38:0x58]
hx = ' '.join(f'{b:02X}' for b in row)
print(f"  0038: {hx}")
print("  If format=type,N,addrlo,addrhi,data:")
print(f"  @3B: type=0x{data[0x3B]:02X}=PATCH, N={data[0x3C]}, addr=0x{data[0x3D]|data[0x3E]<<8:04X}")
print(f"  @3F (next): 0x{data[0x3F]:02X}")
print()
print("  If format=type,addrlo,addrhi,N,data:")
print(f"  @3B: type=0x{data[0x3B]:02X}=PATCH, addr=0x{data[0x3C]|data[0x3D]<<8:04X}, N={data[0x3E]}")
data2 = data[0x3F:0x3F+data[0x3E]] if data[0x3E]<100 else b''
print(f"  data[{data[0x3E]}]=[{' '.join(f'{b:02X}' for b in data2[:8])}...]")

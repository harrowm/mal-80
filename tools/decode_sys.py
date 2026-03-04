#!/usr/bin/env python3
# Decode LDOS SYS file segment records from T20/S5-S8
# LDOS SYS format: stream of records in each sector
# Record types (based on MISOSYS docs):
#   0x02 xx yy zz cc ...  : data segment, load addr=lo(xx)+hi(yy)<<8, len=zz+1, data follows
#   0x04 xx yy            : entry point / transfer address
#   0x05 xx yy zz         : relocation info
#   0x00                  : end of file

import sys, struct

disk = open('disks/ld1-531.dsk', 'rb').read()
SPT = 10
SPB = 256

def read_sector(t, s):
    off = (t * SPT + s) * SPB
    return disk[off:off+SPB]

# Gather all bytes from T20/S5-S8 (the last group read before crash)
sectors = [(20, s) for s in range(5, 9)]
data = b''
for t, s in sectors:
    raw = read_sector(t, s)
    print(f"T{t:02d}/S{s}: {' '.join(f'{b:02X}' for b in raw[:32])} ...")
    data += raw

print(f"\nTotal bytes: {len(data)}")
print()

# Try to decode as LDOS SYS/CMD-like load record format
# LDOS 5.x SYS file block format (from MISOSYS documentation):
# Each block has a 1-byte type followed by data
# Type 0x01: data segment
# Type 0x02: data segment  
# Type 0x05: load address record

print("=== Attempting LDOS SYS record decode ===")
i = 0
while i < len(data):
    b = data[i]
    if b == 0x00:
        print(f"  [{i:04X}] 0x00 = END")
        break
    elif b == 0x01:
        # 1-byte length follows, then data (TRSDOS load record type 1)
        if i+1 >= len(data): break
        length = data[i+1]
        if length == 0: length = 256
        load_lo = data[i+2] if i+2 < len(data) else 0
        load_hi = data[i+3] if i+3 < len(data) else 0
        addr = load_lo | (load_hi << 8)
        end_addr = addr + length - 1
        print(f"  [{i:04X}] TYPE=0x01 len={length} addr=0x{addr:04X}-0x{end_addr:04X}  data[0:4]={' '.join(f'{data[i+4+j]:02X}' for j in range(min(4,length)) if i+4+j < len(data))}")
        i += 4 + length
    elif b == 0x02:
        # Transfer address (entry point)
        if i+2 >= len(data): break
        addr = data[i+1] | (data[i+2] << 8)
        print(f"  [{i:04X}] TYPE=0x02 ENTRY=0x{addr:04X}")
        i += 3
    elif 0x03 <= b <= 0x1E:
        # Short data: next b bytes are at following address
        length = b
        if i+2 >= len(data): break
        addr = data[i+1] | (data[i+2] << 8)
        end_addr = addr + length - 1
        print(f"  [{i:04X}] TYPE=0x{b:02X} (short data len={length}) addr=0x{addr:04X}-0x{end_addr:04X}")
        i += 3 + length
    elif b == 0x1F:
        # Extended: next 2 bytes = length, next 2 = address
        if i+4 >= len(data): break
        length = data[i+1] | (data[i+2] << 8)
        addr   = data[i+3] | (data[i+4] << 8)
        end_addr = addr + length - 1
        print(f"  [{i:04X}] TYPE=0x1F EXTENDED len={length} addr=0x{addr:04X}-0x{end_addr:04X}")
        i += 5 + length
    else:
        print(f"  [{i:04X}] UNKNOWN byte=0x{b:02X} (next 8: {' '.join(f'{data[j]:02X}' for j in range(i, min(i+8, len(data))))})")
        i += 1
        if i > 32 and i % 256 == 0:
            print("  ... stopping after sector boundary ...")
            break

print()
# Also print raw first 64 bytes of T20/S0-S4 (which LDOS never reads)
print("=== T20/S0-S4 (UNREAD sectors) ===")
for s in range(5):
    raw = read_sector(20, s)
    hex32 = ' '.join(f'{b:02X}' for b in raw[:32])
    print(f"T20/S{s}: {hex32}")

# Check T09/S0 format — the very first sector of the kernel
print()
print("=== T09/S0 first 32 bytes (SYS0 start) ===")
raw = read_sector(9, 0)
print(' '.join(f'{b:02X}' for b in raw[:32]))

#!/usr/bin/env python3
"""
parse_full_boot_stream.py — Parse the complete LDOS primary boot stream
from disk. Reads the config sector (T17/S4) then processes T09/S0 onwards.

Boot loader record types (from T00/S0 disassembly):
  Type 0x01 = PATCH: next byte = count (B), then B-2 bytes = lo,hi, data...
    → writes (B-2) bytes to HL starting from (lo,hi)
    Actually: B = total bytes INCLUDING addr fields. So:
      read addr_lo, B-- ; read addr_hi, B-- ; then read B more bytes → write
  Type 0x02 = ENTRY: read 2 bytes → HL, JP(HL) — stream terminator
  All others = SKIP: next byte = count (S), read/discard S bytes, continue

The key: the byte fetcher reads sequentially through sectors T09/S0→T10/S6.
"""
import os, sys, struct

DISK = os.path.join(os.path.dirname(__file__), '..', 'disks', 'ld1-531.dsk')
SPT  = 10
BPS  = 256

def read_sector(d, t, s):
    off = (t * SPT + s) * BPS
    return bytearray(d[off:off+BPS])

with open(DISK, 'rb') as f:
    disk = f.read()

# Read config sector T17/S4
cfg = read_sector(disk, 17, 4)
print(f"Config T17/S4:")
print("  " + " ".join(f"{b:02X}" for b in cfg[:32]))
print()

# Config sector format (from LDOS docs):
# Byte 0x00: flags (bit 4 = disk OS present)
# Bytes 0x14-0x15: start track/sector of stream (packed: hi bits of both)
# Bytes 0x16-0x17: start track+sector in TRS-80 format
#   Actually 0x5116 = LD HL,(0x5116) → HL = cfg[0x16] | (cfg[0x17]<<8)
#   Then D=L, A=H, and the track/sector computation is:
#     E = (A>>3)*5 + (A>>5)*5 = ... 
# Let me just check what T09 is (known to be stream start)

# From disassembly: D=track, E=sector start
# 0x5116 holds the first track:sector doubly-packed
val = cfg[0x16] | (cfg[0x17] << 8)
print(f"  cfg[0x16..17] = 0x{val:04X}  (raw)")

# The disassembly computes D,E from HL=(0x5116):
# D = L (low byte)
# A = H (high byte)
# RLCA×3 then AND 0x07 → H
# RLCA×2, ADD A,H → E
# (This converts TRS-80 packed track/sector to separate values)
L = cfg[0x16]
H = cfg[0x17]
D_init = L   # track = low byte
A = H
# T00/S0 boot loader: LD A,H; RLCA×3; AND 0x07; LD H,A; RLCA×2; ADD A,H; LD E,A
for _ in range(3):
    A = ((A << 1) | (A >> 7)) & 0xFF
A = A & 0x07      # AND 0x07 → only low 3 bits remain
H_saved = A       # LD H,A
for _ in range(2):
    A = ((A << 1) | (A >> 7)) & 0xFF  # RLCA×2
E_init = (A + H_saved) & 0xFF         # ADD A,H; LD E,A

print(f"  Stream start: Track=0x{D_init:02X}={D_init}, Sector=0x{E_init:02X}={E_init}")
print()

# Build a sequential byte generator from the stream sectors
def make_stream(disk, start_track, start_sector):
    """Yields bytes from sectors starting at start_track/start_sector."""
    t = start_track
    s = start_sector
    while True:
        sec = read_sector(disk, t, s)
        for b in sec:
            yield b
        s += 1
        if s >= SPT:
            s = 0
            t += 1

stream = make_stream(disk, D_init, E_init)

# Parse the stream
def next_byte():
    return next(stream)

print("Parsing primary boot stream...")
print(f"  (Record types: 0x01=PATCH, 0x02=ENTRY, other=SKIP)")
print()

# Position counter
pos = 0
records = 0
patches = []
high_patches = []

try:
    for _ in range(100000):  # limit iterations
        rec_type = next_byte()
        pos += 1

        if rec_type == 0x01:
            # PATCH: B = count (includes addr bytes), addr_lo, addr_hi, data...
            count = next_byte(); pos += 1
            addr_lo = next_byte(); pos += 1
            count -= 1
            addr_hi = next_byte(); pos += 1
            count -= 1
            addr = (addr_hi << 8) | addr_lo
            data_bytes = []
            for _ in range(count):
                data_bytes.append(next_byte())
                pos += 1
            patches.append((addr, data_bytes))
            region = "HIGH" if addr >= 0x8000 else "low "
            if addr >= 0x8000:
                high_patches.append((addr, data_bytes))
            firstbytes = " ".join(f"{b:02X}" for b in data_bytes[:8])
            print(f"  +0x{pos-count-4:04X}: PATCH  addr=0x{addr:04X} [{region}]  n={count}  [{firstbytes}...]")
            records += 1

        elif rec_type == 0x02:
            # ENTRY: read 2 bytes for jump address
            lo = next_byte(); pos += 1
            hi = next_byte(); pos += 1
            entry = (hi << 8) | lo
            print(f"  +0x{pos-3:04X}: ENTRY  addr=0x{entry:04X}  ← JP(HL) → this is the LDOS SYS12 entry")
            records += 1
            break  # Stream terminates here

        else:
            # SKIP: next byte = count, skip N bytes
            count = next_byte(); pos += 1
            skipped = []
            for _ in range(count):
                skipped.append(next_byte())
                pos += 1
            ascii_str = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in skipped)
            fb = " ".join(f"{b:02X}" for b in skipped[:8])
            print(f"  +0x{pos-count-2:04X}: SKIP   type=0x{rec_type:02X}  n={count}  [{fb}]  '{ascii_str[:24]}'")
            records += 1

except StopIteration:
    print("(stream ended unexpectedly)")

print()
print(f"Total records parsed: {records}")
print(f"PATCH records: {len(patches)}")
print(f"HIGH-RAM patches: {len(high_patches)}")
print()
print("All PATCH destinations:")
for addr, data in patches:
    region = "HIGH" if addr >= 0x8000 else "low "
    endaddr = addr + len(data) - 1
    firstbytes = " ".join(f"{b:02X}" for b in data[:12])
    print(f"  0x{addr:04X}–0x{endaddr:04X} [{region}]  n={len(data):3d}  [{firstbytes}...]")

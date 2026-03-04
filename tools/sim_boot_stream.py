#!/usr/bin/env python3
"""
scan_all_sectors.py - Scan all 350 sectors and show what load addresses they have.
For LDOS, the first two bytes of each module sector are the load address (LE).
But the BOOT STREAM is different - sectors read via the boot loader are a byte stream.
We need to find SYS12 by looking at what gets copied where.

Key insight from emulator log:
  [JPHL] boot JP(HL)=0x4E00  AF=4E01
  [HIGHRAM] first write to page 0xFF00 val=0x36 PC=0x4E0C

So something at 0x4E0C writes 0xFF00. After boot, PC is at 0x4E0C.
The boot stream PATCH records must have patched 0x4E00+ with executable code.
Let's find which PATCH record covers 0x4E00.

We'll simulate the boot stream parser exactly as the boot loader does it.
"""

DISK = "disks/ld1-531.dsk"
SEC_SIZE = 256
SECS_PER_TRACK = 10

class StreamReader:
    def __init__(self, disk_path):
        self.f = open(disk_path, 'rb')
        self.track = 9     # Start at T09/S0 (from boot config: track=9)
        self.sector = 0
        self.buf = bytearray()
        self.pos = 0       # position within buf (0-255 cycling)
        self.total_bytes = 0
        self._load_sector()

    def _load_sector(self):
        offset = (self.track * SECS_PER_TRACK + self.sector) * SEC_SIZE
        self.f.seek(offset)
        self.buf = bytearray(self.f.read(SEC_SIZE))
        print(f"    [STREAM] loaded T{self.track:02d}/S{self.sector} into staging")
        self.pos = 0

    def get_byte(self):
        """Simulate the boot loader's get_byte subroutine at 0x4279."""
        # The boot loader increments C (staging offset) and wraps at 256
        # When C wraps, it loads next sector
        b = self.buf[self.pos % 256]
        self.pos += 1
        self.total_bytes += 1
        if self.pos >= 256:
            # Load next sector
            self.sector += 1
            if self.sector >= SECS_PER_TRACK:
                self.sector = 0
                self.track += 1
            self._load_sector()
        return b

    def close(self):
        self.f.close()

def simulate_boot_stream():
    """Simulate the LDOS boot loader stream parser."""
    reader = StreamReader(DISK)
    ram = bytearray(0x10000)
    patches = []

    print("\n=== Simulating boot stream record parser ===")
    while True:
        rectype = reader.get_byte()
        rectype_dec1 = (rectype - 1) & 0xFF

        if rectype_dec1 == 0:  # original type = 0x01 = PATCH
            n = reader.get_byte()
            addr_lo = reader.get_byte()
            n -= 1
            addr_hi = reader.get_byte()
            n -= 1
            addr = addr_lo | (addr_hi << 8)
            data = bytearray()
            for _ in range(n):
                b = reader.get_byte()
                data.append(b)
                ram[addr % 0x10000] = b
                addr = (addr + 1) & 0xFFFF
            dest = addr_lo | (addr_hi << 8)
            print(f"  PATCH addr=0x{dest:04X} n={n+2} data_len={n}  [{' '.join(f'{b:02X}' for b in data[:8])}{'...' if n>8 else ''}]")
            patches.append((dest, bytes(data)))

        elif rectype_dec1 == 1:  # original type = 0x02 = JUMP dispatch
            _ = reader.get_byte()       # discard first byte
            addr_lo = reader.get_byte()
            addr_hi = reader.get_byte()
            addr = addr_lo | (addr_hi << 8)
            print(f"  JUMP 0x{addr:04X}  ← boot dispatches here (JP HL)")
            break  # boot loader jumps away at this point

        elif rectype_dec1 == 2:  # original type = 0x03 = END?
            # Actually DEC twice: 0x03 -1 = 0x02, then at 0x425F DEC → 0x01 (not zero) → skip
            # So 0x03 is SKIP too? Let's handle as SKIP
            n = reader.get_byte()
            for _ in range(n):
                reader.get_byte()
            print(f"  SKIP({n}) [type=0x03]")

        else:
            # SKIP branch: type N → skip N bytes
            n = reader.get_byte()
            for _ in range(n):
                reader.get_byte()
            print(f"  SKIP({n}) [type=0x{rectype:02X}]")

        # Safety: don't loop forever
        if reader.total_bytes > 50000:
            print("  TOO MANY BYTES, stopping")
            break

    # Show what got patched around 0x4E00
    print(f"\n=== Ram contents at 0x4E00 after stream ===")
    for i in range(0, 64, 16):
        row = ram[0x4E00+i:0x4E00+i+16]
        hx = ' '.join(f'{b:02X}' for b in row)
        ch = ''.join(chr(b) if 32<=b<127 else '.' for b in row)
        print(f"  {0x4E00+i:04X}: {hx:<48}  {ch}")

    print(f"\n=== Ram contents at 0x4040 (LDOS vectors) ===")
    for i in range(0, 64, 16):
        row = ram[0x4040+i:0x4040+i+16]
        hx = ' '.join(f'{b:02X}' for b in row)
        ch = ''.join(chr(b) if 32<=b<127 else '.' for b in row)
        print(f"  {0x4040+i:04X}: {hx:<48}  {ch}")

    print(f"\n=== All patches ===")
    for addr, data in patches:
        end = addr + len(data)
        if 0x4000 <= addr <= 0x5200 or 0x4000 <= end <= 0x5200:
            print(f"  PATCH 0x{addr:04X}..0x{end:04X} ({len(data)} bytes)")

    reader.close()
    return ram

simulate_boot_stream()

# LDOS 5.3.1 Disk Layout — `ld1-531.dsk`

Disk image analysis and boot-sequence reference for `disks/ld1-531.dsk`,
used as the primary LDOS boot disk in the mal-80 emulator.

---

## Physical Format (JV1)

| Property | Value |
|----------|-------|
| Format | JV1 (TRSDOS/LDOS single-density) |
| File size | 89,600 bytes |
| Geometry | 35 tracks × 10 sectors/track × 256 bytes/sector |
| Sector numbering | 0-based (sector 0 = first sector of track) |
| Byte offset formula | `(track × 10 + sector) × 256` |
| Memory model | 16 KB base + 32 KB expansion = **48 KB total** |

---

## Memory Map (what the disk expects)

| Address range | Contents |
|--------------|----------|
| 0x0000–0x2FFF | ROM — Level II BASIC (12 KB) |
| 0x3800–0x3BFF | Keyboard matrix (memory-mapped, active-low) |
| 0x3C00–0x3FFF | Video RAM (64×16 characters) |
| 0x4000–0x43FF | LDOS kernel — jump vectors, SVC table, variables |
| 0x4400–0x4FFF | LDOS kernel — core OS code |
| 0x5000–0x7FFF | Transient overlay area — low-RAM /SYS modules |
| 0x8000–0xFFFF | Transient overlay area — high-RAM /SYS modules (expansion) |

HIGH$ is set to `0xFFFF` by SYS12 at init — confirming this disk was installed
on a full 48 KB machine (16 KB base + 32 KB expansion board).

---

## Track Layout

| Track | Role |
|-------|------|
| T00 | Boot track — boot loader + secondary LDOS components |
| T01–T16 | Data area — user files and /SYS overlay modules |
| T17 | **System track** — GAT, HIT, directory (never used for file data) |
| T18–T34 | Data area — user files and /SYS overlay modules |

---

## System Track (Track 17)

| Sector | Contents |
|--------|----------|
| T17/S0 | **GAT** — Granule Allocation Table. 192 bytes, one per granule. Values: `0x00`=free, `0xFF`=last-in-chain, `0xFE`=system-reserved, else = next granule number |
| T17/S1 | **HIT** — Hash Index Table. 256 bytes. Each entry is the starting directory slot for files that hash to that value; accelerates directory search |
| T17/S2–S9 | **Directory** — 8 entries × 32 bytes per sector = 64 directory slots maximum |

### Directory Entry Format (32 bytes)

```
Offset  Len  Field
  0      1   Flags: 0x00=free, 0x10=active file, 0x20=active /SYS,
                    0x08=active dir entry, 0xFE=end of directory
  1      8   Filename, padded with spaces (not null-terminated)
  9      2   Extension (e.g. 'SY' for /SYS)
 11      1   Password/attribute byte
 12      2   EOF offset in last sector (LE)
 14      1   Month (BCD)
 15      1   Day  (BCD)
 16     16   Extent table: up to 4 × (gran_lo, gran_hi, lsn_lo, lsn_hi)
             gran = starting granule, 0xFF = unused slot
```

### Granule Addressing

A **granule** = 5 consecutive sectors on one track.
- Granule N → track = N ÷ 2, first sector = (N mod 2) × 5
- So granule 18 → track 9, sector 0 (i.e. T09/S0–S4)
- And granule 19 → track 9, sector 5 (i.e. T09/S5–S9)

---

## Boot Track (Track 0)

| Sector | Load addr | Region | Notes |
|--------|----------|--------|-------|
| T00/S0 | 0xFE00 | HIGH | **Primary boot loader** — ROM reads this first. Loaded to 0xFE00. Sets up stack, loads secondary boot block, displays `No System` / `Disk Error` messages. |
| T00/S1 | 0xFE00 | HIGH | **Secondary boot loader** — continues boot sequence, copies LDOS kernel to 0x4200 staging area. |
| T00/S2 | 0xC953 | HIGH | LDOS secondary boot component |
| T00/S3 | 0xFCFD | HIGH | LDOS secondary boot component |
| T00/S4 | — | — | Unused / padding (all zeros) |
| T00/S5 | 0x5354 | low  | LDOS secondary boot component |
| T00/S6 | 0xD701 | HIGH | LDOS secondary boot component |
| T00/S7–S9 | 0xE5E5 | HIGH | Padding / free-sector fill (`0xE5` = LDOS free-sector marker) |

> **Note**: The first two bytes of a LDOS module sector are the LE load address.
> `00 FE` → load address 0xFE00.  `11 F3` is **not** a load address — those are the
> first Z80 instructions: `LD DE,0xF311` part of the boot code.

---

## LDOS Kernel Sectors

The boot loader reads these sectors into a staging buffer at 0x5100, then the
copy loop at 0x4CDB/0x4CE6 copies each sector to its baked-in load address.

Sectors observed during boot (`[SECDST]` log), read in order:

| Read order | Sector | Load addr | Region | Contents |
|-----------|--------|----------|--------|----------|
| 1 | T17/S4 | — | — | System file index / config (not a module) |
| 2 | T09/S0 | 0x0605 | — | Directory data (not a RAM module) |
| 3 | T09/S1 | 0xBFBF | HIGH | LDOS high-RAM overlay module |
| 4 | T09/S2 | 0x4F43 | low  | LDOS low-RAM overlay (`CONFIG/SYS`) |
| 5 | T09/S3 | 0x8FC3 | HIGH | LDOS high-RAM overlay |
| 6 | T09/S4 | 0xC1EE | HIGH | LDOS high-RAM overlay |
| 7 | T09/S5 | 0x46F6 | low  | LDOS low-RAM code |
| 8 | T09/S6 | 0x3E06 | — | Not a RAM module (address < 0x4000) |
| 9 | T09/S7 | 0x9005 | HIGH | LDOS high-RAM overlay |
| 10 | T09/S8 | 0x53C4 | low  | LDOS low-RAM code |
| 11 | T09/S9 | 0xF3CD | HIGH | LDOS high-RAM overlay |
| 12 | T10/S0 | 0xCD00 | HIGH | LDOS high-RAM overlay |
| 13 | T10/S1 | 0xC5D5 | HIGH | LDOS high-RAM overlay |
| 14 | T10/S2 | 0xD1E5 | HIGH | LDOS high-RAM overlay |
| 15 | T10/S3 | 0xD300 | HIGH | LDOS high-RAM overlay |
| 16 | T10/S4 | 0xEBF7 | HIGH | LDOS high-RAM overlay |
| 17 | T10/S5 | 0xC467 | HIGH | LDOS high-RAM overlay |
| 18 | T10/S6 | 0x203F | — | Not a RAM module (address < 0x4000) |

**Of the 17 kernel sectors, 11 have high-RAM load addresses (>0x7FFF).**
Note: the docs previously claimed these are only copied when `0x403D` bit 3 = 1,
but that is **incorrect**. 0x403D bit 3 is the ROM display-width flag (32-column
vs 64-column video mode), not an expansion-RAM flag. The LDOS kernel at
0x4000–0xFFFF contains zero references to 0x403D. The actual mechanism
controlling whether high-RAM modules are loaded is in the LDOS disk boot sector
code and has not yet been fully disassembled.

---

## Module Copy Loop (`0x4CDB`)

After staging, the LDOS kernel copy routine works like this:

```
0x4CDB  LD (HL), A      ; write byte to destination
0x4CDC  CP (HL)         ; verify it was written (read-back)
0x4CDD  JR NZ, error    ; SYS ERROR if mismatch
0x4CDF  INC HL          ; advance destination pointer
...     DJNZ 0x4CDB     ; loop for BC bytes
```

The copy loop writes each sector's 254-byte payload to the address embedded
in bytes 0–1 of that sector.  HL starts at `load_addr`, BC = 0xFE (254 bytes).

Known copy destinations observed in practice:  

| Destination | Contents |
|------------|----------|
| 0x4E00 | SYS12 — first LDOS overlay (sets HIGH$ = 0xFFFF) |
| 0x4F00 | SYS12 continuation |
| 0x5000 | SYS12 continuation |
| 0x5100 | SYS12 continuation |
| 0x4BC9 | Secondary kernel overlay |

---

## Key LDOS RAM Variables

| Address | Name | Description |
|---------|------|-------------|
| 0x4000 | JMPDO | `JP nnnn` — LDOS SVC dispatcher entry point |
| 0x4003 | JMPNO | `JP nnnn` — LDOS NMI handler |
| 0x4006 | JMPTO | `JP nnnn` — initial entry point after boot |
| 0x403D | DSPFLG | **Display-width flag** — bit 3 = 32-column (double-wide) mode active. All ROM sites that read/write 0x403D immediately do `OUT (0xFF),a` (video control port) and adjust cursor arithmetic. **Not** an expansion-RAM flag. The LDOS kernel (0x4000–0xFFFF) never reads this address. Previously mislabelled `RAMFLG` in these notes. |
| 0x4040 | FCBMAP | Active FCB (File Control Block) allocation map |
| 0x4049 | HIGH$ lo | Low byte of top of available user RAM |
| 0x404A | HIGH$ hi | High byte (0xFF → HIGH$ = 0xFFFF on 48 KB machine) |
| 0x404B | LDRFLG | Loader flags |
| 0x50B0 | SVCJMP | SVC chain — `0xC3` (JP opcode) |
| 0x50B1 | SVCADR | SVC chain — target address (2 bytes LE) |
| 0x50B4 | SVCCNT | SVC entry count |
| 0x50B6 | SVCNXT | SVC chain link pointer (2 bytes LE, = 0x441E in ld1-531.dsk) |

---

## ROM Intercept Addresses (LDOS boot path)

| Address | Name | Role in LDOS boot |
|---------|------|------------------|
| 0x0543 | (ROM) | Reads 0x403D to check expansion RAM — LDOS boot bypasses this by jumping directly to 0x4200 load |
| 0x4200 | LDOS boot stage 2 | Staging area where boot sector is copied by ROM |
| 0x4259 | (LDOS kernel) | Writes SVC dispatcher bytes to 0x50B0–0x50B7 |
| 0x4CDB | (LDOS loader) | Module copy loop — `LD (HL),A` write + verify |
| 0x4CDF | (LDOS loader) | `INC HL` — advances copy-loop destination pointer |
| 0x4E00 | SYS12 | Entry point of first overlay module |
| 0x4E17 | SYS12 | Sets HIGH$ = 0xFFFF |

---

## Boot Sequence

```
1. ROM reset (0x0000)
   └─ reads 0x403D for memory size (result ignored by LDOS)
   └─ reads T00/S0 into ROM-controlled buffer
   └─ copies T00/S0 to 0x4200, jumps to 0x4200

2. LDOS primary boot (0x4200 = T00/S0 code)
   └─ DI, SP = 0x41E0
   └─ reads T17/S4 (system index) into 0x5100
   └─ reads kernel sectors T09/S0..T10/S6 into 0x5100 (staging)
   └─ copies each staged sector to its baked-in load address via 0x4CDB loop
      (both low-RAM and high-RAM modules are copied unconditionally;
       the exact gating mechanism is in the boot sector code,
       verified by LDOS successfully loading all kernel modules in practice)
   └─ writes SVC table to 0x50B0..0x50B7 (PC=0x4259)
   └─ jumps to module init chain at HIGH$ (= 0xFFFF)

3. SYS12 init (0x4E00)
   └─ sets HIGH$ = 0xFFFF (0x4049:0x404A)
   └─ calls next module init at address stored in HIGH$

4. Module init chain executes
   └─ SYS12 init, SYS0 init, etc. — LDOS reaches READY prompt
   └─ Prompts: "Date?" then "Time?" then boots to LDOS `READY` prompt
```

---

## Related Tools

| Script | Purpose |
|--------|---------|
| `tools/ldos_disk_map.py` | Generates this document from the disk image |
| `tools/scan_load_addrs.py` | Brute-force scan every sector for module load addresses |
| `tools/analyze_sys12.py` | Deep-dive into the SYS12 module structure |
| `tools/dump_sys12.py` | Raw hex dump of SYS12 sectors |
| `tools/find_loadaddr.py` | Search disk for specific address patterns |

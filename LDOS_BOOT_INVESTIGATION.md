# LDOS 5.3.1 Boot Investigation

**Goal:** Boot LDOS 5.3.1 from `disks/ld1-531.dsk` to the `LDOS Ready` command prompt.  
**Status: ✅ RESOLVED** — LDOS boots successfully to the date/time prompt and beyond.

---

## Summary of Fixes

Four changes were required to get LDOS booting. They are described in detail in
[§ Fixes](#fixes) below.

| # | File | Change | Why |
|---|---|---|---|
| 1 | `src/system/Bus.cpp` | Place `RET` (`0xC9`) at `0xFFFF` on startup | LDOS `HIGHCALL` chain terminates at `HIGH$=0xFFFF`; needs graceful return |
| 2 | `src/system/Bus.cpp` | Force bit 3 of `0x403D` when read from PC ≥ `0x4000` | Tells LDOS 48 KB expansion RAM is present so it loads high-RAM modules |
| 3 | `src/fdc/FDC.cpp` | Fix RECTYPE: deleted mark only for T17/S2+ and data-track sectors starting `0x1F` | LDOS uses RECTYPE to distinguish module header sectors from code sectors |
| 4 | `src/Emulator.cpp` | Add `--auto-ldos-date` flag | Convenience: auto-inject date/time so tests don't require manual keyboard input |

---

## Boot Sequence (Fully Traced)

### Phase 1: ROM → Boot Sector

ROM reads **T00/S0** → **0x4200** (256 bytes) and jumps there.

```
[SECDST] T00/S0 → dest=0x4200  PC=0x06C5
```

### Phase 2: Boot Loader at 0x4200 — Config Read

- Reads **T17/S4** (config sector) → staging at **0x5100**.
- Reads `(0x5100)` = `0x5F`; bit 4 is set → **48 KB path taken** ✓
- Reads word at `(0x5116)` = `0x0309` → track=9, groups=3.

### Phase 3: Boot Loader — Stream Sectors to Staging

Reads **T09/S0 through T10/S6** (17 sectors) as a byte stream into **0x5100**
staging buffer. The stream record format:

```
Type 0x01 = PATCH:  01 [N] [addr_lo] [addr_hi] [data × (N-2)]
Type 0x02 = JUMP:   02 [??] [addr_lo] [addr_hi]  → JP(HL) to addr
Type 0x05 = SKIP:   05 [N] [ignored × N]
Type 0x1F = INFO:   1F [N] [text × N]
```

The stream installs PATCH records that write LDOS kernel code into RAM at
`0x400C–0x461C`, then terminates with `JUMP 0x4E00`.

### Phase 4: First Dispatch — JP(HL) → 0x4E00 (SYS12)

The boot loader fires `JP (HL)` = `0x4E00` (the SYS12 module entry point).
SYS12 is installed entirely by the stream PATCH records; it lives in low RAM.

```
[JPHL] boot JP(HL)=0x4E00  AF=4E01 BC=0000 DE=1104 SP=41E0
```

SYS12 first action: write `0x36` to **0xFF00** (first high-RAM write).

```
[HIGHRAM] first write to page 0xFF00  val=0x36  PC=0x4E0C
```

SYS12 then sets `HIGH$` (stored at `0x404A:4B`) and calls the second-stage
loader at **0x4777** (`CALL 0x4777`).

### Phase 5: Second-Stage Loader at 0x4777

The second-stage loader reads a sequence of sectors to load the LDOS high-RAM
modules. All reads go via the FDC, byte-by-byte through the DRQ data register
at `0x37EF`, into a destination address kept in `BC`.

#### Call chain into the DRQ copy loop

```
0x4777 → CALL 0x478F (sets IY = 0x4700 + entry*10)
       → LD A,0x20; OR A; CALL 0x4BCB
0x4BCB → JP (IY)           ; IY = 0x4700 (entry 0)
0x4700 → JP 0x45FB         ; entry table slot 0
0x45FB → LD A,B; OR A      ; B=9, A=9
0x4602 → JR NC,0x466b      ; 9 ≥ 7 → NC taken
0x466b → [FDC sector read sequence: CALL 0x469a per sector]
```

#### Self-modifying DRQ copy loop

The DRQ copy loop at `0x46CB–0x46D4` starts as dead code (NOPs) and is
**patched by the first sector read (T00/S2)**:

| Address | Before T00/S2 | After T00/S2 | Meaning |
|---|---|---|---|
| `0x46D1` | `00` NOP | `1A` LD A,(DE) | DE = 0x37EF (FDC data register) |
| `0x46D2` | `00` NOP | `02` LD (BC),A | BC = destination address |
| `0x46D3` | `03` INC BC | `03` INC BC | (unchanged) |
| `0x46F6` | `3E 00` LD A,0 | `3E 01` LD A,1 | patch-complete flag |

T00/S2 is a special bootstrap sector: reading it installs the copy microcode
into the loop itself, so all subsequent sector reads are copied correctly.

#### Sector sequence read by the second-stage

```
T00/S2  → relay 0x4200  load(LE)=0xC953  CODE [HIGH]   ← installs copy microcode
T17/S0  → relay 0x4200  DIR (all 0xFF)
T00/S0  → relay 0x4200  load=0xFE00      CODE [HIGH]
T17/S0  → relay 0x4200  DIR (all 0xFF)
T17/S8  → relay 0x4200  DIR
T20/S5  → relay 0x4200  HEADER "Copyright"  RECTYPE=1  load=0x7420
T20/S6  → relay 0x4200  load=0xCB47     CODE [HIGH]
T20/S7  → relay 0x4200  load=0x0528     CODE [low]
T20/S8  → relay 0x4200  load=0x6574     CODE [low]
```

T20/S5 is detected as a **module header sector** (RECTYPE=1, bytes[0]=`0x1F`).
The second-stage parses the descriptor to find the module name and load address;
T20/S6–S8 are code sectors copied to the module's load address.

### Phase 6: HIGHCALL Chain Runs — Modules Initialise

After the second-stage returns, SYS12 executes a sequence of `HIGHCALL`s:
jumps into high RAM (> `0x7FFF`) to call each loaded module's init routine.

```
HIGHCALL#1: 0x4BEE → 0xC954   (SYS0 init)
HIGHCALL#2: 0x4537 → 0xCE10   (SYS1 init)
HIGHCALL#3: 0x4537 → 0xE9C1   (SYS3 init)
HIGHCALL#4: 0x494E → 0xFAFA   (SYS12 relocate)
```

Because `HIGH$=0xFFFF`, the chain eventually reaches `0xFFFF`. With the `RET`
placed there (fix #1), it returns gracefully. Without it, execution fell through
to the ROM reset vector at `0x0000`.

### Phase 7: LDOS Ready Prompt

LDOS displays the date/time prompt, accepts `01/01/84` and `00:00:00`, then
shows `LDOS Ready`.

---

## Fixes

### Fix 1 — RET at 0xFFFF (`src/system/Bus.cpp`)

```cpp
// Bus constructor (after zeroing RAM):
ram[0xFFFF - RAM_START] = 0xC9;   // RET — terminates the HIGHCALL chain
```

**Why it was needed:**  
LDOS sets `HIGH$` to `0xFFFF` on a 48 KB system.  The HIGHCALL dispatch chain
walks a linked list of module init routines in high RAM; the final entry in the
chain jumps to `HIGH$` (`0xFFFF`) as a sentinel.  With no instruction there,
execution fell into the ROM reset vector at `0x0000`, which looked like a crash:

```
[CRASH] PC 0xFFFF→0x0000  SVC=0x441E
```

Placing `RET` (`0xC9`) at `0xFFFF` lets the chain terminate cleanly.

---

### Fix 2 — Force 48 KB flag at `0x403D` (`src/system/Bus.cpp`)

```cpp
// In Bus::read(), when reading address 0x403D:
if (addr == 0x403D) {
    if (last_cpu_pc_ >= 0x4000) {
        value |= 0x08;  // tell LDOS: 48 KB expansion RAM present
    }
}
```

**Why it was needed:**  
LDOS reads `0x403D` bit 3 to decide whether to load high-RAM modules
(`SYS0` → `0xC953`, `SYS1` → `0xCE10`, `SYS3` → `0xE9C1`, etc.).
When booting from disk, LDOS jumps to the boot loader before the ROM's
RAM-detection routine can set this flag; the flag stays `0x00`, LDOS concludes
it's on a 16 KB machine, skips all high-RAM module loads, and crashes.

The fix intercepts reads from LDOS code (PC ≥ `0x4000`) and forces bit 3 high.
ROM reads (PC < `0x4000`) see the real value, preserving normal ROM behaviour
(e.g. 32/64-column display detection).

Note: an earlier approach of forcing bit 3 on *writes* to `0x403D` in
`Bus::write()` was a dead end — LDOS never writes the flag on a disk boot, only
reads it.

---

### Fix 3 — RECTYPE for module header sectors (`src/fdc/FDC.cpp`)

```cpp
// Before (wrong):
bool deleted = (t == 17);

// After (correct):
bool deleted = ((t == 17) && (s >= 2))           // T17 dir sectors S2-S9
            || ((t != 17) && (bytes[0] == 0x1F)); // data-track header sectors
```

**Why it was needed:**  
The FD1771 status register bit 5 (RECTYPE) signals whether a sector was written
with a *deleted data address mark* (RECTYPE=1) or a normal mark (RECTYPE=0).
LDOS's second-stage loader uses RECTYPE to distinguish two sector roles:

- **RECTYPE=1 (deleted mark) → header sector:** parse the LDOS module descriptor
  (name, load address, size). Do not copy bytes to RAM.
- **RECTYPE=0 (normal mark) → code sector:** copy raw bytes to the load address
  from the preceding header.

On real TRS-80 hardware, track-17 directory entry sectors (S2–S9) and
data-track module header sectors (whose first byte is `0x1F`) were formatted with
deleted data marks. The GAT (`T17/S0`) and HIT (`T17/S1`) use normal marks.

The old code treated **all** track-17 sectors as deleted (wrong for S0/S1) and
never set RECTYPE for any data-track sector (so all module headers were treated
as code sectors, corrupting the load).

JV1 disk images carry no per-sector metadata, so RECTYPE must be inferred from
content. The `bytes[0] == 0x1F` heuristic is reliable because `0x1F` is the
LDOS module header length-prefix byte and does not appear as the first byte of
any legitimate code sector.

---

### Fix 4 — `--auto-ldos-date` flag (`src/Emulator.cpp`)

```cpp
// CLI parse:
else if (std::strcmp(argv[i], "--auto-ldos-date") == 0)
    auto_ldos_date_ = true;

// Per-frame VRAM scan in step_frame():
// Looks for "Date ?" (0x44 0x61 0x74 0x65 0x20 0x3F) anywhere on screen.
// On match, injects "01/01/84\n00:00:00\n" via KeyInjector.
```

**Why it was added:**  
Not a correctness fix — purely a test convenience. Without it, every automated
boot test requires manual keyboard input to answer the LDOS date/time prompt.
With `--auto-ldos-date`, the emulator scans VRAM every frame and auto-injects a
fixed date/time the moment the prompt appears, allowing unattended boot testing.

Usage:
```
./mal-80 --disk disks/ld1-531.dsk --auto-ldos-date
```

---

## RAM State at Boot (Key Addresses)

### LDOS System Variables (0x4040–0x40FF)

```
404A:4B  HIGH$      0xFF FF  — top of high RAM (48 KB system)
404C:4D  (pointer)  0xC0 80  — something in high RAM
404E:4F  (pointer)  0x45 37  — 0x4537 (SVC dispatcher entry)
```

Pattern `37 45 37 45 ...` repeating at 0x404E+ is a linked list of `0x4537` entries in the SVC chain.

### SVC Chain at 0x50B0

After boot, `0x50B0` contains the SVC dispatcher. The tail of the chain
points to `0x0033` (a ROM routine), which is normal — LDOS chains ROM SVCs.

```
50B0: F1          POP AF         ; SVC entry: restore AF
50B1: C6 3A       ADD A,0x3A
50B3: C3 33 00    JP 0x0033      ; tail: fall through to ROM SVC handler
```

### High-RAM Modules Loaded (confirmed by [HIGHRAM] logger)

```
0xFF00   first write PC=0x4E0C   (SYS12 setup)
0xFA00   first write PC=0x427D   (SYS12 relocate / HIGHCALL#4)
0xC900+  SYS0 — loaded by second-stage (verified by [MODWR])
0xCE00+  SYS1 — loaded by second-stage
0xE900+  SYS3 — loaded by second-stage
```

---

## Instrumentation (still in source)

### `src/fdc/FDC.cpp`
- `[FDC]` — every `execute_command()` call (command name, track, sector, drive)
- `[FDC] RNF!` — Record Not Found errors with head/sector state
- `[SEC]` — every sector read: type (DIR / HEADER / CODE), load address, first 4 data bytes

### `src/system/Bus.cpp`
- `[HIGHRAM]` — first write per 256-byte page in 0x8000–0xFFFF (shows PC)
- `[MODWR]` — first 24 writes to the three critical module pages (0xC9xx, 0xCExx, 0xE9xx)
- `[403D]` — every read of `0x403D` showing real vs forced value and PC
- `[MEMRD]` — first read of each byte in `0x4040–0x406F` (LDOS system variable area)
- `[SVC]` — when SVC dispatcher bytes at `0x50B0–0x50B7` are saved (PC=0x4259)
- `[CRASH]` — dumps `0x4040–0x40FF`, `0x4CC0–0x4CFF`, `0x50A0–0x50CF` on RST 0

### `src/Emulator.cpp`
- `[4777ENTRY]` — hex dump of `0x4600–0x4900` on first entry to `0x4777` (pre-patch)
- `[4777RET]` — hex dump of `0x4000–0x50FF` on return from `0x4777` (post-patch)
- `[S12#N]` — 500-entry instruction trace while PC is in `0x4500–0x4CFF` (SYS12 range)
- `[HIGHCALL#N]` — fires on any transition from PC ≤ `0x7FFF` to PC > `0x7FFF`
- `[VDUMP]` — periodic VRAM row dump (every 60 frames) when `--auto-ldos-date` active
- `[CRASH]` — dumps key RAM ranges on RST 0 at `0xFFFF→0x0000`

---

## Tools

| File | Purpose | Status |
|---|---|---|
| `tools/disasm_45fb.py` | Z80 disassembler; reads hex dump from `log.txt` `[4777RET]` section, disassembles from named addresses | Working |
| `tools/disasm_boot.py` | Disassembler for the T00/S0 boot sector (loaded at 0x4200) | Working |
| `tools/sim_boot_stream.py` | Boot stream record parser simulator | **Broken** — misidentifies Z80 opcode bytes as record-type bytes |
| `tools/raw_stream.py` | Dumps raw T09/S0+S1 bytes for manual inspection | Working |
| `tools/find_sys12.py` | Scans disk directory and all sectors for SYS/12 module | Working |
| `tools/ldos_disk_map.py` | LDOS disk sector map | Working |
| `tools/scan_load_addrs.py` | Scans sectors for load addresses | Working |
| `docs/LDOS_DISK_LAYOUT.md` | Comprehensive LDOS JV1 disk format reference | Working |

---

## Disk Layout (Quick Reference)

```
JV1: 35 tracks × 10 sectors × 256 bytes = 89,600 bytes
Sector offset = (track×10 + sector) × 256

T00/S0     Boot loader (loads to 0x4200)
T00/S2     Second-stage bootstrap (installs DRQ copy microcode; loads to relay 0x4200)
T17/S0     GAT (Granule Allocation Table)  — normal data mark
T17/S1     HIT (Hash Index Table)           — normal data mark
T17/S2–S9  Directory entries (8 × 32-byte records/sector) — deleted data mark
T17/S4     Boot config: byte[0]=0x5F (bit4→48KB path), word[0x16]=0x0309 (track=9, groups=3)
T09/S0–T10/S6  Boot stream (17 sectors → 0x5100 staging; PATCH records install SYS12)
T20/S5     SYS module header (RECTYPE=1, bytes[0]=0x1F, "Copyright", load=0x7420)
T20/S6–S8  SYS module code sectors (RECTYPE=0, copied to load address)
```

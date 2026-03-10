# LDOS 5.3.1 DIR :0 "ILLEGAL DRIVE NUMBER" — Investigation Notes

## The Problem

When LDOS 5.3.1 boots on mal-80 and the user types `DIR :0`, LDOS responds with
`ILLEGAL DRIVE NUMBER` instead of listing the disk directory.

LDOS boots successfully to the `READY` prompt. Only the DIR command (and presumably
other disk commands) fails.

---

## Error Location

The error triggers at **PC=0x4409**: `ld a,0x96; rst 28h` (error code 0x96 =
"ILLEGAL DRIVE NUMBER"). This is reached via:

```
0x4FE0: call 0x4B45
0x4FE0: jp nz, 0x4409   ← fires if 0x4B45 returns NZ
```

---

## The Drive Validation Call Chain

```
0x4FE0  (DIR command path)
  └─ call 0x4B45         (drive validation entry)
       └─ call 0x4B5E    (inner probe)
            └─ call 0x4777   (B=9 probe dispatcher)
                 └─ call 0x4BCB  (JP(IY) → 0x4700 → JP 0x45FB)
                      └─ 0x45FB  (LDOS disk driver, B=9 → dispatch to 0x466B)
                           └─ 0x466B  (Read Sector with RECTYPE check)
```

**For success:** the driver must return **A=6**.
`0x4B5E` does `sub 6` after the driver returns; Z flag (A=6) = success. If NZ,
0x4B45 tries fallback probes, then returns NZ → 0x4FE0 → "ILLEGAL DRIVE NUMBER".

---

## How the Driver Returns A=6 (the RECTYPE mechanism)

The LDOS driver at 0x466B issues a **Read Sector** command (FDC command 0x88).
After transferring all 256 bytes, it reads the **final FDC status register**
(at PC=0x46D8) and does:

```z80
ld a,(hl)       ; A = final FDC status (HL=0x37EC)
and 0x7C        ; mask error bits
ret z           ; return A=0 if no errors (SUCCESS PATH → returns A=0, NOT 6!)
```

**The `ret z` path returns A=0** (no errors), which does NOT give A=6. This is the
wrong path for validation purposes.

**The A=6 path** (through the bit-counting loop at 0x46F5) is reached when:

1. Final status has **bit 5 set** (0x20 = ST_RECTYPE, "deleted data mark")
   AND bits 3,4 clear (no CRC/RNF error)
2. Code at 0x46E4 does `and 0x18` → zero → `jr z,$+11` → jumps to 0x46F3
3. `pop af` → A=0x20, `ld b,a` → B=0x20
4. Bit-counting loop (rrc B repeatedly until carry=1):
   - B=0x20 requires exactly 6 rotations before carry=1
   - Returns **A=6** ✓

So: **the sector being probed must have RECTYPE (bit 5) set in the final FDC status**.

---

## What Sets RECTYPE: The FD1771 Deleted Data Mark

On the FD1771 (Model I FDC), a sector read returns **bit 5 of status = 1** when
the sector's physical Data Address Mark is **FA** (deleted data) instead of **FB**
(normal data).

In the TRS-80 LDOS/TRSDOS standard disk format:
- **Track 17 (directory track)** is formatted with **FA-type** (deleted) sector
  headers on all 10 sectors.
- All other data tracks use **FB-type** (normal) headers.

### xtrs confirms this

In `xtrs/trs_disk.c` (the reference emulator), for JV1 format at Read Sector setup:

```c
if (d->phytrack == 17) {
    if (state.controller == TRSDISK_P1771) {
        new_status = TRSDISK_1771_FA;  // = 0x20 = deleted mark
    }
}
// Other tracks: new_status stays 0 (normal)
```

This sets status = `BUSY | DRQ | 0x20` at the start of any track-17 read.
After transfer completes, BUSY and DRQ clear, leaving **0x20** as the final status.

---

## Our FDC Implementation (Current State)

In `src/fdc/FDC.cpp`, `cmd_read_sector()`:

```cpp
bool deleted = (t == 17);
status_ = ST_BUSY | ST_DRQ | (deleted ? ST_RECTYPE : 0x00);
```

When all bytes are consumed:
```cpp
status_ &= ~(ST_BUSY | ST_DRQ);  // preserves ST_RECTYPE
```

**This is correct** — our implementation matches xtrs exactly for track 17.

---

## What the Probe Actually Seeks To

The B=9 driver path (0x466B → 0x469A → 0x462A) includes a **Seek** before reading.
The seek target is passed via the **DE register**:

- `ld (0x37EE),de` at 0x464F is a 16-bit write:
  - E → sector register (0x37EE)
  - D → **data register** (0x37EF) ← the Seek target track
- `ld (iy+5),d` updates the DCT's current-track field
- Seek command 0x1B is then issued

So **D = target track** when the driver is called, passed through from the caller's
DE register up through the entire 0x4FE0 → 0x4B45 → ... → driver chain.

---

## What the Log Shows

From watchpoint log `/tmp/wp5.txt` (16684 lines, captured during a failing run):

| Track | Sector | Final status at PC=0x46D8 | Driver returns |
|-------|--------|--------------------------|----------------|
| 17    | 0      | **0x20** (RECTYPE) ✓     | A=6 → Z        |
| 17    | 2      | **0x20** (RECTYPE) ✓     | A=6 → Z        |
| 17    | 5      | **0x20** (RECTYPE) ✓     | A=6 → Z        |
| 18    | 5–9    | 0x00                     | A=0 → NZ ✗     |
| 21    | 0–9    | 0x00                     | A=0 → NZ ✗     |

Track 17 reads return RECTYPE correctly. Track 18 and 21 reads do not.

**Pattern:** LDOS loads modules by first reading a track-17 directory entry (gets
RECTYPE → A=6 → passes first probe), then reads the actual module data from tracks
18, 21, etc. The ERROR triggers when the **final validation probe** (called from
0x4FE0 after module loading completes) runs with the head on track 18 (the last
accessed track), and that returns A=0 → NZ → error.

---

## What We Investigated and Rejected

### FDC initial status (0x84 vs 0x04) ← FIXED, not the root cause
- Original: status_ = ST_TRACK0 (0x04) on disk load
- xtrs uses: NOTREADY | TRACK0 (0x84)
- Changed to 0x84 — this fixed DCT initialization (DCT fields were zeroed before)
- Did NOT fix the DIR error

### 0x403D — display-width flag, NOT an expansion-RAM flag ← CLOSED
- Previously mislabelled `RAMFLG`; forcing bit 3 was tried and reverted
- Disassembly of all ROM sites confirms: **bit 3 = 32-column wide-char mode**
  - `0x04C3`: `ld a,(0x403D); and 0xF7; ld (0x403D),a; out (0xFF),a` — clears bit 3, asserts on video port
  - `0x04F6`: `ld a,(0x403D); or 0x08; ld (0x403D),a; out (0xFF),a` — sets bit 3, asserts on video port
  - `0x04CE`, `0x0543`: bit 3 used to `dec hl`/`inc hl` around video RAM pointer — cursor width arithmetic
- **There are zero references to 0x403D in LDOS kernel code (0x4000–0xFFFF)**
- Forcing bit 3 set caused double-spaced output because it put the ROM into 32-column display mode
- No write interception of 0x403D should be applied; this is unrelated to the DIR bug

### Drive table / DCT confusion
- The drive table at 0x4700 (IY entries, `JP 0x45FB` + parameters) is DIFFERENT
  from the DCT at 0x4044 (runtime disk state)
- Confirmed: IY for drive 0 → 0x4700, IY+5 = current track, IY+7 = 9 (sectors/track)

---

## Current Status (Bug Persists)

### RECTYPE fix applied (necessary but not sufficient)

**Old code** (now replaced):
```cpp
bool deleted = ((t == 17) && (s >= 2))
            || ((t != 17) && (bytes[0] == 0x1F));
```
This excluded T17/S0 and T17/S1 from RECTYPE. Since DIR probes sector 0, this
definitely would have caused failure.

**Current code** (in `src/fdc/FDC.cpp`):
```cpp
bool deleted = (t == 17);
```
All 10 sectors on track 17 get RECTYPE set — matching xtrs and the real JV1 format.

### What watchpoints confirm

Watchpoint at 0x4B45 (VALIDATE_ENTRY):
- D=0x11=17 every time ✓ (directory track loaded correctly by `call 0x4B65`)
- E=0x00 (sector 0) — probe reads T17/S0

FDC logging shows seeks to T17 before the validation read. With current RECTYPE code,
T17/S0 returns status=0x20 → the bit-count loop → A=6 → sub 6 → Z.

### Bug still occurs

Despite the RECTYPE fix and correct D=17 entry, typing `DIR :0` manually still
produces **ILLEGAL DRIVE NUMBER**.

**Key discrepancy**: `--auto-ldos-date` automated mode (injects date/time + `dir :0`
via turbo keyboard injection) appears to reach the READY prompt and then issue the
DIR command — the exact same code path. The difference between automated and manual
invocation is the primary clue remaining.

### Suspected areas for further investigation

1. **FDC state at validation time**: Is the FDC in the right state when the manual
   DIR :0 is typed? Specifically: is `track_` (the FDC's physical head position)
   correct, or could a previous command have left the head in a bad state?

2. **Sector register vs probe sector**: The probe passes E=0 as the sector number.
   Does our `cmd_read_sector()` use the sector register (`sect_`) or the E value
   directly? LDOS writes sector via `ld (0x37EE),de` (E→0x37EE = sector reg) before
   issuing the read command.

3. **Command sequencing**: Manual typing is much slower than auto-inject. Could there
   be an interrupt-driven FDC state machine issue that manifests at normal speed but
   not turbo?

---

## Session 2 Findings (from z80dasm disassembly of memdump.bin)

### RST 38h (IM1 interrupt) actually jumps to 0x4012, not 0x45C0

The ROM at 0x0038 contains `JP 0x4012` (bytes `C3 12 40`). LDOS has patched the
ROM-region jump vector so that the 60Hz timer interrupt goes to 0x4012 in LDOS
kernel RAM, NOT directly to 0x45C0. 0x45C0 is a sub-handler called later in the
chain. Need to disassemble 0x4012 to understand the full interrupt path.

### SVC chain at 0x50B0 — bytes don't match docs

memdump shows:
```
[50B0] = 0xF1  (POP AF)     ← docs said this should be 0xC3 (JP opcode)
[50B1] = 0xC6
[50B2] = 0x3A
[50B3] = 0xC3               ← JP here, address bytes follow
[50B4] = 0x33
[50B5] = 0x00               ← JP target = 0x0033 (ROM SVC handler)
[50B6] = 0x1E
[50B7] = 0x44               ← 0x441E (next SVC chain link)
```
The SVC chain starts with `F1` (POP AF) not `C3` (JP). This suggests the chain
entry point is BEFORE 0x50B0, and 0x50B0 is mid-chain. Or the chain format is
different from what was assumed. **0x50B0 is NOT the SVC entry JP opcode.**

### 0x4022 drive data template block at 0x422A

memdump shows `[422A..4233]` = `C3 8A 30 03 42 FF 22 09 24 11`
- bytes 0-2: `JP 0x308A` — a different dispatch address than drive 0's `JP 0x45FB`
- byte 3: 0x03 (flags, same as drive 0)
- byte 4: 0x42 (flags2 — different from drive 0's 0x01)
- byte 5: 0xFF — likely "no current track" sentinel

During DIR_CMD, `ldir` copies 70 bytes from 0x422A (or 0x427A) to 0x470A
(drives 1–7 table entries). Drive 0 at 0x4700 is NOT overwritten.

### 0x4892 caller context — VALIDATE_DRIVE used for file-open path

At 0x4892, just before `call 0x4B10` (VALIDATE_DRIVE):
```
488C  ld b,(IX+7)    ; B = sectors-per-track from some file/FCB structure
488F  ld c,(IX+6)    ; C = drive number from same structure
4892  call 0x4B10    ; VALIDATE_DRIVE with C=drive#, B=spt
4895  ret nz         ; bail if drive not valid
...
48AA  jp 0x4B1F      ; jump to second validation variant
```
This call path is part of a file-open or directory-search operation. IL uses
IX to point to a File Control Block or directory entry structure. The C register
(drive number) comes from `(IX+6)` not from any command-line parse.

### 0x4C2B caller context — VALIDATE_DRIVE used during module load

At 0x4C2B, just before `call 0x4B10`:
```
4C25  sbc hl,hl      ; HL = 0x0000
4C27  ld (0x44AA),hl ; clear address field
4C2A  ld c,h         ; C = 0 (drive 0!)
4C2B  call 0x4B10    ; VALIDATE_DRIVE with C=0 (drive 0)
4C2E  jr nz,0x4C4B   ; bail → error "SYStem Error" path @ 0x4C4B
4C30  bit 4,(hl)     ; test bit 4 of returned HL address (directory entry flag?)
4C32  jr z,0x4C4B    ; not a valid entry → error
```
This caller is in the LDOS module loader (~0x4BF0–0x4C50). It validates drive 0
before reading a /SYS module from disk. If VALIDATE_DRIVE fails here, LDOS prints
"SYStem Error" and jumps to 0x4030 (probably re-prompt). This is a **boot-time**
call, not the DIR command path.

### 0x4BF0 — self-modifying NOP, called 0x4BE4 is suspicious

```
4BE4  call 0x0000    ; ← CD 00 00: CALL to address 0x0000!
                     ;   This must be a self-modified indirect call. At runtime
                     ;   the 0x00 0x00 address bytes get overwritten with a real target.
4BE7  push af
4BE8  ld a, 0x00
4BEA  ld (0x4315),a  ; stores 0x00 to 0x4315 (some flag)
4BED  pop af
4BEE  ret
4BEF  dec a          ; ← 0x4BEF
4BF0  nop            ; ← 0x4BF0: patched to 0xC8 (RET Z) by DIR command wildcard code
4BF1  jr nz,0x4C4B   ; if not patched, falls through to module-loader error path
```
The `call 0x0000` at 0x4BE4 is a self-modified call site. Before it executes,
something must patch bytes at 0x4BE5/4BE6 with a real address. In the static
memdump they show as 0x0000 (zeroed = unpatched at snapshot time).

### Key runtime values confirmed from memdump.bin

| Address | Value | Meaning |
|---------|-------|---------|
| 0x403D | 0x00 | Display-width flag — 64-column mode (correct, leave alone) |
| 0x4049 | 0xFF | HIGH$ lo byte |
| 0x404A | 0xFF | HIGH$ hi byte → HIGH$ = 0xFFFF (48KB confirmed) |
| 0x404B | 0x80 | LDRFLG |
| 0x4700 | C3 FB 45 | Drive 0: JP 0x45FB (DRIVER_ENTRY) ✓ |
| 0x4705 | 0x00 | Drive 0 IY+5: current head track = 0 ✓ |
| 0x4709 | 0x11 | Drive 0 IY+9: directory track = 17 ✓ |
| 0x4308 | 0x00 | saved_C = 0 |
| 0x4309 | 0x01 | saved drive select byte = 1 (drive 0 selected) |

---

## Key Files

| File | Relevance |
|------|-----------|
| `src/fdc/FDC.cpp` | `cmd_read_sector()` — RECTYPE logic (correct for track 17) |
| `src/system/Bus.cpp` | Watchpoint logging; add watchpoint at 0x4B45 |
| `memdump.bin` | 64KB memory snapshot used for LDOS disassembly |
| `/tmp/wp5.txt` | Watchpoint log (16684 lines) from failing DIR :0 run |
| `xtrs/trs_disk.c` | Reference: JV1 RECTYPE only on track 17 (confirmed correct) |
| `docs/LDOS_DISK_LAYOUT.md` | LDOS disk structure reference |
| `docs/ldos_symbols.md` | All known LDOS addresses and their meanings |
| `docs/ldos_disk_driver_asm.md` | Annotated disassembly: 0x45E0–0x46FF (driver, RECTYPE) |
| `docs/ldos_validation_asm.md` | Annotated disassembly: 0x4B00–0x4BFF (validation) |
| `docs/ldos_dir_cmd_asm.md` | Annotated disassembly: 0x4FC0–0x5020 (DIR command) |
| `docs/ldos_dispatch_asm.md` | Annotated disassembly: 0x4700–0x47FF (drive dispatch) |

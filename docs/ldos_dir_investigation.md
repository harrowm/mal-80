# LDOS 5.3.1 DIR :0 "ILLEGAL DRIVE NUMBER" — Investigation Notes

## The Problem

When LDOS 5.3.1 boots on mal-80 and the user types `DIR :0`, LDOS responds with
`ILLEGAL DRIVE NUMBER` instead of listing the disk directory.

LDOS boots successfully to the `READY` prompt. Only the DIR command (and presumably
other disk commands) fails.

---

## Error Location

The error triggers at **PC=0x4409**: `ld a,0x96; rst 28h` (error code 0x96 =
"ILLEGAL DRIVE NUMBER").

**There are two distinct jp-nz paths to 0x4409:**

| Jump site | From | Via |
|-----------|------|-----|
| `jp nz, 0x4409` at 0x4FE0 | DIR_CMD (0x4FC0) | after `call 0x4B45` (VALIDATE_ENTRY) |
| `jp nz, 0x4409` at 0x4C67 | SVC 0xB3 handler | after `call 0x4430` (= jp 0x4C77 = drive table check) |

### Session 4 discovery: which path fires for `DIR :0`?

Watchpoints added at 0x4B45 (VALIDATE_ENTRY), 0x4C77, and 0x4409 revealed:

- **0x4B45 never fires** when `DIR :0` is typed manually — only during boot (6 hits).
- **0x4C77 never fires** — the 0x4C5E→0x4C77 path is also not taken.
- **0x4409 fires once** with: `B=0x08, A=0xE0, ret=0x5A3C`.

The return address **0x5A3C** falls in the DISKDIR command module loaded into upper
RAM (0x5A00+). So the DIR module calls a LDOS SVC from ~0x5A39, and some path
inside that SVC reaches `jp nz, 0x4409` — **not** via either of the two jp sites
documented above. There must be a third jp (or call) to 0x4409 yet to be found.

**Critical observation**: B=0x08 at the error site. The DIR module encodes `:0`
as 0x08 in the B register when calling the LDOS SVC. Whether 0x08 is the correct
LDOS encoding for drive 0, or whether B=0x08 is itself the bug, is the next
question to answer.

---

## The Drive Validation Call Chain (boot-time module loading only)

This path is used when LDOS loads `/SYS` modules at boot, NOT for the manual DIR
command:

```
0x4FDD  (DIR_CMD at 0x4FC0)
  └─ call 0x4B45         (VALIDATE_ENTRY — fired 6× during boot, never for DIR :0 manual)
       └─ call 0x4B5E    (inner probe)
            └─ call 0x4777   (B=9 probe dispatcher)
                 └─ call 0x4BCB  (JP(IY) → 0x4700 → JP 0x45FB)
                      └─ 0x45FB  (LDOS disk driver, B=9 → dispatch to 0x466B)
                           └─ 0x466B  (Read Sector with RECTYPE check)
```

**For success:** the driver must return **A=6**.
`0x4B5E` does `sub 6` after the driver returns; Z flag (A=6) = success. If NZ,
0x4B45 tries fallback probes, then returns NZ → 0x4FDD → "ILLEGAL DRIVE NUMBER".

The DIR command module in upper RAM (0x5A00+) takes a completely different path
that has not yet been fully traced.

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

**Session 4 finding**: `DIR :0` typed manually does NOT go through VALIDATE_ENTRY
(0x4B45) at all. The 6 VALIDATE_ENTRY hits seen in watchpoint logs all happen at
LDOS boot time (loading /SYS modules). When a user types `DIR :0`, the DISKDIR
command module (loaded into upper RAM ~0x5A00) calls a different LDOS SVC path.
That path reaches `jp nz, 0x4409` via 0x4C67 or another site, with B=0x08.

**B=0x08 mystery**: The DISKDIR module passes B=0x08 when calling the LDOS drive
validation SVC. In the LDOS drive encoding, `:0` should map to drive number 0.
Whether 0x08 is the expected encoding (e.g. a bitmask where bit 3 = drive 0 in
some LDOS convention), or whether 0x08 is wrong (bug in the module or in how our
emulator initialises the drive table), is the key remaining question.

**Next investigation**: Expand WP4409 to dump 8 stack frames to reconstruct the
full call chain from 0x5A38 to 0x4409, and dump mem[4700-470F] to see the live
drive 0 table at error time.

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
| `run_wp404.txt` | Watchpoint log: 0x4040 writes and FDC seeks during boot+DIR |
| `run_wp49.txt` | Watchpoint log: similar, second run |
| `xtrs/trs_disk.c` | Reference: JV1 RECTYPE only on track 17 (confirmed correct) |
| `docs/LDOS_DISK_LAYOUT.md` | LDOS disk structure reference |
| `docs/ldos_symbols.md` | All known LDOS addresses and their meanings |
| `docs/ldos_disk_driver_asm.md` | Annotated disassembly: 0x45E0–0x46FF (driver, RECTYPE) |
| `docs/ldos_validation_asm.md` | Annotated disassembly: 0x4B00–0x4BFF (validation) |
| `docs/ldos_dir_cmd_asm.md` | Annotated disassembly: 0x4FC0–0x5020 (DIR command) |
| `docs/ldos_dispatch_asm.md` | Annotated disassembly: 0x4700–0x47FF (drive dispatch) |

---

## Session 3 Findings (from z80dasm disassembly of 0x4012, 0x4518, 0x4DA6, 0x4750, 0x4FC0)

### RST 38h interrupt chain — fully traced

```
ROM[0x0038] = JP 0x4012        ← LDOS shadow-patches this RST vector
0x4012: JP 0x4518              ← just a jump; 0x4015–0x403F is data/strings
0x4518: IRQ_HANDLER            ← actual handler body
```

The bytes at 0x4015–0x403F are NOT code — they are ASCII data/strings
("KIO", "BASIC", "PR", "DO BC", "SYS0"). z80dasm misinterprets them as instructions.

### 0x4518 — IRQ_HANDLER: full structure

```z80
4518  push hl               ; save HL
4519  push af               ; save AF
451A  ld hl,0x37E0          ; HL = FDC/timer IRQ status port
451D  ld a,(hl)             ; read 0x37E0 — bit7=timer, bit6=FDC INTRQ
451E  or (hl)               ;   read again (debounce)
451F  ld hl,0x404B          ; HL → IRQ scratch byte at 0x404B
4522  ld (hl),a             ; save raw status
4523  inc l                 ; → 0x404C
4524  and (hl)              ; AND with enable mask at 0x404C
4527  jr z,0x452F → call 0x4DA6  (IRQ_CHECK) ; decode sources
452F  call 0x4DA6           ; IRQ_CHECK: sets bits in 0x4423; returns NZ=something pending
4532  jr nz,0x454F          ; if NZ → timer/FDC path

; Normal timer return path:
4534  pop af
4535  pop hl
4536  ei
4537  ret

; FDC INTRQ path (bit 6 set):
4538  push af,bc,de,hl,ix   ; full register save
453E  ld de,0x4547          ; return address after deferred dispatch
4541  push de
4542  ld e,(hl)             ; load dispatch table entry (hl set from IRQ bit scan)
4543  inc l
4544  ld d,(hl)
4545  ex de,hl
4546  jp (hl)               ; jump to sub-handler

; Deferred dispatch return cleanup (0x4547):
4547  pop ix, hl, de, bc, af
454D  jr 0x452B             ; re-scan remaining IRQ bits

; Timer path (0x454F):
454F  jr nc,0x4558          ; if not carry → 0x4558 (deferred check)
4551  push bc
4552  call 0x4750           ; IRQ_DRV_DISPATCH — re-issue pending disk command
4555  pop bc
4556  jr 0x4534             ; → return

; Deferred-callback check (0x4558):
4558  ld a,(0x430F)
455B  bit 4,a               ; if bit4 set → skip deferred hook
455D  jr nz,0x4534
455F  ld hl,0x4315
4562  ld a,0xC3
4564  sub (hl)              ; A = 0xC3 - (0x4315); Zero if 0x4315 == 0xC3
4565  jr nz,0x4534          ; not 0xC3 → normal return
4567  ld (hl),a             ; store 0 to 0x4315 (clear hook)
4568  ld hl,(0x4316)        ; load deferred return address
456B  pop af                ; restore saved AF
456C  ex (sp),hl            ; swap return addr on stack with deferred addr
456D  ei
456E  ret                   ; return to deferred address!
```

**Key insight**: When `(0x4315) == 0xC3`, the IRQ handler hijacks the normal return
stack and instead returns to the address stored at 0x4316/7. This is how LDOS
implements deferred disk callbacks — the disk driver stores 0xC3 in 0x4315 and its
own resume address in 0x4316/7, then the next interrupt delivers control there.

### 0x4DA6 — IRQ_CHECK: decode IRQ sources

```z80
4DA6  push hl
4DA7  ld hl,0x4423          ; IRQ_FLAGS byte
4DAA  ld a,(0x3880)         ; read drive-select I/O port
4DAD  and 0x03              ; low 2 bits
4DAF  neg                   ; make negative (if nonzero)
4DB1  ld a,(0x3801)         ; read printer status
4DB4  bit 0,a               ; bit 0 = printer busy?
4DB6  jr z,0x4DBC
4DB8  jr nc,0x4DBC
4DBA  set 1,(hl)            ; set bit 1 in IRQ_FLAGS (drive active)
4DBC  ld a,(0x3840)         ; read FDC status port  
4DBF  bit 0,a               ; bit 0 = BUSY
4DC1  jr z,0x4DC5
4DC3  set 2,(hl)            ; set bit 2 in IRQ_FLAGS (FDC busy)
4DC5  bit 2,a               ; bit 2 = DRQ
4DC7  push af
4DC8  jr z,0x4DD5
4DCA  jr c,0x4DD5
4DCC  ld a,(0x430F)
4DCF  bit 4,a               ; IRQ_GUARD bit 4
4DD1  jr nz,0x4DD5
4DD3  set 0,(hl)            ; set bit 0 in IRQ_FLAGS (DRQ pending)
4DD5  pop af
4DD6  pop hl
4DD7  ret
```

### 0x4750 — IRQ_DRV_DISPATCH: re-issue pending disk command

Called from the IRQ timer path (0x4551). Looks up command type from 0x4308 (SAVED_C),
sets A to the appropriate driver B-command code, then jumps to 0x4779 which calls
SETUP_IY + JP(IY) to re-execute the driver.

```z80
4750  ld a,(0x4308)   ; SAVED_C = last driver command byte
4753  ld c,a
4754  ld a,1          ; default command = 1
4756  jr 0x4779       ; → SETUP_IY + JP(IY) + cleanup

; Other command re-issue entries (jumped to by IRQ sub-handler table):
4759  ld a,7 → jr 0x4779   ; command 7
475E  ld a,6 → jr 0x4779   ; command 6
4763  ld a,0x0D → jr 0x4779
4767  ld bc,0x0E3E → jr 0x4779
476D  ld a,0x0F → jr 0x4779
4772  ld a,0x0A → jr 0x4779   ; called from 0x4B28 (second validation probe path)
4777  ld a,9  → (falls through to 0x4779)  ← B9_DISPATCH entry
```

### DIR_CMD 0x4FC0 — patches 0x4BF0 for wildcard mode

Disassembly of 0x4FC0 shows:

```z80
4FC0  ld a,7
4FC2  call 0x4410        ; init something
4FC5  ld hl,0x422A       ; DRV_TEMPLATE source
4FC8  ld de,0x470A       ; drives 1-7 table destination
4FCB  ld bc,0x0046       ; 70 bytes
4FCE  ld a,(0x442F)
4FD1  rrca               ; check bit 0
4FD2  jr c,0x4FD6        ; if set, use 0x422A
4FD4  ld l,0x7A          ; else use 0x427A (alternative template)
4FD6  ldir               ; copy drive template to drives 1-7

4FD8  call 0x4B65        ; GET_DIR_TRK_NUM → D=17 (directory track)
4FDB  ld e,c             ; E = C (C=0 after ldir, so E=0 = sector 0)
4FDC  ld l,c             ; L = 0
4FDD  call 0x4B45        ; VALIDATE_ENTRY with D=17, E=0
4FE0  jp nz,0x4409       ; ← ILLEGAL DRIVE NUMBER if NZ

4FE3  ld l,0xCD          ; ...
4FE5  ld a,(hl)
4FE6  and 0x20           ; bit 5
4FE8  ld b,a
4FE9  ld a,(0x4704)      ; drv0_byte4
4FEC  or b
4FED  ld (0x4704),a      ; update IY+4 for drive 0

; wildcard detection:
500B  ld a,0xE6
5008  ld (0x505E),a      ; patch something in listing routine
500D  ld a,0xC8          ; RET Z opcode
500F  ld (0x4BF0),a      ; patch 0x4BF0 → RET Z (WILDCARD_NOP becomes RET Z)
```

**Confirmed**: when the DIR command has a wildcard spec, it patches `0x4BF0` from
`NOP` (0x00) to `RET Z` (0xC8). This changes the module-loader fallback path
through 0x4BF1 (`jr nz,0x4C4B`). Without a wildcard, 0x4BF0 stays NOP and the
`jr nz` falls through normally.

### disasm_region.py bug fixed

The z80dasm argument syntax in `tools/disasm_region.py` was wrong — `--address VALUE`
and `--origin VALUE` are not valid z80dasm flags. Corrected to:
```
z80dasm -a -l -t --origin=0x<hex> <file>
```
(`-a` = print address, `-l` = create labels, `-t` = print hex/ASCII data bytes)

---

## Session 4 Findings (watchpoints + disassembly of 0x400C, 0x4BCD, 0x4E00, 0x4400, 0x43E0)

### Watchpoint results

| Watchpoint | Hits (boot) | Hits (DIR :0) | Key registers |
|------------|-------------|---------------|---------------|
| 0x4B45 VALIDATE_ENTRY | 6 | **0** | D=0x11, C=0x00, IY=0x0000 |
| 0x4B5E INNER_PROBE | 6 | **0** | matches 4B45 exactly |
| 0x4C77 DRV_VAL_ENTRY | 0 | **0** | never reached |
| 0x4409 ERROR | 0 | **1** | B=0x08, A=0xE0, ret=0x5A3C |

**VALIDATE_ENTRY fires only at boot** (for /SYS module loading), not for the manual
DIR command. The six boot hits all show D=0x11 (T17) and IY=0x0000 — validation
works correctly during boot with the current RECTYPE fix.

### 0x4409 watchpoint dump (manual DIR :0)

```
[WP4409] #1  A=E0 B=08 C=? D=? E=? H=? L=?
  ret=0x5A3C  (DISKDIR module body; called from ~0x5A39)
  mem[430E]=0xC4
  mem[4200-420F]: 3E 16 18 0A CD 68 47 CC 72 47 FE 06 3E 17 D1 C1
  mem[4700-470F]: (to be confirmed in next run)
```

**mem[4200] at error time** = `3E 16 18 0A CD 68 47 CC 72 47 FE 06 3E 17 D1 C1`
This is **FDC driver code** (not drive-table template data). The 0x4200 region is
used for the JV1 FDC driver at runtime. The drive template data seen at boot in
the static memdump had been overwritten by the time DIR :0 is typed.

### RST 28h → SVC Dispatcher chain

```
ROM[0x0028]  → jp 0x400C                  (LDOS patches ROM vector)
     0x400C  → jp 0x4BCD                  (SVC dispatcher)
     0x4BCD  → sub_4BF5 (SVC router)
```

Disassembly of **0x4BCD — SVC dispatcher** (annotated):

```z80
4BCD  or a              ; test A (SVC number)
4BCE  jp p, 0x4BEF     ; if positive (A < 0x80) → different path
4BD1  ex (sp),hl        ; swap HL with return address
4BD2  push af           ; save A (SVC#)
4BD3  ld hl, 0x4315     ; DEFER_HOOK
4BD6  ld a, (hl)        ; read DEFER_HOOK
4BD7  ld (0x4BE9), a    ; self-modify: save old DEFER_HOOK into inline ld-imm
4BDA  ld (hl), 0xC9     ; write RET (0xC9) to DEFER_HOOK while in SVC
4BDC  pop af            ; restore SVC#
4BDD  pop hl            ; restore caller HL
4BDE  call sub_4BF5     ; route the SVC
4BE1  ld a, (0x430E)    ; IRQ_TRACK
4BE4  call 0x0000       ; ← SELFMOD_CALL: bytes at 0x4BE5/6 patched at runtime
4BE7  push af
4BE8  ld a, 0x00        ; ← 0x4BE9: inline imm; gets old DEFER_HOOK written here
4BEA  ld (0x4315), a    ; restore DEFER_HOOK
4BED  pop af
4BEE  ret

; Short path (A >= 0x80, i.e. A is a signed-positive SVC# like 0x00–0x7F):
4BEF  dec a             ; A = SVC# - 1
4BF0  nop               ; (WILDCARD_NOP: patched to 0xC8=RET Z by DIR wildcard code)
4BF1  jr nz, 0x4C4B    ; if not zero → error "SYStem Error"
4BF3  inc a             ; A = 1
4BF4  ret
```

**Key**: The SELFMOD_CALL at 0x4BE4 (`CD 00 00`) is patched at runtime with a real
address. In the static memdump it reads as `CALL 0x0000`. Something (the SVC install
chain or module loader) writes the real dispatch target to 0x4BE5/6 at initialisation.

### sub_4BF5 — SVC router

```z80
4BF5  push hl           ; save HL
4BF6  ld h, a           ; H = SVC# (e.g. 0xB3)
4BF7  ld a, b           ; A = B (caller's B = drive number)
4BF8  ld (0x4C3B), a   ; store B for later
4BFB  ld a, h           ; A = SVC# again
4BFC  or 0x01           ; A |= 1
4BFE  cp 0x89           ; special case for SVC 0x88/89?
4C00  ld a, h           ; A = SVC# again
4C01  jr z, 0x4C12      ; if SVC# was 0x88 or 0x89 → skip XOR
4C03  ld a, (0x430E)    ; IRQ_TRACK (a "segment" selector)
4C06  xor h             ; XOR with SVC#
4C07  and 0x0F          ; low nibble
4C09  ld a, h           ; A = SVC# (restored)
4C0A  ld (0x430E), a   ; update IRQ_TRACK with SVC#
4C0D  ld hl, 0x4E00    ; HL = SVC dispatch table base
4C10  jr z, 0x4C12      ; (never taken — xor result might be 0)
4C12  push de / push bc
4C14  and 0x0F          ; A = SVC# & 0x0F
4C16  bit 3, a          ; test bit 3
4C18  jr z, 0x4C1C
4C1A  add a, 0x18       ; if bit 3 set: A += 24 (high SVC group)
4C1C  ld (0x44A7), a   ; store dispatch offset
4C1F  ld b, a
  ... (continues to compute jump into SVC table and call it)
```

The SVC table base is **0x4E00** (or 0x4E18 for the high group). SVC# is used
to index a jump table there.

### 0x4C5E — SVC drive-validate path (NOT taken for DIR :0)

```z80
4C5E  push hl
4C5F  ld hl, 0x430F    ; IRQ_GUARD
4C62  set 2, (hl)       ; set bit 2 of IRQ_GUARD
4C64  call 0x4430       ; → JP 0x4C77 (DRV_VAL_ENTRY)
4C67  jp nz, 0x4409    ; ILLEGAL DRIVE NUMBER if NZ — drives table check failed
4C6A  ex (sp), hl
4C6B  ld a, (0x430F)
4C6E  bit 1, a          ; bit 1 of IRQ_GUARD
4C70  ret nz
4C71  bit 7, a
4C73  jp nz, 0x400F    ; WARM_RESTART if bit 7 set
4C76  ret
```

0x4C77 was watched but **never fired** during DIR :0. The path 0x4C5E→0x4C77 is
not reached by the DIR module's call sequence.

### 0x4C77 — DRV_VAL_ENTRY (drive table validation)

```z80
4C77  ld b, 0           ; always checks drive 0
4C79  ld hl, 0x4200     ; drive table area (but at runtime contains FDC code!)
4C7C  call 0x4424       ; drive validate via SVC chain
```

Note: at DIR :0 error time, `mem[0x4200]` = FDC driver code. If 0x4C77 were to be
reached, the validation would scan code bytes looking for a drive table entry and
likely fail. But since 0x4C77 is never reached, this is moot for the current bug.

### 0x4400 — drive-access wrapper with patched RST delays

```z80
4400  ld a, 0x93     ; SVC# 0x93 (some drive-setup call)
4402  rst 28h        ; → LDOS SVC dispatcher → SVC 0x93
; Next two bytes PATCHED by boot init at 0x4E1A (ld (0x4403), hl):
4403  <lo byte of HIGH$>   ; for 48KB RAM → 0xFF → RST 38h (fires timer ISR!)
4404  <hi byte of HIGH$>   ; for 48KB RAM → 0xFF → RST 38h (fires timer ISR again!)
4405  ld a, 0xB3     ; SVC# 0xB3 (drive validate)
4407  rst 28h        ; → LDOS SVC dispatcher → SVC 0xB3
4408  ret z          ; Z = drive valid → return OK
4409  push af        ; NZ → drive NOT valid
440A  ld a, 0x96     ; error code = ILLEGAL DRIVE NUMBER
440C  rst 28h        ; signal error via SVC
440D  jp 0x44B4      ; → error handler
```

The two RST 38h at 0x4403/4 are **an intentional timing delay** imposed by boot.
After SVC 0x93, LDOS yields to the timer interrupt twice before calling SVC 0xB3.
The number of RST 38h inserted depends on the top-of-RAM address (0xFF = RST 38h
for both bytes in 48KB systems).

### 0x4E1A — boot patching of 0x4403

```z80
4E09  ; ... RAM probe loop determines top of RAM, result in HL
4E17  ld (0x4049), hl   ; store HIGH$ (top of RAM)
4E1A  ld (0x4403), hl   ; patch 0x4403-4 with top-of-RAM bytes!
```

For mal-80's 48KB configuration: HL = 0xFFFF → `[0x4403]` = 0xFF (RST 38h),
`[0x4404]` = 0xFF (RST 38h). Emulator RAM probe must return 0xFFFF for this to work.

### New tools/find_callers.py

Script created to scan memdump.bin for all CALL/JP to a given target address.
Usage: `python3 tools/find_callers.py 0x4400 0x4405`

---

## Session 5 Findings — Root Cause Identified and Fixed

### The actual DISKDIR call chain for `DIR :0`

When the user types `DIR :0`, DISKDIR parses the command at 0x5225-0x524F:

```z80
5230  cp 0x3A           ; is char ':'?
5232  ld c, 0x00        ; C = 0 (default drive)
5234  jr nz, l5244h     ; if no ':' → skip drive parse
5236  ld a, (hl)        ; read drive digit
5237  inc hl
5238  inc hl
5239  sub 0x30          ; A = drive number (0x00 for ':0')
523B  cp 0x08           ; if drive# ≥ 8 → error
523D  jp nc, 0x5A35
5240  ld c, a           ; C = parsed drive number
5241  ld (0x53FE), a    ; *** save parsed drive number to 0x53FE ***
l5244h:
5244  dec hl
5245  ld a, c           ; A = drive number
5246  ld (0x52AE), a
5249  ld de, 0x5A42
524C  call 0x4476       ; validate drive range via some check
524F  jp nz, 0x5A35    ; error path if not valid
```

For `DIR :0`: `A = 0x00`, stored to `mem[0x53FE] = 0x00`.

### The DISKDIR drive scan loop: 0x52B4

After parsing, DISKDIR scans all 8 drives C=0..7 in a loop:

```z80
52B4  push bc          ; save loop counter C (drive index 0..7)
52B5  ld a, 0x03
52B7  ld (0x55A2), a
52BA  ld a, c          ; A = drive index
52BB  add a, 0x30      ; A = ASCII drive char ('0'..'7')
52BD  ld (0x5B02), a   ; store drive char (parameter for CALL 0x44B8)
52C0  call 0x44B8      ; ← DRIVE_ACCESS: init/validate drive C
52C3  jp nz, 0x541B    ; *** if drive not accessible → error-handling path ***
; (if Z: continue → seek, motor-wait, read directory)
```

### CALL 0x44B8 — the failing function

`CALL 0x44B8` issues three SVC calls and then jumps to a multiply routine:

```z80
44B8  ld a, 0xC4 ; rst 28h  → SVC 0xC4  (drive select/motor-on)
44BB  ld a, 0xA5 ; rst 28h  → SVC 0xA5
44BE  ld a, 0xCD ; rst 28h  → SVC 0xCD
44C1  jp 0x4B8F           → multiply finalisation → RET to caller
```

### SVC 0xC4 body — the motor-wait that fails

SVC 0xC4 (A & 0x70 = 0x40) maps to `l4e52h` in the 0x4E00 multi-SVC dispatcher:

```z80
4E52  push iy
4E54  call 0x478F      ; SETUP_IY → IY = drive 0 DCB at 0x4700
4E57  ld a, (iy+0)     ; A = 0xC3 (JP opcode)
4E5A  sub 0xC3         ; A = 0 → Z
4E5C  jp nz, 0x4ED4    ; (not taken)
4E5F  push hl / push de
4E61  ld d, (iy+5)     ; D = IY+5 = 0x10 = T16 (seek target)
4E64  ld e, a          ; E = 0 (sector 0)
4E65  call 0x475E      ; SEEK to T16 (drive driver B=6)
4E68  ei
4E69  call 0x4759      ; CALL drive driver B=7 (read index pulse status)
4E6C  bit 4, (iy+4)    ; IY+4 = 0x01 → bit 4 = 0 → single-sided disk → fall through
```

After the seek to T16, LDOS runs three motor-wait loops that synchronise to the
**disk INDEX PULSE** (the magnetic index hole on the disk, firing once per revolution
at 300 RPM = once per ~354,800 T-states at 1.774 MHz):

```z80
4E72  ld a, (0x4040) ; read interrupt counter
4E75  add a, 0x14    ; D = counter + 20 (20-interrupt timeout ≈ 333ms)
4E77  ld d, a
l4e78h: call 0x4ED7; jr nz, l4e78h  ; ① wait while bit1=1 (stay until INDEX goes LOW)
l4e7dh: call 0x4ED7; jr z,  l4e7dh  ; ② wait while bit1=0 (wait for next INDEX pulse)
l4e82h: call 0x4ED7; jr nz, l4e82h  ; ③ wait while bit1=1 (wait for INDEX to go LOW)
```

Sub-function at 0x4ED7 ("motor_wait_poll"):
```z80
4ED7  ld a, (0x4040) ; read LDOS 60Hz interrupt counter
4EDA  cp d           ; if counter reached D (timeout) → exit with NZ
4EDB  jr z, l4ee3h   ; → l4ee3h: POP AF; OR 0x01; JR l4ed2h → POP DE/HL/IY; RET NZ
4EDD  call 0x4759    ; read disk hardware status (drive driver B=7)
                     ; B=7 → driver at 0x45FB → l461ch:
                     ;   ld a,(0x37EC); bit 0,a; ret z  ← reads INDEX PULSE bit 0
                     ; returns with A = mem[0x37EC], Z from BIT
4EE0  bit 1, a       ; *** check INDEX PULSE = bit 1 of FDC status ***
4EE2  ret            ; return with Z=1 if bit1=0, Z=0 if bit1=1
```

The INDEX PULSE is **bit 1 of the FD1771 status register** (also reported at
memory address `0x37EC`). On real hardware:
- Bit 1 = INDEX PULSE during Type I commands (Restore/Seek/Step idle state)
- Pulses HIGH for ~1 ms once per disk revolution (300 RPM = 200 ms period)

### What was failing (root cause)

In mal-80, after a Seek command, `FDC::cmd_seek()` sets:
```cpp
status_ = (track_ == 0) ? ST_TRACK0 : 0x00;
```

So `status_ = 0x00` for non-track-0. When LDOS reads `0x37EC` (→ `fdc_.read(0x37EC)`)
during the motor-wait, it always gets `0x00` → **bit 1 = 0 always**.

Loop ① exits immediately (bit1=0 → Z → `jr nz` not taken) ✓
Loop ② spins forever (bit1=0 always → Z → `jr z` loops) ✗

Loop ② loops ~35,000 times (memory reads at ~10 T-states each) until the LDOS
60Hz interrupt counter increments 20 times (~590,000 T-states ≈ 333ms). Then
the `cp d; jr z, l4ee3h` timeout fires. `l4ee3h` does `POP AF; OR 0x01; JR l4ed2h`
which exits SVC 0xC4 with **NZ**.

Back in DISKDIR: `CALL 0x44B8` returned NZ → `jp nz, 0x541B` taken:
```z80
541B  pop bc                ; restore loop counter
541C  ld a, (0x53FE)        ; read parsed drive number = 0x00 (from `:0` parse)
541F  inc a                 ; 0x00 + 1 = 0x01 → NZ flag
5420  ld a, 0x20            ; A = error code (doesn't change Z flag from INC)
5422  jp nz, 0x5A37         ; TAKEN → error path
5A37  or 0xC0               ; A = 0x20 | 0xC0 = 0xE0
5A39  call 0x4409           ; → ILLEGAL DRIVE NUMBER (A=0xE0, B=0x08)
```

`mem[0x53FE]` is not a "status byte" — it is the **user-specified drive number**
(0 for `:0`). The `INC A; JP NZ` check means: "if a specific drive was requested
and it's not accessible, report error". The drive was not accessible because SVC 0xC4
timed out waiting for the INDEX PULSE.

### The fix

In `src/system/Bus.cpp`, the `read()` handler for 0x37EC already calls
`fdc_.read(addr)` which returns `status_`. After a Seek, `status_ = 0x00`.
We now add a **simulated INDEX PULSE** based on `global_t_states`:

```cpp
} else {
    value = fdc_.read(addr);   // 0x37EC-0x37EF: FDC registers
    // Simulate FD1771 INDEX PULSE (bit 1) during Type I idle state.
    // After Seek/Restore/Step, status_ has no BUSY, no DRQ, no RECTYPE.
    // LDOS motor-wait loops (l4e78h/l4e7dh/l4e82h inside SVC 0xC4) need
    // bit 1 to oscillate with disk rotation before calling VALIDATE_ENTRY.
    // Real disk: 300 RPM → index pulse once per ~354,800 T-states (~5% duty).
    if (addr == 0x37EC && (value == 0x00 || value == 0x04 /* ST_TRACK0 */)) {
        constexpr uint64_t INDEX_PERIOD = 354800;  // T-states / revolution
        constexpr uint64_t INDEX_WIDTH  =  17740;  // ~5% duty ≈ 1 ms at 1.774 MHz
        if ((global_t_states % INDEX_PERIOD) < INDEX_WIDTH)
            value |= 0x02;  // bit 1 = INDEX PULSE in Type I idle
    }
}
```

**Why the condition `value == 0x00 || value == 0x04`:**
- After T17 sector read: `status_ = 0x20` (ST_RECTYPE) → condition false → no pulse
  (avoids falsely setting bit 1 = DRQ after a completed Type II command)
- After non-T17 read: `status_ = 0x00` → condition true, but LDOS doesn't check
  status bit 1 after Type II data reads, so this is harmless
- During active Type II: `status_ = ST_BUSY | ST_DRQ = 0x03` → condition false → no pulse

**Expected motor-wait behavior after fix:**
- Loop ①: if we're mid-pulse (bit1=1), spin until pulse ends; else exit immediately
- Loop ②: wait (up to 354,800 T-states ≈ 13 interrupt cycles) for next pulse start
- Loop ③: wait for pulse end (~17,740 T-states)
- Total: ≤390,000 T-states ≈ 13 interrupt frames — well under the 20-frame timeout

After the motor-wait completes, SVC 0xC4 calls `VALIDATE_ENTRY` (0x4B45) with
D=17, E=0 — the same parameters that work at boot time → returns Z →
SVC 0xC4 returns Z → `CALL 0x44B8` returns Z → DISKDIR reads the directory ✓

### Summary of all fixes applied across sessions

| Session | Fix | File | Status |
|---------|-----|------|--------|
| 3 | RECTYPE: `bool deleted = (t == 17)` (was excluding S0/S1) | FDC.cpp | Applied ✓ |
| 5 | INDEX PULSE simulation at 0x37EC read | Bus.cpp | Applied ✓ |

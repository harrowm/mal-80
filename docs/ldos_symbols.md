# LDOS 5.3.1 Symbol Table (from memdump.bin)

All addresses are from the runtime memory snapshot (`memdump.bin`) taken after
LDOS 5.3.1 has booted. Code at 0x4000–0xFFFF is LDOS; 0x0000–0x2FFF is the
TRS-80 Level II ROM (with LDOS shadow RAM patching some vectors).

---

## FDC Registers (memory-mapped)

| Address | Name | Notes |
|---------|------|-------|
| 0x37E0–0x37E3 | DRVSEL | Drive select latch (write); IRQ status (read) |
| 0x37E1 | DRVSEL1 | Drive select byte written by LDOS driver |
| 0x37EC | FDC_CMD/STATUS | Command (write) / Status (read); read clears INTRQ |
| 0x37ED | FDC_TRACK | Track register |
| 0x37EE | FDC_SECTOR | Sector register |
| 0x37EF | FDC_DATA | Data register |

---

## LDOS Variables (RAM, 0x4000–)

| Address | Name | Notes |
|---------|------|-------|
| 0x4040 | var_4040 | Incremented by IRQ handler at 0x4586 (8-bit counter, wraps 0-FF) |
| 0x4041–0x4043 | var_4041 | 3-byte timer values compared by IRQ sub-handler at 0x45D1 |
| 0x4044 | DCT | Drive Control Table base (LDOS uses IY → drive table entries) |
| 0x404B | DRSEL | Written at 0x45F0 from IY+3/IY+4; drive-select byte cached for IRQ re-assertion |
| 0x403D | DSPFLG | **Display-width flag** (bit 3 = 32-column mode). ROM-only; LDOS kernel never reads this. Previously mislabelled RAMFLG. |
| 0x4308 | SAVED_C | Saved C register (drive number/cmd) before B=9 dispatch at 0x477C |
| 0x4309 | saved_drv | Drive select byte cache |
| 0x430E | IRQ_TRACK | XOR'd with H at 0x4C06; tracks some disk state across calls |
| 0x430F | IRQ_GUARD | Bit 4: if set, IRQ handler skips deferred callback at 0x4558 |
| 0x4315 | DEFER_HOOK | Deferred-callback gate: if == 0xC3, IRQ handler redirects return to addr at 0x4316/7 |
| 0x4316/7 | DEFER_ADDR | 2-byte address for deferred IRQ callback (read at 0x4568 when 0x4315==0xC3) |
| 0x422A–0x426F | DRV_TEMPLATE | Drive table template block (70 bytes). First entry: `JP 0x308A`, flags=0x03, flags2=0x42. Copied to 0x470A (drives 1-7) by DIR_CMD ldir. Drive 0 at 0x4700 is NOT overwritten by this copy. |
| 0x4423 | IRQ_FLAGS | Bits set by 0x4DA6: bit0=FDC INTRQ, bit1=drive-select, bit2=timer. Read by IRQ handler at 0x4532 to decide which sub-handler to call. |
| 0x442F | ? | Flag bit 0 checked before ldir at 0x4FD6 |
| 0x44A0 | ? | Buffer passed to 0x4C8D (module loader sub) with B=0 |
| 0x45B5 | ? | Dispatch pointer (2 bytes) |
| 0x46D1–0x46D2 | PATCHED_READ | Self-modified by EX(SP),HL trick at 0x469A |
| 0x46F6 | PATCHED_INIT | Self-modified by EX(SP),HL trick at 0x469A (bit-counter initial A) |
| 0x4704 | drv0_byte4 | IY+4 of drive 0 table entry (side/flags); updated at 0x4FE6 |

---

## LDOS Drive Table (0x4700)

One 10-byte entry per drive, accessed via IY register.
Layout: IY+0/1/2 = `JP 0x45FB` (3 bytes), then 7 data bytes.

| IY offset | Name | Drive 0 value | Notes |
|-----------|------|--------------|-------|
| IY+0/1/2  | DISPATCH | 0xC3 0xFB 0x45 | `JP 0x45FB` — entry to LDOS disk driver |
| IY+3 | FLAGS | 0x03 | Drive flags (bits 0-1 = step rate, bit 4 = side, bit 7 = ?) |
| IY+4 | FLAGS2 | 0x01 | More flags (bit 5 = double-sided?) |
| IY+5 | CURTRK | 0x00 | Current head track (updated by seek) |
| IY+6 | ? | 0x22 | |
| IY+7 | SPT | 0x09 | Sectors per track (9 for LDOS JV1) |
| IY+8 | ? | 0x24 | |
| IY+9 | DIRTRK | 0x11 | Directory track number (0x11 = 17) |

Drive entries: 0x4700 (drive 0), 0x470A (drive 1), 0x4714 (drive 2), 0x471E (drive 3).

---

## LDOS Subroutines

### Disk Driver Entry & Dispatch

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x45FB | DRIVER_ENTRY | B=cmd → various | Main LDOS disk driver. Dispatches on B: B=9 → Read Sector with RECTYPE |
| 0x45E0 | WAIT_SELECT | A=status → — | Wait for FDC BUSY, update drive select latch (0x37E1) |
| 0x461C | WAIT_BUSY | — → A=status | Polls 0x37EC until BUSY (bit 0) = 0; returns status in A |
| 0x462A | DO_SEEK | D=track,E=sector → — | Seek to track D, write sector E to FDC sector reg; updates IY+5 |
| 0x4662 | ISSUE_CMD | A=cmd | Write A to FDC command register (0x37EC); tiny wait loop |
| 0x466B | CMD_READ_SECTOR | B=9,D=track,E=sector | Read Sector path; issues FDC cmd 0x88; RECTYPE → A=6, no error → A=0 |
| 0x469A | INLINE_DATA_TRICK | — | EX(SP),HL; reads 3 inline bytes; patches 0x46F6 and 0x46D1–0x46D2; falls through (not a normal subroutine) |
| 0x46F5 | BIT_COUNT_LOOP | B=status_byte,A=1 | Count right-rotations until carry; B=0x20 → A=6 (RECTYPE path) |
| 0x4700 | IY_DRIVE0 | — | Drive table entry for drive 0 (JP 0x45FB + 7 bytes) |
| 0x4750 | IRQ_DRV_DISPATCH | — | IRQ-driven disk re-issue table. Loads saved C from 0x4308, sets command A, jumps to 0x4779. Called from IRQ handler at 0x4551 when bit 6 (FDC INTRQ) set in 0x37E0 read. |
| 0x4777 | B9_DISPATCH | C=drvcmd,D=track,E=sector | Issue B=9 driver call → calls DRIVER_ENTRY via JP(IY). Returns A. |
| 0x478F | SETUP_IY | C=drive# | Push HL, call 0x47A5 to set IY → drive table entry, EX(SP),HL, pop IY |
| 0x479C | READ_DCT_FIELD | A=field_offset,C=drive# → A | Read byte at IY+field_offset from drive table entry |
| 0x47A5 | DRIVE_TABLE_HL | H=offset,C=drive# → HL,A | HL = 0x4700 + (drive & 7)*10 + field; A = field address offset |
| 0x4BCB | JP_IY | — | `JP (IY)` → jumps to drive table entry (0x45FB for drive 0) |
| 0x4BCD | SVC_DISPATCH | A=svc# → varies | RST 28h routes here (via 0x400C). Saves/restores DEFER_HOOK; calls sub_4BF5. Self-mod call at 0x4BE4 (`CALL 0x0000`) is patched at init with real handler. |
| 0x4BEF | SVC_SHORT | A=svc# → varies | Short path for A<0x80; A−1: if 0 return A=1, else → SYStem Error (0x4C4B) |
| 0x4BF5 | sub_4BF5 | A=svc#,B=drive# → varies | SVC router: indexes into 0x4E00 SVC table using A. Saves B at 0x4C3B. |
| 0x4C5E | SVC_DRV_HDR | — → Z/NZ | SVC drive-validate path: sets IRQ_GUARD bit2, calls 0x4430 (=JP 0x4C77), jp nz → ILLEGAL DRIVE NUMBER. **Not reached during manual DIR :0.** |
| 0x4C77 | DRV_VAL_ENTRY | — | Sets B=0, HL=0x4200; calls 0x4424 (drive table scan). **Watchpointed; never fires for DIR :0.** |

### Drive Validation

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x4B10 | VALIDATE_DRIVE | — → Z/NZ, A=0x11 | Calls 0x4B37 (sets D=dirtrk), then VALIDATE_ENTRY; returns A=0x11 on success |
| 0x4B17 | — | (call 0x4B45 via 0x4B10) | |
| 0x4B37 | GET_DIR_TRACK | B=? → D=dirtrk, E=? | Calls 0x4B65; D=dir_track (17); E=(B & 0x1F)+2; HL=0x42xx |
| 0x4B45 | VALIDATE_ENTRY | D=track,E=sector → Z=success | Outer validation: first probe 0x4B5E; if fail, fallback D=0; if fail, NZ |
| 0x4B5E | INNER_PROBE | D=track → Z, A=0 on success, NZ A≠0 on fail | Calls B9_DISPATCH; sub 6; ret |
| 0x4B65 | GET_DIR_TRK_NUM | C=drive# → D=dirtrk | Reads IY+9 from drive table (= directory track = 17 for drive 0) |

### DIR Command Path

| Address | Name | Notes |
|---------|------|-------|
| 0x4FC0 | DIR_CMD | Entry point for DIR command |
| 0x4FD8 | — | Calls 0x4B65 to load D=17 (directory track) |
| 0x4FDB | — | LD E,C (E=0 after LDIR, sets sector); LD L,C (L=0) |
| 0x4FDD | — | CALL VALIDATE_ENTRY with DE={D=17,E=0} |
| 0x4FE0 | — | JP NZ, ERROR if validation fails |
| 0x4409 | ERROR | `LD A,0x96; RST 28h` — ILLEGAL DRIVE NUMBER error |

### Other

| Address | Name | Notes |
|---------|------|-------|
| 0x4399 | ERR_HANDLER | Jump target when FDC error detected at 0x45F8 |
| 0x4B6C | MULTIPLY | 8-bit multiply used for block/sector calculations |
| 0x4B7B | DIVIDE? | Another arithmetic routine |
| 0x4B8F | ? | Another arithmetic routine |
| 0x4B92–0x4BA8 | ? | 16-bit arithmetic |
| 0x4BA9–0x4BBB | ? | 16-bit arithmetic |
| 0x4BCB | JP_IY | `JP (IY)` — driver dispatch |
| 0x4BCD | ? | Error/interrupt-related |
| 0x4BE4 | SELFMOD_CALL | `CD 00 00` = CALL to self-modified address. Bytes at 0x4BE5/4BE6 patched at runtime with real target. |
| 0x4BF0 | WILDCARD_NOP | Normally `NOP` (0x00). DIR command patches this to `0xC8` (RET Z) at 0x500D when a wildcard spec is given. Reverts path through 0x4BF1. |

### SVC Dispatcher & Table

| Address | Name | Value/Notes |
|---------|------|-------------|
| 0x400C | RST28_VEC | `JP 0x4BCD` — ROM RST 28h vector patched by LDOS to route to SVC dispatcher |
| 0x4BCD | SVC_DISPATCH | SVC dispatcher entry; saves DEFER_HOOK, calls sub_4BF5, restores |
| 0x4BE4 | SELFMOD_CALL | `CD 00 00` = `CALL 0x0000`; 0x4BE5/6 patched at runtime with real SVC completion handler |
| 0x4BE9 | SELFMOD_IMM | 1-byte field inside `LD A, nn`; old DEFER_HOOK value written here by dispatcher |
| 0x4C3B | SAVED_B | Caller's B register saved by sub_4BF5 |
| 0x4E00 | SVC_TABLE | SVC dispatch table; SVC# used to index entries here |
| 0x4400 | DRV_ACCESS | Drive-access wrapper: SVC 0x93 + 2× RST 38h delay (patched by boot) + SVC 0xB3 (validate) |
| 0x4403 | BOOT_DELAY | **Patched by boot (0x4E1A: `ld (0x4403), hl`)**: set to top-of-RAM bytes. For 48KB: 0xFF 0xFF = two RST 38h timer yields between SVC 0x93 and SVC 0xB3. |

### SVC Numbers (partial)

| SVC# | Name | Notes |
|------|------|-------|
| 0x93 | SVC_DRV_SETUP? | Called at 0x4402 before drive validate; purpose unknown |
| 0xB3 | SVC_DRV_VALIDATE | Called at 0x4407; validates drive using table at 0x4200/0x4700; returns NZ → ILLEGAL DRIVE NUMBER |
| 0x94 | SVC_0x94 | Called at 0x4424 as part of validation chain |
| 0x95 | SVC_0x95 | Called at 0x442A |
| 0x9C | SVC_0x9C | Called at 0x442C |
| 0x96 | SVC_ERR | Error signal: ILLEGAL DRIVE NUMBER (called at 0x440C with error code in A) |

### SVC Chain (0x50B0)

Runtime memdump shows the SVC chain is NOT at 0x50B0 (docs were wrong):

| Address | Name | Value | Notes |
|---------|------|-------|-------|
| 0x50B0 | SVC_MID | 0xF1 (POP AF) | Not the JP entry — this is mid-chain data |
| 0x50B3 | SVCJMP | 0xC3 (JP opcode) | Actual JP to SVC handler |
| 0x50B4/5 | SVCTGT | 0x0033 | SVC jump target = 0x0033 (ROM SVC handler) |
| 0x50B6/7 | SVCNXT | 0x441E | Next chain link = 0x441E (memdump: 1E 44) |

---

## Known Callers

| Callee | Callers |
|--------|---------|
| 0x4B10 (VALIDATE_DRIVE) | 0x4892 (file-open path, C from IX+6), 0x4C2B (module-loader, C=0) |
| 0x4B45 (VALIDATE_ENTRY) | 0x4B17 (via 0x4B10), 0x4FDD (DIR_CMD) — **only fires at boot, not for manual DIR :0** |
| 0x4B65 (GET_DIR_TRK_NUM) | 0x4B37 (GET_DIR_TRACK), 0x4FD8 (DIR_CMD) |
| 0x4777 (B9_DISPATCH) | 0x4B4D (fallback in 0x4B45), 0x4B5E (INNER_PROBE) |
| 0x4750 (IRQ_DRV_DISPATCH) | 0x4551 (timer IRQ path in IRQ_HANDLER) |
| 0x4DA6 (IRQ_CHECK) | 0x452F (IRQ_HANDLER, every interrupt) |
| 0x4518 (IRQ_HANDLER) | 0x4012 (RST 38h / IM1 vector trampoline) |
| 0x4BCD (SVC_DISPATCH) | 0x400C (RST 28h vector); all `rst 28h` instruction sites |
| 0x4BF5 (sub_4BF5) | 0x4BDE (SVC_DISPATCH) |
| 0x4C77 (DRV_VAL_ENTRY) | 0x4C64 (via 0x4430=JP 0x4C77) — **watchpointed; never reached during DIR :0** |
| 0x44B8 (DRIVE_ACCESS) | 0x52C0 (DISKDIR drive-scan loop) |
| 0x4E52 (SVC_0xC4_BODY / l4e52h) | 0x4E00 multi-SVC dispatcher (A&0x70=0x40) |
| 0x4ED7 (motor_wait_poll) | 0x4E78/4E7D/4E82 (three motor-wait loops) |
| 0x461C (l461ch / index_pulse_wait) | 0x45FB driver B=7 path, 0x462A driver B=6 path |

---

## DISKDIR Module (loaded into upper RAM ~0x5200-0x5B00)

| Address | Name | Notes |
|---------|------|-------|
| 0x5225 | DISKDIR_CMDPARSE | Parses `:N` drive specifier; writes drive# to 0x53FE |
| 0x52B4 | DISKDIR_DRVSCAN | Drive scan loop C=0..7; calls 0x44B8 per drive |
| 0x52C0 | DRIVE_ACCESS_CALL | `call 0x44B8` — checks if drive accessible |
| 0x52C3 | DRIVE_NZ_BRANCH | `jp nz, 0x541B` — if not accessible → error-handling path |
| 0x541B | DISKDIR_ERR_CHECK | For failed drive: reads `mem[0x53FE]` (parsed drive#), checks if user specified this drive |
| 0x53FE | DISKDIR_TGTDRV | User-specified drive number (0x00 for `:0`, 0xFF=none); NOT a "status" byte |
| 0x5A37 | DISKDIR_ERRPATH | `or 0xC0; call 0x4409` — maps error code to ILLEGAL DRIVE NUMBER |

## SVC 0xC4 Motor-Wait (l4e52h, address 0x4E52)

| Address | Role | Notes |
|---------|------|-------|
| 0x4E52 | l4e52h | Entry: PUSH IY; SETUP_IY; check IY+0 == 0xC3 |
| 0x4E65 | seek_call | `call 0x475E` = seek to IY+5 (track 0x10 for drive 0) |
| 0x4E72-0x4E86 | motor_wait | Three index-pulse sync loops (see below) |
| 0x4E87-0x4E92 | validate_call | `call 0x4B65; call 0x4B45` = VALIDATE_ENTRY (same as boot) |
| 0x4EE3 | l4ee3h | Failure exit: `POP AF; OR 0x01; JR l4ed2h` (NZ return) |
| 0x4ED2 | l4ed2h | Exit sequence: `POP DE; POP HL; POP IY; RET` |

**Motor-wait loop pattern** (l4e78h / l4e7dh / l4e82h):
```
Each loop calls 0x4ED7 (motor_wait_poll):
  - Check LDOS interrupt counter 0x4040 against D (initial+20 = 20-frame timeout)
  - CALL 0x4759 (drive driver B=7 → l461ch → reads mem[0x37EC] INDEX PULSE)
  - BIT 1, A → Z if INDEX=0, NZ if INDEX=1
Loop ①: jr nz → spins while INDEX=1 (wait for pulse to go LOW)
Loop ②: jr z  → spins while INDEX=0 (wait for next pulse — THE ONE THAT HUNG)
Loop ③: jr nz → spins while INDEX=1 (wait for pulse to go LOW again)
```

**mem[0x37EC] = FD1771 Status Register** (memory-mapped via Expansion Interface):
  - Bit 0: INTRQ / INDEX (from I/O mapping)
  - Bit 1: INDEX PULSE in Type I idle state (Seek/Restore/Step done, no command active)
  - Bit 7: NOT READY

---

## Open Questions

1. **What patches 0x4BE5/0x4BE6?** The self-modified call at 0x4BE4 reads as
   `CALL 0x0000` in the static memdump. Something at runtime writes the real target
   address there. Likely the module loader or SVC install chain. Unknown.

2. ~~**Why B=0x08 for `:0`?**~~ _(Session 4 — now resolved in Session 5)_
   B=0x08 was `mem[0x4C3B]` (saved by sub_4BF5 on SVC entry). It is NOT the drive
   number passed to VALIDATE_DRIVE. The watchpoints gave misleading information since
   B was overwritten by SVC processing before the error was reached.
   The actual cause was the INDEX PULSE simulation missing (see Session 5 above).

3. ~~**Which `jp nz, 0x4409` fires for DIR :0?**~~ _(Session 5 resolved)_
   `CALL 0x4409` at 0x5A39 inside DISKDIR module fires directly (not a `jp nz`).
   Path: `LD A,(0x53FE); INC A; JP NZ,0x5A37; OR 0xC0; CALL 0x4409`.

4. **What does SVC 0x93 do?** Called at 0x4402 before 0xB3. Unknown purpose.

5. ~~**What does SVC 0xB3 actually check?**~~ _(Session 5: moot)_
   SVC 0xB3's `AND 0x70=0x30` path exits with `RET NZ` immediately from 0x4E00
   dispatcher. But SVC 0xB3 is called at 0x4407 (inside 0x4400 wrapper) only for
   the SEPARATE VALIDATE_ENTRY path (0x4FDD) — this path isn't reached for DIR :0
   because CALL 0x44B8's SVC 0xC4 fails first.

6. **What does 0x4012 region 0x4015–0x4032 contain?**
   Bytes are printable ASCII and look like string data ("KIO", "BASIC", "PR",
   "DO BC", "SYSTEM"). Possibly LDOS module names or copyright strings embedded
   in the interrupt page. Only the `JP 0x4518` at 0x4012 is code.

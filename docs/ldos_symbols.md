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
| 0x4040 | var_4040 | Something incremented by IRQ handler |
| 0x4041–0x4043 | var_4041 | 3-byte values compared by IRQ handler at 0x45D1 |
| 0x4044 | DCT | Drive Control Table base (LDOS uses IY → drive table entries) |
| 0x403D | DSPFLG | **Display-width flag** (bit 3 = 32-column mode). ROM-only; LDOS kernel never reads this. Previously mislabelled RAMFLG. |
| 0x4308 | saved_C | Saved C register (drive number?) before B=9 dispatch |
| 0x4309 | saved_drv | Drive select byte cache |
| 0x430E | IRQ_TRACK | XOR'd with H at 0x4C06; tracks some disk state across calls |
| 0x430F | ? | Checked (bit 4) by IRQ handler |
| 0x4315 | FLAG_4315 | Written to 0x00 by code at 0x4BEA after self-modified call at 0x4BE4 |
| 0x4316 | ? | JP target used by IRQ path |
| 0x422A–0x426F | DRV_TEMPLATE | Drive table template block (70 bytes). First entry: `JP 0x308A`, flags=0x03, flags2=0x42. Copied to 0x470A (drives 1-7) by DIR_CMD ldir. Drive 0 at 0x4700 is NOT overwritten by this copy. |
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
| 0x4750 | DISPATCH_TABLE | — | Secondary dispatch table (accessed via 0x4550 area) |
| 0x4777 | B9_DISPATCH | C=drvcmd,D=track,E=sector | Issue B=9 driver call → calls DRIVER_ENTRY via JP(IY). Returns A. |
| 0x478F | SETUP_IY | C=drive# | Push HL, call 0x47A5 to set IY → drive table entry, EX(SP),HL, pop IY |
| 0x479C | READ_DCT_FIELD | A=field_offset,C=drive# → A | Read byte at IY+field_offset from drive table entry |
| 0x47A5 | DRIVE_TABLE_HL | H=offset,C=drive# → HL,A | HL = 0x4700 + (drive & 7)*10 + field; A = field address offset |
| 0x4BCB | JP_IY | — | `JP (IY)` → jumps to drive table entry (0x45FB for drive 0) |

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
| 0x4B45 (VALIDATE_ENTRY) | 0x4B17 (via 0x4B10), 0x4FDD (DIR_CMD) |
| 0x4B65 (GET_DIR_TRK_NUM) | 0x4B37 (GET_DIR_TRACK), 0x4FD8 (DIR_CMD) |
| 0x4777 (B9_DISPATCH) | 0x4B4D (fallback in 0x4B45), 0x4B5E (INNER_PROBE) |

---

## Open Questions

1. **What is at 0x4012?** RST 38h (IM1 interrupt) jumps there. Need to disassemble
   0x4012 to understand the full interrupt-handling chain before it reaches 0x45C0.

2. **What patches 0x4BE5/0x4BE6?** The self-modified call at 0x4BE4 reads as
   `CALL 0x0000` in static memdump. Something at runtime writes the real target
   address there. Likely the module loader or SVC install chain. Unknown.

3. **Does the Seek actually complete before the Read Sector starts?**
   On real hardware, Seek is async (INTRQ fires when done). In our emulator,
   Seek is instantaneous. LDOS polls BUSY (0x461C loop) after Seek before issuing
   Read Sector — so timing should be fine, but needs confirmation via log.

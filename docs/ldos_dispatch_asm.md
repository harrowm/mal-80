# LDOS Driver Dispatch — Annotated Disassembly
## Range: 0x4700–0x47FF (from memdump.bin)

Key routines: Drive table (0x4700), B9_DISPATCH (0x4777), SETUP_IY (0x478F),
DRIVE_TABLE_HL (0x47A5), READ_DCT_FIELD (0x479C).

See `ldos_symbols.md` for symbol definitions.

---

## 0x4700 — Drive Table (DATA, not code)

10 bytes per drive entry. Misread as instructions by z80dasm; shown as raw bytes.

```
Drive 0 entry (0x4700–0x4709):
  0x4700: C3 FB 45 = JP 0x45FB   ← IY+0/1/2: dispatch jump to DRIVER_ENTRY
  0x4703: 03                      ← IY+3: flags (step rate bits 0-1, side bit 4, etc)
  0x4704: 01                      ← IY+4: flags2 (bit5=double-sided?)
  0x4705: 00                      ← IY+5: current head track (updated by DO_SEEK)
  0x4706: 22                      ← IY+6: ?
  0x4707: 09                      ← IY+7: sectors per track (9 for LDOS JV1)
  0x4708: 24                      ← IY+8: ?
  0x4709: 11                      ← IY+9: directory track = 0x11 = 17 ✓

Drive 1 entry (0x470A–0x4713):
  0x470A: C3 FB 45 = JP 0x45FB
  0x470D: 03
  0x470E: 02
  0x470F: 11
  0x4710: 22
  0x4711: 09
  0x4712: 24
  0x4713: 11                      ← IY+9: directory track = 17 for drive 1 too

Drive 2 entry (0x4714–0x471D):  03 04 11 22 09 24 11
Drive 3 entry (0x471E–0x4727):  03 08 11 22 09 24 (then C9 00 00...)

0x4728–0x474F: All 0x00 (unused drive entries 4-7, mostly NOPs)
  0x4732: C9 = RET (appears in NOP field — probably harmless data)
  0x473C: C9 = RET
  0x4746: C9 = RET
```

The `JP (IY)` at 0x4BCB reads IY as a pointer to the drive table entry and jumps
to the first 3 bytes (JP 0x45FB = DRIVER_ENTRY). This is how drive 0's dispatch works.

---

## 0x4750–0x4776 — IRQ_DRV_DISPATCH: re-issue pending disk command

**Not** a simple secondary dispatch table — this is called from the **IRQ timer path**
(0x4551) to re-issue a pending disk driver command after an interrupt fires.

Reads saved command from 0x4308 (SAVED_C), then jumps to 0x4779 with the right A value.
Each entry sets A to a different driver command code before falling through to 0x4779.

```z80
4750  ld a,(0x4308)  ; SAVED_C — last driver command byte
4753  ld c,a
4754  ld a,1         ; re-issue as command 1
4756  jr 0x4779      ; → SETUP_IY + B←A + JP(IY)
4758  nop
4759  ld a,7         ; re-issue as command 7
475B  jr 0x4779
475D  nop
475E  ld a,6         ; command 6
4760  jr 0x4779
4762  ff             ; (rst 38h — unreachable normally)
4763  ld a,0x0D
4765  jr 0x4779
4767  ld bc,0x0E3E
476A  jr 0x4779
476C  nop
476D  ld a,0x0F
476F  jr 0x4779
4771  nop
4772  ld a,0x0A      ; ← also called from 0x4B28 (second validation probe variant)
4774  jr 0x4779
4776  nop
```

---

## 0x4777 — B9_DISPATCH: issue B=9 driver command

Called from INNER_PROBE (0x4B5E) and the fallback in VALIDATE_ENTRY (0x4B4D).
Sets B=9 and dispatches to the driver via JP(IY).
On return, A contains the driver's result (6 for RECTYPE, 0 for clean read).

```z80
4777  3E 09       ld a, 9            ; A=9 (will become B=9 = Read Sector w/RECTYPE)
4779  C5          push bc            ; save BC
477A  47          ld b,a             ; B=9
477B  79          ld a,c             ; A=C (drive command byte, from 0x4308)
477C  32 08 43    ld (0x4308),a      ; save C to var_4308
477F  FD E5       push iy            ; save IY
4781  CD 8F 47    call 0x478F        ; SETUP_IY: set IY → drive table entry for current drive
4784  3E 20       ld a, 0x20         ; A=0x20 (RECTYPE bit — used somewhere in 0x4BCB path?)
4786  B7          or a               ; set flags (A≠0, NZ)
4787  CD CB 4B    call 0x4BCB        ; JP_IY: jump to IY[0] = JP 0x45FB = DRIVER_ENTRY
                                     ; driver executes with B=9, D=track, E=sector
                                     ; returns A = driver result
478A  FD E1       pop iy             ; restore IY
478C  C1          pop bc             ; restore BC
478D  C9          ret                ; return A (driver result)
478E  00          nop
```

---

## 0x478F — SETUP_IY: point IY to current drive's table entry

Called from B9_DISPATCH before `JP (IY)`.
Sets IY = 0x4700 + (drive_num & 7) * 10.

```z80
478F  E5          push hl            ; save HL
4790  CD A5 47    call 0x47A5        ; DRIVE_TABLE_HL: HL = drive table entry address
4793  E3          ex (sp),hl         ; HL ↔ [SP]: top of stack gets drive table addr
4794  FD E1       pop iy             ; IY = drive table entry address (was on stack)
4796  C9          ret
```

---

## 0x4797 — (part of another path)
```z80
4797  DD 4E 06    ld c,(ix+6)        ; read from IX-based structure
479A  3E 08       ld a, 8
```

---

## 0x479C — READ_DCT_FIELD: read a field from current drive's table entry

On entry: A = field offset (e.g., 9 for IY+9), C = drive number (or via 0x4308).
Returns: A = value of that field; HL = address of that field.

```z80
479C  E5          push hl
479D  67          ld h,a             ; H = field offset (e.g. 9)
479E  CD A5 47    call 0x47A5        ; DRIVE_TABLE_HL: A = drive*10 + offset; HL = table addr
47A1  6F          ld l,a             ; L = drive*10 + offset
                                     ; HL = {0x47, drive*10} (from DRIVE_TABLE_HL)
                                     ; but we need HL = 0x4700 + drive*10 + field_offset
                                     ; After: H = 0x47 (from DRIVE_TABLE_HL), L = drive*10 + field_offset
                                     ; Wait: ld l,a where a = drive*10 + field_offset
                                     ;       and H was set to 0x47 by DRIVE_TABLE_HL
                                     ; → HL = 0x4700 + drive*10 + field_offset ✓
47A2  7E          ld a,(hl)          ; A = byte at that address (the field value)
47A3  E1          pop hl
47A4  C9          ret
```

For GET_DIR_TRK_NUM (0x4B65): calls this with A=9 (field offset = IY+9 = directory track).
Result: A = byte at 0x4700 + drive*10 + 9 = 0x4709 = 0x11 = 17 for drive 0. ✓

---

## 0x47A5 — DRIVE_TABLE_HL: compute drive table entry address

On entry: H = field offset, C = drive number (or from context).
Returns: HL = 0x4700 + (drive & 7)*10 (base of entry); A = (drive&7)*10 + H (field address).

```z80
47A5  79          ld a,c             ; A = drive number
47A6  E6 07       and 0x07           ; A = drive & 7 (0-7)
47A8  87          add a,a            ; A *= 2
47A9  6F          ld l,a             ; save A*2
47AA  87          add a,a            ; A *= 4
47AB  87          add a,a            ; A *= 8
47AC  85          add a,l            ; A = A*8 + A*2 = original*10
47AD  C6 00       add a, 0x00        ; + base low byte (0x00 = table starts at 0x4700)
47AF  6F          ld l,a             ; L = drive*10 (+ 0)
47B0  84          add a,h            ; A = drive*10 + field_offset (H)
47B1  26 47       ld h, 0x47         ; H = 0x47 (table at page 0x4700)
47B3  C9          ret
; Returns: HL = 0x4700 + drive*10 (base of drive's 10-byte entry)
;          A = drive*10 + original_H (address offset of desired field)
; Caller then does ld l,a → HL = 0x4700 + drive*10 + field_offset = field address
```

**Example:** drive=0, H=9 (for IY+9):
  - A = 0*10 + 0 = 0; L = 0; A = 0 + 9 = 9; H=0x47
  - Caller ld l,a: L=9 → HL = 0x4709 = address of drive 0's directory track field ✓

---

## 0x47B4–0x47FF — Other routines (not yet analyzed)

```z80
47B4  6F          ld l,a
47B5  11 AE 44    ld de, 0x44AE
47B8  CD C3 47    call 0x47C3
47BB  E6 1F       and 0x1F
47BD  3C          inc a
47BE  12          ld (de),a
47BF  13          inc de
47C0  AF          xor a
47C1  12          ld (de),a
47C2  13          inc de

47C3  CD C6 47    call 0x47C6        ; (recursive-looking but probably not)
47C6  7E          ld a,(hl)
47C7  12          ld (de),a
47C8  23          inc hl
47C9  13          inc de
47CA  C9          ret

47CB  CD 4D 48    call 0x484D        ; some sector/block calculation
47CE  03          inc bc
47CF  CD F3 49    call 0x49F3        ; another routine
47D2  DD CB 01 F6 set 6,(ix+1)
47D6  DD CB 01 7E bit 7,(ix+1)
47DA  28 1E       jr z, +30
47DC  44          ld b,h
47DD  4D          ld c,l
47DE  FD BE 09    cp (iy+9)          ; compare with directory track (IY+9)
47E1  28 17       jr z, +23
47E3  CD 8F 4B    call 0x4B8F        ; multiply?
47E6  44          ld b,h
47E7  4D          ld c,l
47E8  FD 77 05    ld (iy+5),a        ; update current track in DCT
47EB  DD CB 01 6E bit 5,(ix+1)
47EF  28 0E       jr z, +14
47F1  CD 7E 49    call 0x497E
47F4  37          scf
47F5  ED 42       sbc hl,bc
47F7  CA 1D 4A    jp z, 0x4A1D
47FA  FD 77 05    ld (iy+5),a
47FD  C5          push bc
47FE  CD 93 XX    call ...
```

Note: At 0x47DE: `cp (iy+9)` compares something with the directory track number.
This is used in some higher-level path that decides whether to treat a track as
the directory track. Not directly in the B=9 validation probe path.

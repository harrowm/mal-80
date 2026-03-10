# LDOS Drive Validation — Annotated Disassembly
## Range: 0x4B00–0x4BFF (from memdump.bin)

Key routines: VALIDATE_ENTRY (0x4B45), INNER_PROBE (0x4B5E),
GET_DIR_TRK_NUM (0x4B65), VALIDATE_DRIVE (0x4B10).

See `ldos_symbols.md` for symbol definitions and callers.

---

## 0x4B00–0x4B0F — Unknown arithmetic routine (ends at 0x4B0F)

Called from somewhere. Performs a multiply and returns A=0 with Z.
Not yet fully understood but unrelated to the DIR bug.

```z80
4B00  85          add a,l
4B01  57          ld d,a
4B02  F1          pop af
4B03  E6 1F       and 0x1F
4B05  3C          inc a
4B06  D5          push de
4B07  CD 6C 4B    call 0x4B6C        ; MULTIPLY (8-bit)
4B0A  D1          pop de
4B0B  C6 00       add a, 0x00        ; add base offset (= 0, base is elsewhere?)
4B0D  5F          ld e,a
4B0E  AF          xor a              ; A=0
4B0F  C9          ret
```

---

## 0x4B10 — VALIDATE_DRIVE

Called from 0x4892 (file-open path: C=drive# from IX+6) and 0x4C2B (module-loader: C=0, drive 0).
See "Notes on the DIR Bug" section below for full caller context.
Sets up D=17 (directory track) via 0x4B37, then calls VALIDATE_ENTRY.
If validation succeeds, returns A=0x11 (=17) with Z.

```z80
4B10  D5          push de            ; save caller's DE
4B11  CD 37 4B    call 0x4B37        ; GET_DIR_TRACK: D=17 (IY+9), E=(B&0x1F)+2, HL=0x42xx
4B14  E5          push hl            ; save HL
4B15  2E 00       ld l, 0x00         ; L=0 (part of HL address arithmetic, not E)
4B17  CD 45 4B    call 0x4B45        ; VALIDATE_ENTRY with D=17 from 0x4B37
4B1A  E1          pop hl
4B1B  3E 11       ld a, 0x11         ; A=0x11=17 (directory track number)
4B1D  D1          pop de
4B1E  C9          ret                ; returns Z from VALIDATE_ENTRY, A=17
```

Note: VALIDATE_ENTRY is called at 0x4B17 with D=17 (via 0x4B37→0x4B65).
L=0 is set but DE is not changed after 0x4B37, so D=17, E=(B&0x1F)+2.

---

## 0x4B1F — Another validation variant

```z80
4B1F  D5          push de
4B20  CD 37 4B    call 0x4B37        ; GET_DIR_TRACK (D=17)
4B23  2E 00       ld l, 0x00
4B25  CD 68 47    call 0x4768        ; some other probe (not B9_DISPATCH)
4B28  CC 72 47    call z, 0x4772     ; if Z, another call
4B2B  D6 06       sub 6
4B2D  D1          pop de
4B2E  C8          ret z
4B2F  FE 09       cp 9
4B31  3E 12       ld a, 0x12
4B33  C0          ret nz
4B34  D6 03       sub 3
4B36  C9          ret
```

---

## 0x4B37 — GET_DIR_TRACK: read directory track from drive table IY+9

Called from VALIDATE_DRIVE (0x4B10) and also 0x4FD8 (DIR_CMD).
Returns D = directory track number (= 0x11 = 17 for drive 0).
Also sets E and HL to some computed values (used in VALIDATE_DRIVE).

```z80
4B37  CD 65 4B    call 0x4B65        ; GET_DIR_TRK_NUM → D=IY+9=17
4B3A  78          ld a,b             ; A = B (caller's B)
4B3B  E6 E0       and 0xE0           ; A = B & 0xE0 (high 3 bits)
4B3D  6F          ld l,a             ; L = B & 0xE0
4B3E  26 42       ld h, 0x42         ; H = 0x42 → HL = 0x42xx (some table address)
4B40  A8          xor b              ; A = (B & 0xE0) XOR B = B & 0x1F (low 5 bits)
4B41  C6 02       add a, 2           ; A = (B & 0x1F) + 2
4B43  5F          ld e,a             ; E = (B & 0x1F) + 2
4B44  C9          ret
; Returns: D=17 (dir track), E=(B&0x1F)+2, HL=0x42xx, B unchanged
```

---

## 0x4B45 — VALIDATE_ENTRY: outer drive validation

Called with DE where D = target track for probe (should be 17 = directory track).
Returns Z if validation succeeds (inner probe found RECTYPE → A=6).
Returns NZ if both first and fallback probes fail.

```z80
4B45  CD 5E 4B    call 0x4B5E        ; INNER_PROBE: B9_DISPATCH + sub 6
                                     ; Z=1 if A was 6 (RECTYPE → A=6, sub6 → A=0)
4B48  C8          ret z              ; SUCCESS: inner probe returned Z → done

; First probe failed. Try fallback with D=0 (track 0):
4B49  D5          push de            ; save original DE (D=17)
4B4A  11 00 00    ld de, 0x0000      ; D=0 (track 0), E=0 — track 0 has no RECTYPE!
4B4D  CD 77 47    call 0x4777        ; B9_DISPATCH with D=0 → read sector from track 0
                                     ; track 0 → status=0x00 → A=0 (from ret z at 0x46DE)
                                     ; (sub 6 NOT called here — raw return from B9_DISPATCH)
4B50  D1          pop de             ; restore DE
4B51  C0          ret nz             ; FAIL: both probes failed → return NZ (error)

; Fallback succeeded (unlikely for track 0 — see notes). Update DCT:
4B52  E5          push hl
4B53  23          inc hl
4B54  23          inc hl
4B55  56          ld d,(hl)          ; D = byte from HL+2 (some DCT update)
4B56  26 09       ld h, 9
4B58  CD A5 47    call 0x47A5        ; DRIVE_TABLE_HL: compute drive table address
4B5B  6F          ld l,a
4B5C  72          ld (hl),d          ; update drive table field
4B5D  E1          pop hl
; falls through to INNER_PROBE (0x4B5E) for a final verification:
```

---

## 0x4B5E — INNER_PROBE: call B9_DISPATCH and check for RECTYPE

The actual probe. Calls B9_DISPATCH (0x4777) which executes the B=9 driver,
seeking to track D and reading a sector. Then subtracts 6:
  - If driver returned A=6 (RECTYPE hit on track 17): A=0, Z ✓
  - If driver returned A=0 (normal read, no RECTYPE): A=0xFA (-6), NZ ✗

```z80
4B5E  CD 77 47    call 0x4777        ; B9_DISPATCH: seek to D, read sector, return A
4B61  D6 06       sub 6              ; A -= 6
4B63  C9          ret                ; Z if A was 6 (RECTYPE success), NZ otherwise
```

---

## 0x4B65 — GET_DIR_TRK_NUM: read directory track number from drive table

Reads IY+9 from the current drive's table entry (= directory track = 0x11 = 17).
Returns D = directory track number.

```z80
4B65  3E 09       ld a, 9            ; field offset = 9 (IY+9 = directory track)
4B67  CD 9C 47    call 0x479C        ; READ_DCT_FIELD: A = byte at drive_table + 9
                                     ; for drive 0: 0x4700+9 = 0x4709 = 0x11 = 17
4B6A  57          ld d,a             ; D = directory track (= 17)
4B6B  C9          ret
```

0x479C (READ_DCT_FIELD) with A=9, C=drive#:
- calls 0x47A5 to compute HL = 0x4700 + (drive&7)*10
- ld l,a → L = drive*10 + 9 → HL = 0x4700 + drive*10 + 9 = address of IY+9
- ld a,(hl) → reads the byte → for drive 0: 0x4709 = 0x11 = 17 ✓

---

## 0x4B6C — MULTIPLY: 8-bit unsigned multiply

Computes A = D * E (or similar). Details not fully analyzed.

```z80
4B6C  55          ld d,a
4B6D  AF          xor a
4B6E  06 08       ld b, 8
4B70  8F          adc a,a
4B71  CB 23       sla e
4B73  30 02       jr nc, +2
4B75  82          add a,d
4B76  10 F8       djnz -8
4B78  C1          pop bc
4B79  C9          ret
```

---

## 0x4BCB — JP_IY: drive dispatch trampoline

```z80
4BCB  FD E9       jp (iy)            ; jump to address in IY → drive table entry → 0x45FB
```

---

## 0x4BCD–0x4BEE — Interrupt/NMI handler helper

Temporarily patches 0x4315 to RET, calls something, restores. Not related to DIR bug.

```z80
4BCD  F2 EF 4B    jp p, 0x4BEF       ; if positive, skip
4BD1  E3          ex (sp),hl         ; swap HL with top of stack (return address)
4BD2  F5          push af
4BD3  21 15 43    ld hl, 0x4315
4BD6  7E          ld a,(hl)
4BD7  32 E9 4B    ld (0x4BE9),a      ; save original byte
4BDA  36 C9       ld (hl), 0xC9      ; patch to RET
4BDC  F1          pop af
4BDD  E1          pop hl
4BDE  CD F5 4B    call 0x4BF5        ; call something
4BE1  3A 0E 43    ld a,(0x430E)
4BE4  CD 00 00    call 0x0000        ; call 0x0000 (ROM entry)
4BE7  F5          push af
4BE8  3E 00       ld a, 0x00
4BEA  32 15 43    ld (0x4315),a      ; restore original byte (self-patched)
4BED  F1          pop af
4BEE  C9          ret
4BEF  3D          dec a
4BF0  00          nop
4BF1  20 XX       jr nz, ...
4BF3  3C          inc a
4BF4  C9          ret
4BF5  E5          push hl
4BF6  67          ld h,a
4BF7  78          ld a,b
4BF8  32 3B 4C    ld (0x4C3B),a
4BFB  7C          ld a,h
4BFC  F6 01       or 1
4BFE  (continues into 0x4C00...)
```

---

## Notes on the DIR Bug

The validation chain for `DIR :0`:

```
0x4FDD: call VALIDATE_ENTRY (DE={D=17,E=0})
  └─ 0x4B45: call INNER_PROBE
       └─ 0x4B5E: call B9_DISPATCH (D=17)
            └─ 0x4777 → 0x4BCB → JP(IY) → 0x45FB → 0x466B
                 └─ DO_SEEK(D=17) → seek to track 17
                      └─ Read Sector → FDC status = 0x20 (RECTYPE)
                           → BIT_COUNT_LOOP → A=6
                 └─ sub 6 → A=0, Z ✓
       └─ ret z (success)
  └─ ret z (success)
0x4FE0: jp nz,ERROR → NOT TAKEN → continues normally
```

**If this chain is correct, the DIR bug should not exist.** Yet it does.
Possible remaining hypotheses:
1. D is NOT 17 when 0x4B45 is called (despite 0x4B65 apparently setting it)
2. The FDC returns wrong status for track 17 in some edge case
3. IY does not point to the drive 0 entry (wrong drive selected)
4. The seek doesn't actually happen (bit4(C) check at 0x46A9 prevents it)

**Known callers of VALIDATE_DRIVE (0x4B10):**
- **0x4892** (file-open path): loads B from IX+7, C from IX+6 (drive# from open-file struct). Calls 0x4B10. On success writes result back to IX+8, IX+C, IX+D.
- **0x4C2B** (module-loader path): does `sbc hl,hl` (HL=0), stores HL to 0x44AA, sets C=0 (drive 0), calls VALIDATE_DRIVE. This is the DIR :0 call path.

**What patches 0x4BE4?** Code at 0x4BD3 patches 0x4315 to 0xC9 (RET) as a guard, then calls 0x4BF5. The self-modified CALL at 0x4BE4 (`CD 00 00`) is NOT patched in the memdump — it calls ROM 0x0000 literally at boot time before any module is loaded. Something at runtime must patch 0x4BE5/0x4BE6. See ldos_symbols.md SELFMOD_CALL entry.

**Recommended next step:** add a watchpoint at 0x4B45 (entry) to log:
  - D register (should be 17)
  - E register (should be 0)
  - C register (drive number — should be 0)
  - IY register (should be 0x4700 for drive 0)

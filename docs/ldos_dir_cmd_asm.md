# LDOS DIR Command Entry — Annotated Disassembly
## Range: 0x4FC0–0x5020 (from memdump.bin)

This is the entry point for the LDOS `DIR` command. The critical section is
0x4FD8–0x4FE0: load D=17 (directory track) and call VALIDATE_ENTRY.

See `ldos_symbols.md` for symbol definitions.

---

## 0x4FC0 — DIR_CMD: entry point

```z80
4FC0  3E 07       ld a, 7
4FC2  CD 10 44    call 0x4410        ; some initialization (drive-related)

; Copy drive table area for DIR processing
4FC5  21 2A 42    ld hl, 0x422A      ; source (drive data block, 70 bytes)
4FC8  11 0A 47    ld de, 0x470A      ; dest (drive 1 table entry area?)
4FCB  01 46 00    ld bc, 0x0046      ; count=70 bytes
4FCE  3A 2F 44    ld a,(0x442F)      ; flag byte
4FD1  0F          rrca               ; rotate: bit0 → carry
4FD2  38 02       jr c, +2           ; if old bit0 set → skip ld l
4FD4  2E 7A       ld l, 0x7A         ; adjust source low byte (0x7A vs 0x2A)

4FD6  ED B0       ldir               ; copy 70 bytes; after: BC=0, C=0

; Load D = directory track number (should be 17 = 0x11)
4FD8  CD 65 4B    call 0x4B65        ; GET_DIR_TRK_NUM → D=IY+9=17 (for drive 0)
                                     ; C is 0 (from ldir) when called

; Set E=0, L=0 (from C=0 after ldir)
4FDB  59          ld e,c             ; E = C = 0  (C was 0 after ldir)
4FDC  69          ld l,c             ; L = C = 0

; THE KEY VALIDATION CALL
4FDD  CD 45 4B    call 0x4B45        ; VALIDATE_ENTRY with DE={D=17, E=0}
                                     ; probes track 17 → expects RECTYPE → A=6
4FE0  C2 09 44    jp nz, 0x4409      ; NZ → ILLEGAL DRIVE NUMBER (error 0x96)
                                     ; Z  → continue with DIR processing

; ---- Post-validation: update drive flags ----
4FE3  2E CD       ld l, 0xCD
4FE5  7E          ld a,(hl)          ; read byte from HL (HL = 0x42CD or similar)
4FE6  E6 20       and 0x20           ; isolate bit 5
4FE8  47          ld b,a             ; B = bit5 from that byte
4FE9  3A 04 47    ld a,(0x4704)      ; read drive 0 table byte at IY+4
4FEC  B0          or b               ; merge bit5
4FED  32 04 47    ld (0x4704),a      ; write back to IY+4 (update drive flags)

; ---- Copy directory data to VRAM for display ----
4FF0  2E D0       ld l, 0xD0
4FF2  11 D6 3C    ld de, 0x3CD6      ; VRAM offset
4FF5  01 08 00    ld bc, 8
4FF8  ED B0       ldir               ; copy 8 bytes
4FFA  0E 08       ld c, 8
4FFC  13          inc de
4FFD  13          inc de
4FFE  ED B0       ldir               ; copy another 8 bytes

; ---- Check for '*' wildcard in filename ----
5000  7E          ld a,(hl)
5001  FE 2A       cp 0x2A            ; '*' ?
5003  20 0D       jr nz, +15         ; no wildcard → skip
5005  23          inc hl
5006  3E E6       ld a, 0xE6
5008  32 5E 50    ld (0x505E),a      ; patch code at 0x505E
500B  3E C8       ld a, 0xC8
500D  32 F0 4B    ld (0x4BF0),a      ; patch code at 0x4BF0 (ret z instruction)
5010  18 0D       jr +15             ; skip

5012  3A 41 38    ld a,(0x3841)      ; read keyboard row
5015  CB 67       bit 4,a            ; check a key
5017  C4 0D 44    call nz, 0x440D    ; handle if pressed
501A  2F          cpl                ; complement A
501B  E6 01       and 1
501D  28 03       jr z, +3
501F  7E          ld a,(hl)          ; continue...
```

---

## Critical Path for DIR Validation (0x4FD8–0x4FE0)

```
call 0x4B65     → D = IY+9 of active drive's table entry = 17 (directory track)
ld e,c          → E = 0 (C was 0 after ldir at 0x4FD6)
ld l,c          → L = 0 (harmless, HL not used until after the call)
call 0x4B45     → VALIDATE_ENTRY with DE={D=17, E=0}
jp nz,0x4409   → error if NZ
```

**Preconditions for success:**
1. IY must point to the correct drive table entry (IY+9 = 17)
2. C must be 0 when 0x4B65 is called (it is, from ldir)
3. VALIDATE_ENTRY must receive D=17

**What 0x4409 does:**
```z80
4409  3E 96       ld a, 0x96         ; error code 0x96 = "ILLEGAL DRIVE NUMBER"
440B  EF          rst 28h            ; LDOS error dispatcher (prints error, returns to prompt)
```

---

## Notes

The `ldir` at 0x4FD6 copies 70 bytes (0x46 = 70) from 0x422A (or 0x427A) to
0x470A (drive 1 entry). After ldir: BC=0, C=0. This C=0 is what makes E=0.

The flag at 0x442F (bit 0, checked at 0x4FD1) controls whether the source for
the ldir is 0x422A or 0x427A. This affects which drive configuration is copied
to the drive 1 table area, but does NOT affect C (still 0 after ldir either way).

**Question:** Is the ldir destination 0x470A really "drive 1 entry"? Drive 0 entry
is at 0x4700 (10 bytes), so drive 1 would start at 0x470A. The ldir copies 70 bytes
to 0x470A–0x4753, which covers drives 1–7 (7 × 10 bytes). This suggests the DIR
command rebuilds the drive table entries for drives 1-7 from an in-memory template.
Drive 0 (at 0x4700) is not overwritten.

The call to 0x4B65 at 0x4FD8 reads IY+9. IY must be pointing to drive 0's entry
(0x4700) at this point for the result to be 17.

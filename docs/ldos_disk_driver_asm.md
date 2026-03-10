# LDOS Disk Driver — Annotated Disassembly
## Range: 0x45E0–0x46FF (from memdump.bin)

Key routines: WAIT_SELECT (0x45E0), DRIVER_ENTRY (0x45FB), DO_SEEK (0x462A),
CMD_READ_SECTOR (0x466B), INLINE_DATA_TRICK (0x469A), BIT_COUNT_LOOP (0x46F5).

See `ldos_symbols.md` for symbol definitions.

---

## 0x45E0 — WAIT_SELECT: wait for FDC BUSY, update drive select latch

Called from DO_SEEK (0x4659) with B = FDC command byte. Selects the correct
drive via the expansion latch (0x37E1) before the command is issued.

```z80
45E0  CD 1C 46    call 0x461C        ; wait for BUSY clear; returns A=FDC status
45E3  07          rlca               ; rotate status left: bit7→carry, old carry→bit0
                                     ; carry=0 normally (bit7 of clean status = 0)
45E4  F5          push af            ; save rotated status + carry
45E5  FD 7E 03    ld a,(iy+3)        ; DCT flags byte (step rate, side select)
45E8  E6 10       and 0x10           ; isolate bit 4 (side select flag)
45EA  0F          rrca               ; bit4 → bit3 (shift right)
45EB  FD B6 04    or (iy+4)          ; combine with flags2 byte
45EE  E6 0F       and 0x0F           ; keep lower nibble (drive select lines)
45F0  32 09 43    ld (0x4309),a      ; cache drive select byte
45F3  32 E1 37    ld (0x37E1),a      ; → WRITE TO DRIVE SELECT LATCH
45F6  F1          pop af             ; restore: A=rotated status, carry=bit7 of status
45F7  D0          ret nc             ; return if carry=0 (no error; normal case)
45F8  C3 99 43    jp 0x4399          ; error: bit7 of FDC status was set → ERR_HANDLER
```

---

## 0x45FB — DRIVER_ENTRY: main LDOS disk driver dispatch

Called via `JP (IY)` from 0x4BCB. IY points to drive table entry (0x4700+).
B = command selector:
  - B=0 → return Z (no-op)
  - B=1 → 0x460B (step in)
  - B=4 → 0x4614 (step out?)
  - B=6 → 0x4604 (?)
  - B=7 → 0x461A (?)
  - B≥8 → 0x466B (Read Sector with RECTYPE; B=9 is the validation path)
  - B=0x0E,0x0F etc. → other paths

```z80
45FB  78          ld a,b             ; A = command selector
45FC  B7          or a               ; set flags
45FD  C8          ret z              ; B=0 → return Z (no-op)
45FE  FE 07       cp 7
4600  28 XX       jr z, +28          ; B=7 → some path
4602  30 XX       jr nc, +105        ; B≥8 → 0x466B (Read Sector path; B=9 goes here)
4604  FE 06       cp 6
4606  28 XX       jr z, +36          ; B=6 → some path
4608  3D          dec a
4609  28 XX       jr z, -41          ; B=1 → 0x460B (step in)
460B  FD 34 05    inc (iy+5)         ; increment current track
460E  FE 04       cp 4
4610  06 58       ld b, 0x58         ; B = 0x58 (Seek w/ verify, some flags)
4612  28 XX       jr z, +71          ; B=4 → step out path
4614  FD 36 05 00 ld (iy+5),0        ; reset current track to 0
4618  06 08       ld b, 8
461A  18 XX       jr +63             ; → some command issue path
```

---

## 0x461C — WAIT_BUSY: poll FDC until not BUSY

Returns A = FDC status (with BUSY=0). Used before issuing any FDC command.

```z80
461C  3A EC 37    ld a,(0x37EC)      ; read FDC status register
461F  CB 47       bit 0,a            ; test BUSY bit (bit 0)
4621  C8          ret z              ; BUSY=0 → done, return status in A
4622  3A 09 43    ld a,(0x4309)      ; drive select cache
4625  32 E1 37    ld (0x37E1),a      ; re-assert drive select while waiting
4628  18 F2       jr -12             ; → 0x461C (loop)
```

---

## 0x462A — DO_SEEK: seek head to track D, with sector/side calculation

Called from CMD_READ_SECTOR (0x46AB) when bit 4 of C is clear.
On entry: D=target track, E=sector number, IY→drive table entry.

```z80
462A  CD 1C 46    call 0x461C        ; wait for BUSY
462D  FD 7E 05    ld a,(iy+5)        ; current head track
4630  32 ED 37    ld (0x37ED),a      ; write to FDC track register (for Seek cmd)
4633  FD 7E 07    ld a,(iy+7)        ; sectors per track (= 9 for LDOS)
4636  E6 1F       and 0x1F           ; mask to 5 bits → A=9
4638  3C          inc a              ; A=10 (one past last sector, for side calc)
4639  FD CB 03 86 res 4,(iy+3)       ; clear side-select flag in DCT flags byte
463D  93          sub e              ; A = 10 - E (sectors remaining on side 0)
463E  ED 44       neg                ; A = -(10-E) = E-10 (negative if E<10)
4640  D5          push de
4641  FA 4F 46    jp m, 0x464F       ; if E<10 (negative result) → no side flip
4644  5F          ld e,a             ; E = E-10 (sector on side 1)
4645  FD CB 04 6E bit 5,(iy+4)       ; double-sided disk?
4649  28 06       jr z, +6           ; no → skip
464B  FD CB 03 C6 set 4,(iy+3)       ; yes → set side-select flag
464F  ED 53 EE 37 ld (0x37EE),de    ; E→0x37EE (sector reg), D→0x37EF (data = Seek target track!)
4653  D1          pop de
4654  FD 72 05    ld (iy+5),d        ; update DCT current track = D (new head position)
4657  06 18       ld b, 0x18         ; B = FDC Seek command (0x18)
4659  CD E0 45    call 0x45E0        ; WAIT_SELECT: wait BUSY, update drive select latch
465C  FD 7E 03    ld a,(iy+3)        ; DCT flags (step rate in bits 0-1)
465F  E6 03       and 0x03           ; mask step rate bits
4661  B0          or b               ; A = Seek cmd (0x18) | step_rate
4662  32 EC 37    ld (0x37EC),a      ; → ISSUE SEEK COMMAND TO FDC
4665  06 08       ld b, 8
4667  10 00       djnz $             ; tiny delay loop (8 iterations)
4669  AF          xor a              ; A=0
466A  C9          ret
```

**Key point:** `ld (0x37EE),de` at 0x464F is a 16-bit write:
  - E → address 0x37EE (FDC Sector register)
  - D → address 0x37EF (FDC Data register) ← the **Seek target track**

This is the Z80 16-bit store trick: `ld (addr),rr` stores rr_lo to addr and rr_hi to addr+1.
Since Z80 is little-endian: DE stored as {E at 0x37EE, D at 0x37EF}.

---

## 0x466B — CMD_READ_SECTOR: Read Sector with RECTYPE detection (B=9 path)

This is the critical path for drive validation. B=9 dispatches here from DRIVER_ENTRY.
The RECTYPE mechanism: track 17 (directory track) has FA (deleted) data address mark,
which sets FDC status bit 5 (0x20 = ST_RECTYPE) after a successful read.
The bit-count loop at 0x46F5 converts 0x20 → A=6, which the validation code checks.

```z80
466B  01 88 05    ld bc, 0x0588      ; C=0x88 (Read Sector cmd), B=5 (temp)
466E  FE 0A       cp 0x0A            ; A = original B value from entry
4670  28 XX       jr z, +12          ; B=10 → different sector count
4672  30 XX       jr nc, +15         ; B>10 → another path (not B=9)
4674  06 0A       ld b, 0x0A         ; B=10 (sector count for this path)
4676  CD 9A 46    call 0x469A        ; INLINE_DATA_TRICK with 3 inline bytes:
                                     ;   byte[0]=0x01 → patches 0x46F6 (bit-counter initial A=1)
                                     ;   byte[1,2]=0x1A,0x02 → patches 0x46D1-D2 (LD A,(DE);LD (BC),A)
                                     ; falls through to 0x46A8 (not a normal call!)
; --- 0x4679–0x467B are INLINE DATA bytes (not instructions) ---
; 0x4679: 0x01 (initial A for bit counter)
; 0x467A: 0x1A (lo byte of instruction patch address = LD A,(DE))
; 0x467B: 0x02 (hi byte — together 0x021A? or 2-byte patch value 0x1A,0x02)

; Other call sites to 0x469A at 0x467C and 0x4694 use different inline bytes
; to patch the data-transfer instruction for write operations etc.

; ----- Falls through to here after EX(SP),HL trick -----
46A8  C5          push bc            ; save B=10, C=0x88 (Read Sector command)
46A9  CB 61       bit 4,c            ; bit 4 of 0x88 = 0 (clear) → Z=1
46AB  CC 2A 46    call z, 0x462A     ; Z=1 → CALL DO_SEEK (D=track, E=sector)
46AE  D5          push de            ; save DE (D=track, E=sector)
46AF  E5          push hl
46B0  21 EC 37    ld hl, 0x37EC      ; HL → FDC status/command register
46B3  CD 1C 46    call 0x461C        ; wait for BUSY (seek must complete)
46B6  CD FD 46    call 0x46FD        ; some setup (single byte: 0x00 = nop? ret? unclear)
46B9  7A          ld a,d             ; A = D (target track)
46BA  32 ED 37    ld (0x37ED),a      ; write track to FDC track register
46BD  11 EF 37    ld de, 0x37EF      ; DE → FDC data register (for reads)
46C0  79          ld a,c             ; A = C = 0x88 (Read Sector command)
46C1  CD 62 46    call 0x4662        ; ISSUE_CMD: write 0x88 to 0x37EC → start Read Sector
46C4  C1          pop bc             ; B=10 (counter?), C=0x88
46C5  C5          push bc

; ---- DRQ transfer loop ----
; Entry from 0x46C6 on first pass (skips 0x46C8-0x46CA which are the exit path)
46C6  18 03       jr $+5             ; → 0x46CB (DRQ wait loop start)

; ---- Loop exit path (reached when DRQ=0 after transfer) ----
46C8  0F          rrca               ; A = status >> 1; carry = old bit0 (BUSY)
46C9  30 0C       jr nc, +14         ; carry=0 (BUSY=0) → jump to 0x46D7 (EI + finish)
                                     ; carry=1 (BUSY=1) → fall through to 0x46CB (wait more)

; ---- DRQ wait + data read loop ----
46CB  7E          ld a,(hl)          ; HL=0x37EC: read FDC status
46CC  CB 4F       bit 1,a            ; test DRQ bit (bit 1)
46CE  28 F8       jr z, -8           ; DRQ=0 → jump to 0x46C8 (exit/wait path)
                                     ; DRQ=1 → fall through (byte ready)
46D0  F3          di                 ; disable interrupts for atomic byte transfer
46D1  1A          ld a,(de)          ; read byte from FDC data register (0x37EF)
                                     ; [PATCHED by 0x469A for write ops]
46D2  02          ld (bc),a          ; store to sector buffer at BC
                                     ; [PATCHED by 0x469A for write ops]
46D3  03          inc bc             ; advance buffer pointer
46D4  C3 CB 46    jp 0x46CB          ; loop for next byte

; ---- Post-transfer processing ----
46D7  FB          ei                 ; re-enable interrupts
46D8  7E          ld a,(hl)          ; read FINAL FDC status (HL=0x37EC)
                                     ; [WATCHPOINT: logged here in wp5.txt]
                                     ;   track 17 → A=0x20 (RECTYPE set) → validation succeeds
                                     ;   other tracks → A=0x00 (clean) → validation fails
46D9  E6 7C       and 0x7C           ; mask: 0x7C = 0111_1100 (bits 2,3,4,5,6)
                                     ;   bit5=RECTYPE(0x20), bit4=RNF(0x10), bit3=CRC(0x08), bit2=LOSTDATA(0x04)
46DB  E1          pop hl
46DC  D1          pop de
46DD  C1          pop bc
46DE  C8          ret z              ; A=0 (no error, no RECTYPE) → return Z ← NORMAL DATA success
                                     ; NB: this returns A=0 which FAILS the sub-6 check at 0x4B61!

46DF  CB 57       bit 2,a            ; test bit 2 (LOST DATA error)
46E1  20 XX       jr nz, -57         ; LOST DATA → retry (jump back into read setup)
46E3  F5          push af            ; A = status & 0x7C (e.g. 0x20 for RECTYPE)
46E4  E6 18       and 0x18           ; test bits 3,4 (CRC error=0x08, RNF=0x10)
46E6  28 0B       jr z, +13          ; no CRC/RNF error → jump to 0x46F3 (RECTYPE path!)
                                     ; (0x20 & 0x18 = 0 → Z=1 → taken for RECTYPE)
46E8  CB 60       bit 4,a            ; RNF error?
46EA  C4 14 46    call nz, 0x4614    ; handle RNF → step out
46EE  C1          pop bc
46EF  C1          pop bc             ; hmm, two pops? (balancing stack after retry)
46F0  10 XX       djnz -72           ; retry loop (B=10 retries)
46F2  06 F1       ld b, 0xF1         ; [DATA BYTE — NOT an instruction at runtime!]
                                     ; 0x46F3 is reached by jr z at 0x46E6 → jumps INTO this
                                     ; 2-byte instruction, treating 0xF1 as the opcode
; ---- BIT_COUNT_LOOP (entered at 0x46F3 via jr z from 0x46E6) ----
; [0x46F3] 0xF1 = POP AF  → A = status byte (0x20 for RECTYPE), F = saved flags
46F4  47          ld b,a             ; B = 0x20 (RECTYPE byte)
46F5  3E 01       ld a, 0x01         ; A = 1 (initial count)
                                     ; [PATCHED at 0x46F6 by INLINE_DATA_TRICK]
46F7  CB 18       rrc b              ; rotate B right through carry
46F9  D8          ret c              ; carry=1 (bit reached LSB) → return A = rotation count
46FA  3C          inc a              ; count++
46FB  18 FA       jr $-4             ; → 0x46F7 (loop)
; For B=0x20: needs 6 rotations before carry fires → returns A=6 ✓
; For B=0x00: never fires carry (infinite loop — but B=0 means we took ret z at 0x46DE instead)

; ---- 0x46FD: small subroutine (called at 0x46B6) ----
46FD  00          nop
46FE  C9          ret
46FF  FD          (IY prefix — not a standalone instruction; see 0x4BCB)
```

---

## Data Flow Summary for B=9 Validation Probe

```
Caller sets:  D=17 (directory track), E=0, C=drive#
  ↓
0x4777 (B9_DISPATCH): B=9, setup IY → drive table, call 0x4BCB
  ↓
0x4BCB: JP(IY) → 0x45FB (DRIVER_ENTRY)
  ↓
0x45FB: A=9 ≥ 8 → jr nc → 0x466B (CMD_READ_SECTOR)
  ↓
0x466B: B=10, call 0x469A (patches self-mod bytes, falls through)
  ↓
0x46AB: bit4(C)=0 → CALL DO_SEEK (D=17, E=0)
  ↓
0x462A: seek to track 17; ld(0x37EF),D=17; issue Seek cmd; IY+5=17
  ↓
0x46C1: issue Read Sector cmd (0x88)
  ↓
DRQ loop: read 256 bytes; exits when DRQ=0 AND BUSY=0
  ↓
0x46D8: read final status
  - Track 17: status=0x20 (RECTYPE) → and 0x7C → 0x20 → NZ → fall through
    → 0x46E3: push 0x20; and 0x18 → 0x00 → Z → jr z → 0x46F3 (BIT_COUNT_LOOP)
    → pop AF (A=0x20); B=0x20; count rotations → A=6; ret
    → sub 6 at 0x4B61 → A=0, Z ✓ (validation SUCCESS)
  - Other tracks: status=0x00 → and 0x7C → 0x00 → Z → ret z at 0x46DE
    → returns A=0; sub 6 at 0x4B61 → A=-6, NZ ✗ (validation FAILS)
```

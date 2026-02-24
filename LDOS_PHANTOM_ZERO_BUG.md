# Phantom '0' Bug — LDOS 5.3.1 Input Prompts

## Observed Behaviour

When LDOS 5.3.1 boots and shows the `DATE ?` prompt, the character `0` is
sometimes already present in the input field before the user has typed anything.
Example from screenshot: `DATE ? 0` with cursor after the zero.

- The `0` is **deletable with backspace** — it is in LDOS's command-line input
  buffer, not a display artifact.
- The `DATE ?` prompt is affected. The `TIME ?` prompt has **not** been observed
  to pre-fill with `0`.
- The LDOS command prompt (`>`) is **also** sometimes pre-filled with `0` after
  the user successfully completes date/time entry.
- The bug is **intermittent** — sometimes date/time entry completes normally.

## What Has Been Ruled Out

### Cursor display artifact
The `0` is deletable with backspace, which removes it from LDOS's input buffer
and from the screen. A blinking cursor (VRAM write only) cannot be deleted this
way. Cursor theory is **discarded**.

### SDL keyboard matrix leak from a prior keypress
The `0` appears at the `DATE ?` prompt — the very **first** user interaction
after boot. No keys have been pressed yet when the prompt appears, so there is
no prior keypress that could have leaked into the buffer. The "last typed
character left in matrix" theory is **discarded** for the date prompt case.

### Rendering / character ROM issue
Character `0` is ASCII 0x30. Nothing in the semigraphics rendering path or
character ROM mapping produces `0` from a non-`0` character code. Rendering
theory is **discarded**.

## What Is Known

- The `0` is **in LDOS's internal keyboard typeahead / input buffer** when the
  date prompt appears, before any SDL key events have occurred.
- The bug appears **after** the ROM shadow fix (commit that added
  `rom_shadow_` to `Bus`). Before that fix, LDOS crashed immediately after
  date/time; the phantom `0` was not observed because LDOS didn't survive that
  far.
- LDOS is version **5.3.1** (MISOSYS, 1991).

## Possible Causes Not Yet Investigated

### 1. LDOS initialisation code populating the typeahead buffer
LDOS's boot sequence runs before the date prompt appears. Some step in that
sequence may write `0x30` (`'0'`) directly into LDOS's keyboard buffer in RAM
as part of initialisation — unrelated to hardware keyboard input.

- **Not investigated**: which LDOS RAM address is the typeahead buffer, and
  whether any LDOS init code writes to it.

### 2. FDC sector data being interpreted as keyboard input
LDOS reads sectors from disk during boot (kernel load, directory read, etc.).
If a sector read returns data with byte `0x30` at a position that LDOS copies
into its keyboard buffer, that would produce a phantom `'0'`.

Our FDC sector read uses 0-indexed sector numbers. If LDOS expects 1-indexed
sectors in the sector register (matching the physical disk label), we are
returning data from the wrong sector — every sector read would be off by one.
This is **not confirmed** and **not investigated**.

### 3. Interrupt handler initialisation side-effect
LDOS installs its ISR by writing machine code to the ROM shadow area
(0x0000–0x2FFF). Some byte in that machine code (e.g., opcode `0x30` = `JR NC`)
might coincide with a memory location that LDOS also uses to initialise its
keyboard buffer pointer, cursor position, or typeahead count — producing an
initial `'0'` in the buffer by coincidence of memory layout.

- **Not investigated**: which ROM shadow addresses LDOS writes to, and what
  values.

### 4. Timer interrupt firing during LDOS init with stale matrix state
LDOS's ISR (now correctly installed via ROM shadow) scans the keyboard matrix
60×/sec. Our keyboard matrix is initialised to all-zeros. If LDOS's debounce
logic misinterprets the initial all-zero state as a transition on the `0` key
row (row 4, bit 0) during the first few interrupt cycles, it might queue `'0'`
to the typeahead buffer.

- **Not investigated**: LDOS's keyboard debounce logic and initial state
  comparison.

## Diagnostic Steps Not Yet Taken

1. **Add ROM shadow write logging** — log every write to 0x0000–0x2FFF (address
   and value) during the LDOS boot sequence. Would reveal what LDOS writes and
   whether any byte is `0x30`.

2. **Add keyboard matrix logging** — log any time `keyboard_matrix[4] & 0x01`
   changes state. Would confirm whether a phantom key event reaches the matrix.

3. **Trace LDOS RAM** — watch the memory address of LDOS's typeahead buffer
   (unknown; needs investigation) for writes during boot before the date prompt.

4. **Test with a different LDOS version** — determine if the bug is specific to
   LDOS 5.3.1 or present in other versions.

## Related Context

- ROM shadow was added to fix a crash (FDC interrupt loop caused by LDOS's ISR
  not being installed). See `Bus::rom_shadow_` / `Bus::rom_shadow_active_`.
- The FDC implementation is new (`src/fdc/FDC.cpp`) and largely untested beyond
  basic boot.
- The DD/FD prefix fall-through fix in `z80.cpp` (unimplemented prefixed opcodes
  now execute the un-prefixed opcode) changed Z80 behaviour and may affect LDOS
  code paths, though no direct connection to this bug has been identified.

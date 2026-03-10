# Mal-80 — TRS-80 Model I Emulator (Claude Context)

## Project Overview
C++20 TRS-80 Model I emulator targeting macOS M4 (arm64). Z80 CPU passes all
67 ZEXALL tests. Has working CLOAD/CSAVE cassette emulation, SYSTEM (machine
language loader), .bas file injection, turbo mode, --load CLI, 1-bit audio
emulation (port 0xFF with IIR filters), a circular trace buffer with freeze
detector, FD1771 floppy disk controller emulation (JV1 format), and LDOS 5.3.1
disk OS support.

## Build & Run
```
make              # build → ./mal-80
make run          # build + run
make clean        # clean build artefacts
make zexall       # run ZEXALL Z80 test suite
```
Binary: `./mal-80` — runs from project root, loads `roms/level2.rom`.

CLI:
```
./mal-80 --load <name>    # auto-load a file from software/ on startup
                           # e.g. --load scarfman  (matches SCARFMAN.cas)
./mal-80 --disk <path>    # mount JV1 disk image on drive 0 (e.g. disks/ld1-531.dsk)
./mal-80 --disk0..3 <p>   # mount on specific drive number
```

## Source Layout
```
src/
├── main.cpp               Entry point (~22 lines): crash handlers + Emulator
├── Emulator.hpp/cpp       Main loop, frame pacing, interrupt delivery
├── SoftwareLoader.hpp/cpp File finding/parsing, ROM intercepts (SYSTEM/CLOAD/CSAVE)
├── KeyInjector.hpp/cpp    Keyboard injection queue + $KEY intercept
├── Debugger.hpp/cpp       Circular trace buffer + freeze detector
├── Sound.hpp/cpp          1-bit audio: IIR LP/HP filters, SDL_QueueAudio push model
├── cpu/
│   ├── z80.hpp            Z80 CPU declaration
│   └── z80.cpp            Z80 opcode implementations (~1800 LOC)
├── system/
│   ├── Bus.hpp            Memory map, cassette state, bus interface
│   └── Bus.cpp            Memory R/W, cassette FSK playback/recording
└── video/
    ├── Display.hpp        SDL display constants and class
    ├── Display.cpp        SDL rendering, keyboard matrix, character ROM
    └── CharRom.hpp        TRS-80 MCM6670P character generator ROM data (128×8 bytes)

software/                  .cas and .bas game/program files
roms/level2.rom            TRS-80 Level II BASIC ROM (required, not committed)
```

## Memory Map
```
0x0000–0x2FFF  12KB ROM (Level II BASIC)
0x3800–0x3BFF  Keyboard matrix (memory-mapped, active-low, 8 rows)
0x3C00–0x3FFF  Video RAM (1KB, 64×16 chars)
0x4000–0xFFFF  User RAM (48KB)
```

## ROM Intercept Addresses
SYSTEM/CLOAD/CSAVE intercepts live in `SoftwareLoader`; $KEY lives in `KeyInjector`.
```
0x0049  ROM_KEY          $KEY — wait-for-keypress (BASIC line input)
0x002B  ROM_KBD          $KBD — poll-only scan (INKEY$, do NOT intercept)
0x0293  ROM_SYNC_SEARCH  CSRDON — cassette sync search (CLOAD entry)
0x0284  ROM_WRITE_LEADER CSAVE write-leader entry
0x02CE  ROM_SYSTEM_ENTRY LOPHD — SYSTEM command entry
0x1A19  ROM_BASIC_READY  READY prompt (redirect here after .bas load)
```

## PC Intercept Pattern (Emulator::step_frame)
```cpp
uint16_t pc = cpu_.get_pc();
loader_.on_system_entry(pc, cpu_, bus_);
loader_.on_cload_entry(pc, cpu_, bus_, injector_);
loader_.on_cload_tracking(pc, cpu_, bus_, injector_);
loader_.on_csave_entry(pc, bus_);
if (injector_.handle_intercept(pc, cpu_, bus_, frame_ts))
    continue;  // skip cpu_.step() this cycle
```

## Class Responsibilities

### `Emulator`
Owns Bus, Z80, Display, SoftwareLoader, KeyInjector, Debugger.
Methods: `init(argc, argv)`, `run()`, `step_frame(t_budget)`,
`deliver_interrupt(frame_ts)`, `update_title()`, `pace_frame()`.
SpeedMode enum (NORMAL/TURBO) is defined here.

### `SoftwareLoader`
All software loading logic. Owns SYSTEM/CLOAD/CSAVE intercept state
(`system_active_`, `cload_active_`, `cli_autoload_path_`, etc.).
Public: `setup_from_cli()`, `on_system_entry()`, `on_cload_entry()`,
`on_cload_tracking()`, `on_csave_entry()`.
Private: `find_cas_file()`, `is_system_cas()`, `load_system_cas()`,
`extract_filename()`, `file_ext()`.

### `KeyInjector`
Keyboard injection queue (`std::queue<uint8_t>`).
Methods: `enqueue(text)`, `load_bas(path)`, `is_active()`,
`handle_intercept(pc, cpu, bus, frame_ts)` → returns bool (did fire).
`is_active()` drives the turbo mode decision in Emulator.

### `Debugger`
Circular trace buffer (500 entries) + freeze detector.
Methods: `record(cpu, ticks)`, `check_freeze(pc)` → bool,
`dump(bus)`, `has_entries()`.
Writes `trace.log` on freeze detection and again on clean exit.

### `Sound`
1-bit audio emulation (port 0xFF bit 1, the cassette output line).
Methods: `init()`, `cleanup()`, `update(sound_bit, ticks, active)`, `flush()`, `clear()`.
Uses SDL_QueueAudio (push model): `update()` accumulates samples per instruction,
`flush()` is called once per normal-mode frame. Muted during cassette I/O and turbo
mode; `clear()` purges buffered silence when returning to normal speed.
Two-stage filter: IIR low-pass (α=0.363, ~4 kHz cutoff) + DC-blocking high-pass
(α=0.999, ~7 Hz cutoff) to match the original hardware RC filter and prevent pops.

## Key Features Implemented

### SYSTEM command (machine language loader)
- Intercept at 0x02CE (LOPHD), before cassette motor starts
- Looks up `software/<name>.cas` (case-insensitive, shortest match wins)
- CAS format: 256×0x00 leader + 0xA5 sync + 0x55 type + 6-byte name +
  blocks[0x3C + count(0=256) + load_lo + load_hi + data + checksum] + 0x78 EOF
- Checksum = (load_hi + load_lo + sum(data_bytes)) mod 256
- `system_active_` flag prevents CLOAD intercept firing for same file

### CLOAD (BASIC tape loader)
- Intercept at 0x0293 (CSRDON)
- If `.bas` file found: inject via KeyInjector → turbo mode kicks in
- If `.cas` file found: start FSK cassette playback via Bus

### .bas file injection
- `KeyInjector::load_bas()` reads text, uppercases, maps \n→0x0D
- Prepends "NEW\r" to clear BASIC memory first
- Characters fed via $KEY intercept (0x0049) — fake RET with char in A
- INKEY$ uses $KBD (0x002B) so injection doesn't affect game keyboard

### Turbo mode
- `SpeedMode` enum in `Emulator.hpp`: `NORMAL` / `TURBO`
- Auto-triggers TURBO when `KeyInjector::is_active()` is true
- TURBO: 100× T-states per outer loop, render every 10th frame
- NORMAL: chrono-based sleep to maintain ~60Hz (16667µs per frame)
- Title bar shows `[TURBO]` when active

### --load CLI
- `SoftwareLoader::setup_from_cli()` resolves name → file, then:
  - SYSTEM .cas → enqueues `\nSYSTEM\n<stem>\n` keystrokes
  - BASIC .cas  → sets `cli_autoload_path_`, enqueues `CLOAD\n` + autorun
  - .bas        → `KeyInjector::load_bas()` + enqueues `RUN\n`

### File selection (`find_cas_file`)
- Searches `software/` directory, case-insensitive prefix match
- Shortest filename wins (so "SC" matches SCARFMAN.cas)
- `.bas` sorts before `.cas` — .bas takes priority if both exist

### Trace buffer + freeze detector (Debugger)
- 10000-entry circular buffer; records PC, all registers, IFF flags, ticks
- Freeze detection: same-PC streak > 100,000 OR all PCs in last 64 steps
  within a 64-byte range for 300,000,000 T-states (~170s)
- Threshold is high to avoid false triggers on LDOS idle loops
- Dumps `trace.log` on freeze; also dumps on clean exit

## Cassette Timing Constants (Bus.hpp)
```
CAS_BIT_PERIOD   = 3548  T-states/bit at 500 baud
CAS_HALF_0       = 1774  half-period for bit=0
CAS_HALF_1       = 887   half-period for bit=1
CAS_CYCLE_THRESH = 2600  threshold to distinguish short/long cycles
```

## Video Constants (Display.hpp)
```
64 chars × 16 lines, each cell 6×12 pixels → 384×192 logical
Window: 1152×576 (3× scale)
60Hz = 29498 T-states/frame (VIDEO_T_STATES_PER_FRAME in Bus.hpp)
```

## Sound Constants (Sound.hpp)
```
SAMPLE_RATE      = 44100 Hz
TICKS_PER_SAMPLE = 40    T-states/sample (1,774,000 / 44,100)
LP_ALPHA         = 0.363  IIR low-pass α (~4 kHz cutoff at 44100 Hz)
HP_ALPHA         = 0.999  DC-blocking high-pass α (~7 Hz cutoff)
AMPLITUDE        = 16384  int16_t peak (half of max, leaves headroom)
MAX_QUEUED_FRAMES = 4     cap on SDL audio queue (~67 ms)
```

## LDOS Notes

### 0x403D — display-width flag (NOT an expansion RAM flag)
0x403D bit 3 is a **ROM display-mode flag** that controls 64-column vs 32-column
(double-wide) video layout. All ROM references to 0x403D are immediately followed
by `OUT (0xFF),a` (the video/cassette control port) and cursor-pointer arithmetic
(`inc hl`/`dec hl`). There are **zero** references to 0x403D anywhere in the
LDOS kernel at 0x4000–0xFFFF.

A previous investigation session wrongly named this the "48KB RAM flag" and
tried forcing bit 3 set on every write. That caused LDOS date output to appear
double-spaced (32-column wide-character mode) and was reverted. No forced write
should be applied to 0x403D. High-RAM module loading is controlled by other
mechanism(s) in the LDOS disk boot sector, not by this flag.

### FDC (src/fdc/FDC.hpp/cpp)
FD1771-compatible controller. JV1 disk format: 35 tracks, 10 sectors/track,
256 bytes/sector. Drive 0 is the boot drive. LDOS boots from T00/S0.
Disk images in `disks/` directory.

**INDEX PULSE simulation** (`Bus::read()` for 0x37EC):
After a Type I command (Seek/Restore/Step), `Bus` sets `fdc_type1_idle_=true`
and records `last_type1_t_`. On subsequent reads of 0x37EC while in Type I idle,
bit 1 is ORed with a simulated periodic INDEX PULSE (300 RPM = 354,800 T/revolution,
5% duty ~17,740 T/pulse, phase measured from `last_type1_t_`). After any Type II/III
command, `fdc_type1_idle_=false` and no INDEX is injected. This is required for the
LDOS SVC 0xC4 motor-wait loops (three index-pulse sync loops at 0x4E78-0x4E85) to
complete instead of timing out with ILLEGAL DRIVE NUMBER.

**RECTYPE (deleted data mark)**: Track 17 (directory track) returns bit 5 set in
FDC status after a sector read, matching real JV1 format. Code: `bool deleted = (t == 17)`.

### LDOS Disk Layout Reference
`docs/LDOS_DISK_LAYOUT.md` — comprehensive reference covering JV1 format,
memory map, track layout, GAT/HIT/directory format, granule addressing, boot
track sector table, kernel sector load addresses, module copy loop, LDOS RAM
variables, ROM intercept addresses, and boot sequence.

## User Preferences
- Workflow: make change → user tests → then commit + push (never push untested)
- No auto-commit; always wait for user confirmation before git push
- Turbo mode auto-triggers on KeyInjector activity, reverts when queue empties
- When a bug report is ambiguous (e.g. unclear which code path is failing),
  **ask a clarifying question rather than guessing**.

## Recent Commits
```
1a7f930  Remove LDIR debug logging
418f281  Remove FDC debug fprintf logging
39566a3  Remove investigation watchpoints from step_frame
2648d92  Fix INDEX PULSE non-determinism and Type II DRQ contamination
7025d23  Fix DIR :0 ILLEGAL DRIVE NUMBER — simulate FD1771 INDEX PULSE in Bus
8fda394  Add disassembly tools for LDOS investigation
0253904  Add tinyfiledialogs header file for cross-platform file dialog support
c247a14  Part way through fixing LDOS startup issues
bcb5e00  Refactor Emulator and CPU Logic; Enhance Memory Banking Documentation
```

# Mal-80 — TRS-80 Model I Emulator (Claude Context)

## Project Overview
C++20 TRS-80 Model I emulator targeting macOS M4 (arm64). Z80 CPU passes all
67 ZEXALL tests. Has working CLOAD/CSAVE cassette emulation, SYSTEM (machine
language loader), .bas file injection, turbo mode, --load CLI, 1-bit audio
emulation (port 0xFF with IIR filters), and a circular trace buffer with freeze
detector.

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
- 500-entry circular buffer; records PC, all registers, IFF flags, ticks
- Freeze detection: same-PC streak > 100,000 OR all PCs in last 64 steps
  within a 64-byte range for 3,000,000 T-states (~1.7s)
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

## User Preferences
- Workflow: make change → user tests → then commit + push (never push untested)
- No auto-commit; always wait for user confirmation before git push
- Turbo mode auto-triggers on KeyInjector activity, reverts when queue empties
- When a bug report is ambiguous (e.g. unclear which code path is failing),
  **ask a clarifying question rather than guessing**.

## Recent Commits
```
ecb7a93  Refactor audio filter initialization and remove debug logging for keyboard events
670380d  Enhance debugging output for key events and keyboard reads; refine freeze detection logic in Debugger
a5d602d  Add sound emulation support with SDL audio integration
f33133b  Update CLAUDE.md and README to reflect refactored architecture
6540efa  Implement Debugger, Emulator, KeyInjector, and SoftwareLoader classes
5d41b38  Add clarification on handling ambiguous bug reports; improve error handling in main
```

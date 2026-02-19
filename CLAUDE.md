# Mal-80 — TRS-80 Model I Emulator (Claude Context)

## Project Overview
C++20 TRS-80 Model I emulator targeting macOS M4 (arm64). Z80 CPU passes all
67 ZEXALL tests. Has working CLOAD/CSAVE cassette emulation, SYSTEM (machine
language loader), .bas file injection, and turbo mode.

## Build & Run
```
make              # build → ./mal-80
make run          # build + run
make clean        # clean build artefacts
make zexall       # run ZEXALL Z80 test suite
```
Binary: `./mal-80` — runs from project root, loads `roms/level2.rom`.

## Key Source Files
| File | Purpose |
|------|---------|
| `src/main.cpp` | Main loop, ROM intercepts, cassette/bas loading, turbo mode |
| `src/cpu/z80.hpp` | Z80 CPU (header-only implementation) |
| `src/cpu/z80.cpp` | Z80 CPU compilation unit |
| `src/system/Bus.hpp` | Memory map, cassette state, bus interface |
| `src/system/Bus.cpp` | Memory R/W, cassette FSK playback/recording |
| `src/video/Display.hpp` | SDL display constants and class |
| `src/video/Display.cpp` | SDL rendering, keyboard matrix, character ROM |
| `software/` | .cas and .bas game/program files |
| `roms/level2.rom` | TRS-80 Level II BASIC ROM (required) |

## Memory Map
```
0x0000–0x2FFF  12KB ROM (Level II BASIC)
0x3800–0x3BFF  Keyboard matrix (memory-mapped, active-low, 8 rows)
0x3C00–0x3FFF  Video RAM (1KB, 64×16 chars)
0x4000–0xFFFF  User RAM (48KB)
```

## ROM Intercept Addresses (in main.cpp)
```
0x0049  ROM_KEY         $KEY — wait-for-keypress (BASIC line input)
0x002B  ROM_KBD         $KBD — poll-only scan (INKEY$, do NOT intercept)
0x0293  ROM_SYNC_SEARCH CSRDON — cassette sync search (CLOAD entry)
0x0284  ROM_WRITE_LEADER              CSAVE entry
0x02CE  ROM_SYSTEM_ENTRY LOPHD — SYSTEM command entry
0x1A19  ROM_BASIC_READY READY prompt (redirect here after .bas load)
```

## PC Intercept Pattern (in main.cpp inner loop)
```cpp
uint16_t pc = cpu.get_pc();
if (pc == SOME_ROM_ADDR) {
    // do something
    // optionally: fake a RET
    uint16_t sp = cpu.get_sp();
    uint16_t ret = bus.peek(sp) | (bus.peek(sp+1) << 8);
    cpu.set_sp(sp + 2);
    cpu.set_pc(ret);
    cpu.set_a(result);
    bus.add_ticks(10);
    frame_t_states += 10;
    continue;  // skip cpu.step()
}
```

## Key Features Implemented

### SYSTEM command (machine language loader)
- Intercept at 0x02CE (LOPHD), before cassette motor starts
- Looks up `software/<name>.cas` (case-insensitive, shortest match wins)
- CAS format: 256×0x00 leader + 0xA5 sync + 0x55 type + 6-byte name +
  blocks[0x3C + count(0=256) + load_lo + load_hi + data + checksum] + 0x78 EOF
- Checksum = (load_hi + load_lo + sum(data_bytes)) mod 256
- `system_active` flag prevents CLOAD intercept firing for same file

### CLOAD (BASIC tape loader)
- Intercept at 0x0293 (CSRDON)
- If `.bas` file found: inject via type_queue → turbo mode kicks in
- If `.cas` file found: start FSK cassette playback via Bus

### .bas file injection
- `load_bas_file()` reads text, uppercases, maps \n→0x0D
- Prepends "NEW\r" to clear BASIC memory first
- Characters fed via $KEY intercept (0x0049) — fake RET with char in A
- INKEY$ uses $KBD (0x002B) so injection doesn't affect game keyboard

### Turbo mode
- `SpeedMode` enum: `NORMAL` / `TURBO`
- `user_speed` = NORMAL by default (future control panel hook)
- Auto-triggers TURBO when `type_queue` is non-empty
- TURBO: 100× T-states per outer loop, render every 10th frame
- NORMAL: chrono-based sleep to maintain ~60Hz (16667µs per frame)
- Title bar shows `[TURBO]` when active
- SDL_RENDERER_PRESENTVSYNC removed; frame pacing done in software

### File selection logic (`find_cas_file`)
- Searches `software/` directory
- Case-insensitive, shortest filename wins (so "SC" matches SCARFMAN)
- `.bas` sorts before `.cas` alphabetically — .bas takes priority if both exist

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

## User Preferences
- Workflow: make change → user tests → then commit + push (never push untested)
- No auto-commit; always wait for user confirmation before git push
- Turbo mode auto-triggers on type_queue, reverts when queue empties
- When a bug report is ambiguous (e.g. unclear which code path or invocation is failing), **ask a clarifying question rather than guessing**. Do not assume and proceed with analysis of the wrong path.

## Current Task / Bug Being Investigated
**SCARFMAN freeze during attract mode**

SCARFMAN loads fine, plays opening scene (Pac-Man chased across screen), then
during attract mode the Pac-Man moves left twice then the emulator freezes.

### Plan
1. Add `--load <filename>` command-line argument to auto-load a SYSTEM file on startup
2. Add ring buffer of last ~200 Z80 instructions (PC + registers + IFF state)
3. Add freeze detection: if same PC (or tiny loop) repeats for N steps, auto-dump
   ring buffer to `trace.log`
4. Run `./mal-80 --load scarfman`, let it freeze, analyse trace.log

### Working Hypothesis
Likely a tight polling loop waiting for:
- An interrupt that isn't arriving correctly
- A port read returning wrong value
- A keyboard scan returning unexpected data

Z80 is solid (all ZEXALL pass), so CPU instructions are not the issue.
Interrupt delivery or I/O port behaviour most likely suspect.

### Approved Tools for Debug Loop
- Writing to `trace.log` in project root should be auto-approved
- Build + run cycle: `make && ./mal-80 --load scarfman`
- Evaluate trace.log output after freeze

## Recent Commits
```
7e93ddb  Add turbo mode for fast .bas file injection
34839d6  Add .bas file loading via keyboard injection
55ceca7  Implement SYSTEM command fast loader
ea110b9  Allow shorter filenames eg "SC" for SCARFMAN etc
7e6a95a  Fix Z80 instruction timing, implement CLOAD/CSAVE cassette emulation
b28bb90  Fix all ZEXALL Z80 test failures (67/67 pass)
```

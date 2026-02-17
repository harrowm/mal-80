# Mal-80 TRS-80 Emulator - Design Document

**Version:** 1.0  
**Target Platform:** macOS M4 (ARM64)  
**Language:** C++20  
**Graphics:** SDL2  

---

## 1. Project Overview

**Mal-80** is a cycle-accurate TRS-80 Model I emulator. The primary design goal is **timing accuracy** to ensure compatibility with games that rely on exact CPU cycle counts for music, copy protection, and game logic.

### Key Design Principles
| Principle | Implementation |
|-----------|----------------|
| **Cycle Accuracy** | Video contention inserts wait states during M1 cycles |
| **Memory Safety** | `std::array` and `std::span` instead of raw pointers |
| **Modern C++** | C++20 features (lambdas, enums, constexpr) |
| **Simple Build** | Single Makefile, no CMake dependencies |
| **Native Performance** | Compiled for `arm64` (no Rosetta translation) |

---

## 2. Directory Structure

```
mal-80/
├── Makefile                # Build instructions
├── .gitignore              # Ignore ROMs, build artifacts
├── roms/                   # TRS-80 BIOS dumps (DO NOT COMMIT)
│   ├── level1.rom
│   └── level2.rom
├── build/                  # Compiled objects (auto-created)
│   ├── cpu/
│   ├── system/
│   └── video/
└── src/
    ├── main.cpp            # Entry point, SDL loop, timing
    ├── cpu/
    │   ├── Z80.hpp         # CPU header, register unions
    │   └── Z80.cpp         # Opcode tables, instruction logic
    └── system/
        ├── Bus.hpp         # Memory map, contention logic
        └── Bus.cpp         # RAM/ROM arrays, timing, ports
```

---

## 3. Memory Map

The TRS-80 Model I memory map. The video circuitry reads VRAM during active scanlines, causing bus contention with the CPU.

Reference: https://www.trs-80.com/main-internal-ram-addresses-and-routines.htm

| Address Range | Size | Description | Bus Array |
|---------------|------|-------------|----------|
| `0x0000-0x2FFF` | 12 KB | ROM (Level I/II BASIC) | `Bus::rom` |
| `0x3000-0x37FF` | 2 KB | Unused / Expansion I/O | N/A (returns `0xFF`) |
| `0x3800-0x3BFF` | 1 KB | **Keyboard** (memory-mapped, 8 rows) | Memory-mapped I/O |
| `0x3C00-0x3FFF` | 1 KB | **Video RAM** (64×16 text, `DSPAD$`) | `Bus::vram` |
| `0x4000-0xFFFF` | 48 KB | User RAM | `Bus::ram` |

### Keyboard Memory Map (`KEYAD$`, 0x3800-0x3BFF)
Address bits 0-7 select which keyboard row(s) to scan. Data bits D0-D7 return
column state (active low = pressed). Multiple rows can be scanned simultaneously
by setting multiple address bits.

| Address | Row | Keys |
|---------|-----|------|
| `0x3801` | 0 | @ A B C D E F G |
| `0x3802` | 1 | H I J K L M N O |
| `0x3804` | 2 | P Q R S T U V W |
| `0x3808` | 3 | X Y Z |
| `0x3810` | 4 | 0 1 2 3 4 5 6 7 |
| `0x3820` | 5 | 8 9 : ; , - . / |
| `0x3840` | 6 | Enter Clear Break ↑ ↓ ← → Space |
| `0x3880` | 7 | Shift |

### C++ Memory Arrays (in `Bus.hpp`)
```cpp
std::array<uint8_t, 0x3000> rom;   // 12KB ROM  (0x0000-0x2FFF)
std::array<uint8_t, 0x0400> vram;  // 1KB VRAM  (0x3C00-0x3FFF)
std::array<uint8_t, 0xC000> ram;   // 48KB RAM  (0x4000-0xFFFF)
```

---

## 4. Z80 CPU & Timing Architecture

### 4.1 M-Cycles and T-States

The Z80 executes instructions in **M-Cycles** (Machine Cycles), each composed of **T-States** (clock cycles).

| Instruction | M-Cycles | T-States (Ideal) | T-States (TRS-80 Video) |
|-------------|----------|------------------|-------------------------|
| `LD A, B`   | 1        | 4                | 4                       |
| `LD A, (HL)`| 2        | 7                | 9 (M1 has +2 wait)      |
| `CALL nn`   | 5        | 17               | 19 (M1 has +2 wait)     |

### 4.2 Video Bus Contention

The TRS-80 video circuitry steals the bus during **M1 (opcode fetch)** cycles on visible scanlines.

```
TIME (T-States) --->
       1   2   3   4   5   6
       |   |   |   |   |   |
CPU:  [T1][T2][T3][T4][T5][T6]  (M1 Cycle)
       |   |   ^   ^   |   |
       |   |   |   |   |   |
VID:  [---][---][VVV][VVV][---][---]
       |   |   |   |   |   |
BUS:  [CPU][CPU][VID][VID][CPU][CPU]
       |   |   |   |   |   |
WAIT: [0 ] [0 ] [1 ] [1 ] [0 ] [0 ]
```

**Result:** M1 cycle extends from 4 T-states to **6 T-states** during active video.

### 4.3 Implementation Flow

```cpp
// In Z80::step()
uint8_t op = fetch(true);  // is_m1=true triggers contention check

// In Bus::read()
if (is_m1 && is_visible_scanline() && in_contention_window()) {
    global_t_states += 2;  // Insert wait states
    update_video_timing(2);
}
```

---

## 5. File-by-File Summary

### `src/cpu/Z80.hpp`
**Purpose:** CPU core declaration, register definitions, opcode tables.

**Key Components:**
- `struct Registers` with **anonymous unions** for 8/16-bit access
- 5 opcode tables (`main_table`, `cb_table`, `ed_table`, `dd_table`, `fd_table`)
- `std::function<void()>` lambdas for each opcode
- Flag constants (`FLAG_S`, `FLAG_Z`, `FLAG_H`, etc.)

**Critical Design:**
```cpp
union {
    struct { uint8_t c, b; };  // Little-endian: c=low, b=high
    uint16_t bc = 0;
};
```

---

### `src/cpu/Z80.cpp`
**Purpose:** Opcode implementations, flag logic, instruction dispatch.

**Key Components:**
- `step()` - Fetches opcode, handles prefixes, dispatches to lambda
- `init_*_table()` - Initializes all 256+ opcode lambdas
- Arithmetic helpers (`op_add`, `op_sub`, `op_inc`, etc.)
- Flag setting logic (S, Z, H, P/V, N, C)

**Line Count:** ~1,800 LOC

---

### `src/system/Bus.hpp`
**Purpose:** Memory map definition, timing state, bus interface.

**Key Components:**
- Memory arrays (`rom`, `vram`, `ram`)
- Keyboard matrix (memory-mapped at `0x3800-0x3BFF`)
- Timing counters (`global_t_states`, `current_scanline`)
- Video state (`is_visible_scanline()`, `get_vram_byte()`)
- Port I/O (`read_port`, `write_port`)

**Critical Constants:**
```cpp
constexpr uint16_t VIDEO_T_STATES_PER_FRAME = 29498;
constexpr uint16_t VIDEO_TOTAL_SCANLINES = 262;
```

---

### `src/system/Bus.cpp`
**Purpose:** Memory access, contention logic, video timing, interrupts.

**Key Components:**
- `read(addr, is_m1)` - Checks contention, returns byte
- `write(addr, val)` - Writes to RAM (ROM is read-only)
- `update_video_timing(t)` - Advances scanline counter
- `should_insert_wait_state()` - Determines if CPU should pause

**Line Count:** ~400 LOC

---

### `src/main.cpp`
**Purpose:** Entry point, SDL initialization, emulation loop.

**Key Components:**
- SDL2 window/audio initialization
- ROM loading
- Main emulation loop (CPU step + video render + timing sync)
- Event polling (keyboard, quit)

**Line Count:** ~200 LOC

---

### `Makefile`
**Purpose:** Build automation for macOS M4.

**Key Flags:**
```makefile
CXX = clang++
CXXSTD = -std=c++20
CXXFLAGS = -arch arm64 -O3 -Wall
SDL_LIBS = $(shell sdl2-config --libs)
```

**Targets:**
- `make` - Build `mal-80` executable
- `make run` - Build and execute
- `make clean` - Remove build artifacts

---

## 6. Build & Run Instructions

### Prerequisites
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install SDL2 via Homebrew
brew install sdl2
```

### Build
```bash
cd mal-80
make clean && make
```

### Run
```bash
make run
```

### Debug Build
Edit `Makefile`:
```makefile
OPT = -g  # Instead of -O3
```

---

## 7. Key Technical Decisions

| Decision | Rationale |
|----------|-----------|
| **Lambda Opcode Tables** | Easier debugging than switch statements, cleaner than templates |
| **Anonymous Unions for Registers** | Allows `reg.bc` and `reg.b`/`reg.c` to share memory naturally |
| **Single-Threaded Emulation** | Avoids mutex overhead; TRS-80 is slow enough for one thread |
| **Bus Contention in `read()`** | Centralizes timing logic; CPU doesn't need to know about video |
| **`uint64_t` for T-States** | Prevents overflow during long sessions |
| **`std::array` for Memory** | Stack-allocated, bounds-safe, no malloc overhead |
| **Native ARM64 Compile** | M4 is powerful; no need for x86 translation |

---

## 8. Development Roadmap

| Phase | Goal | Files to Complete |
|-------|------|-------------------|
| **1** | CPU Core | `Z80.hpp`, `Z80.cpp` (all opcodes) |
| **2** | Memory & Timing | `Bus.hpp`, `Bus.cpp` (contention logic) |
| **3** | Video Display | `Display.hpp`, `Display.cpp` (SDL texture) |
| **4** | Input | Keyboard matrix scanning |
| **5** | Cassette | Port 0xFF, WAV loading |
| **6** | Polish | Debugger, save states, settings |

---

## 9. Testing Strategy

### Unit Tests
- Individual opcode correctness (use Z80 exercise ROMs)
- Flag setting after arithmetic operations
- 16-bit register rollover (`INC HL` from `0xFFFF` → `0x0000`)

### Integration Tests
- **ZEXDOC** - Z80 instruction exercise ROM
- **BASIC Copyright Screen** - Verify video output
- **CLOAD** - Test cassette timing accuracy
- **Games** - Verify music speed and game logic timing

### Timing Verification
```cpp
// After 1 second of emulation:
// Expected: ~1,770,000 T-states (1.77 MHz)
// Acceptable range: 1,750,000 - 1,790,000
```

---

## 10. Known Limitations

| Limitation | Impact | Future Fix |
|------------|--------|------------|
| Simplified contention window | Some games may run slightly fast | Refine `should_insert_wait_state()` |
| No DMA for cassette | Cassette motor doesn't steal cycles | Accurate for Model I (no DMA) |
| Single-threaded video | Screen updates once per frame | Acceptable for 60Hz target |
| No Expansion Interface | No floppy disk support | Add WD1791 emulation later |

---

## 11. Legal Notes

⚠️ **ROM Distribution:** TRS-80 BIOS ROMs are copyrighted. Do not include them in your repository.

```bash
# Add to .gitignore
roms/*.rom
roms/*.cas
```

Users must provide their own ROM dumps from hardware they own.

---

## 12. Quick Reference

### Common Commands
```bash
make          # Build
make run      # Build and run
make clean    # Remove build artifacts
make -j4      # Parallel build (4 cores)
```

### Debug Output
```cpp
std::cout << "PC: 0x" << std::hex << cpu.get_pc() << std::endl;
std::cout << "T-States: " << std::dec << bus.get_global_t_states() << std::endl;
std::cout << "Scanline: " << bus.get_current_scanline() << std::endl;
```

### Key Constants
| Constant | Value | Purpose |
|----------|-------|---------|
| `CPU_CLOCK` | 1.77 MHz | Z80 clock speed |
| `FRAME_RATE` | 60 Hz | Video refresh |
| `T_STATES_PER_FRAME` | 29,498 | Cycles per video frame |
| `VRAM_SIZE` | 1 KB | Screen buffer |

---

**Document Version:** 1.0  
**Last Updated:** 2024  
**Author:** Malcolm Harrow  
**Project:** Mal-80 TRS-80 Emulator

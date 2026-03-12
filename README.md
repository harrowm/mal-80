# Mal-80 — TRS-80 Model I Emulator

**Platform:** macOS M4 (arm64) | **Language:** C++20 | **Graphics:** SDL2

A TRS-80 Model I emulator with accurate Z80 emulation, cassette loading, instant
software injection, and FD1771 floppy disk controller emulation (JV1 format).

---

![SCARFMAN running in Mal-80](docs/scarfman.png)
*SCARFMAN (1981) running in Mal-80*

---

## Features

- **Z80 CPU** — passes all 67 ZEXALL tests
- **Floppy disk** — FD1771 controller, JV1 format; boots LDOS 5.3.1 to `READY` prompt
- **Instant software loading** — SYSTEM (.cas) and BASIC (.bas/.cas) files load very quickly via ROM intercepts, no FSK wait
- **Turbo mode** — 100× speed during BASIC injection, automatic throttle back to 60 Hz for gameplay
- **1-bit audio** — port 0xFF square-wave output with IIR low-pass + DC-blocking filter via SDL audio
- **CLOAD / CSAVE** — full FSK cassette emulation for normal tape workflows
- **`--load <name>`** — auto-load any software file from the command line
- **Freeze detector** — circular trace buffer auto-dumps `trace.log` if the emulator loops

---

## Quick Start

```bash
# Prerequisites
brew install sdl2

# Build
make

# Run (drops to BASIC READY prompt)
./mal-80

# Load a game directly
./mal-80 --load scarfman

# Boot LDOS 5.3.1
./mal-80 --disk disks/ld1-531.dsk
```

---

## Floppy Disk Support

Mal-80 emulates the FD1771 floppy disk controller used in the TRS-80 Model I
Expansion Interface. Disk images must be in **JV1 format** (35 tracks × 10
sectors × 256 bytes = 89,600 bytes).

### Mounting disks from the command line

```bash
# Mount one disk image on drive 0 (boot drive)
./mal-80 --disk disks/ld1-531.dsk

# Mount specific drives
./mal-80 --disk0 disks/ld1-531.dsk --disk1 disks/favourites1_80sssd_jv1.DSK
```

### Mounting disks at runtime

Press **Ctrl+0** through **Ctrl+3** to open a file picker and mount an image on
that drive while the emulator is running.

### Example: LDOS with a software disk

```bash
# Boot LDOS on drive 0, then mount a second disk on drive 1 at runtime
./mal-80 --disk disks/ld1-531.dsk
```

Once LDOS reaches the `READY` prompt:

1. Press **Ctrl+1** to mount a second disk image on drive 1
2. At the LDOS prompt type `COMMAND :1` to run a program from the disk

---

## Build Targets

| Command | Description |
|---------|-------------|
| `make` | Build `./mal-80` |
| `make run` | Build and run |
| `make clean` | Remove build artefacts |
| `make zexall` | Run ZEXALL Z80 test suite (67/67) |

---

## Hotkeys

| Key | Action |
|-----|--------|
| `Home` | TRS-80 **CLEAR** key  *(Ctrl+Left on Mac)* |
| `F5` | `@` key (always unshifted) |
| `F6` | `0` key (always unshifted) |
| `F7` | Dump RAM to `memdump.bin` |
| `F8` | Quit |
| `F9` | Toggle CRT effects on/off (scanlines + vignette) |
| `Shift+F9` | Cycle phosphor colour (white → amber → green) |
| `F10` | Warm boot — returns to BASIC `READY`, keeps program in RAM |
| `Shift+F10` | Hard reset — clears RAM |
| `Shift+F11` | Show hotkey help overlay |
| `F12` | Show about overlay |
| `Ctrl+V` | Paste clipboard as keystrokes |
| `Ctrl+0`–`Ctrl+3` | Mount a disk image on drive 0–3 (opens file picker) |

---

## Directory Structure

```
mal-80/
├── Makefile
├── roms/
│   └── level2.rom          TRS-80 Level II BASIC ROM (not committed — provide your own)
├── disks/                  JV1 floppy disk images (.dsk)
├── software/               .cas and .bas game/program files
├── docs/                   Screenshots and documentation
└── src/
    ├── main.cpp            Entry point (~22 lines)
    ├── Emulator.hpp/cpp    Main loop, frame pacing, IM1 interrupt delivery
    ├── SoftwareLoader.hpp/cpp  File loading, ROM intercepts (SYSTEM/CLOAD/CSAVE)
    ├── KeyInjector.hpp/cpp Keyboard injection queue + $KEY intercept
    ├── Debugger.hpp/cpp    Circular trace buffer + freeze detector
    ├── Sound.hpp/cpp       1-bit audio: IIR filters + SDL_QueueAudio
    ├── cpu/
    │   ├── z80.hpp         Z80 CPU declaration
    │   └── z80.cpp         All opcodes (~1800 LOC)
    ├── fdc/
    │   ├── FDC.hpp         FD1771 controller declaration
    │   └── FDC.cpp         FD1771 command emulation (JV1 format)
    ├── system/
    │   ├── Bus.hpp         Memory map, cassette/FDC state
    │   └── Bus.cpp         Memory R/W, FSK cassette playback/recording, INDEX PULSE
    └── video/
        ├── Display.hpp     SDL display constants
        ├── Display.cpp     SDL rendering, keyboard matrix, character ROM
        └── CharRom.hpp     TRS-80 MCM6670P character generator data
```

---

## Memory Map

| Address | Size | Description |
|---------|------|-------------|
| `0x0000–0x2FFF` | 12 KB | ROM (Level II BASIC) |
| `0x3800–0x3BFF` | 1 KB | Keyboard matrix (memory-mapped, active-low) |
| `0x3C00–0x3FFF` | 1 KB | Video RAM (64×16 characters) |
| `0x4000–0xFFFF` | 48 KB | User RAM |

FDC registers are memory-mapped at `0x37E0–0x37EF` (Expansion Interface range).

---

## How Software Loading Works

Rather than waiting for real FSK tape timing, Mal-80 intercepts the ROM cassette
entry points and loads files instantly:

| ROM address | Intercept | Action |
|-------------|-----------|--------|
| `0x02CE` LOPHD | SYSTEM entry | Parse `.cas` binary, write blocks to RAM, jump to exec address |
| `0x0293` CSRDON | CLOAD entry | Stream FSK playback (`.cas`) or inject keystrokes (`.bas`) |
| `0x0284` | CSAVE entry | Record typed program back to `.cas` |
| `0x0049` $KEY | Keypress wait | Drain injection queue one char at a time |

File matching is case-insensitive prefix — `--load sc` matches `SCARFMAN.cas`.
If both `.bas` and `.cas` exist for the same name, `.bas` takes priority.

---

## Display

- 64×16 character display, each cell 6×12 pixels → 384×192 logical resolution
- Rendered at 3× scale: 1152×576 window
- ~60 Hz frame rate (29,498 T-states/frame)
- Title bar shows cassette status and `[TURBO]` when injection is active

---

## ROM

You must supply your own `roms/level2.rom` (12,288 bytes). TRS-80 ROMs are
copyrighted.

---

## Legal

ROMs are not included. `roms/` is in `.gitignore`.
Software in `software/` is freeware or shareware from the TRS-80 homebrew community.

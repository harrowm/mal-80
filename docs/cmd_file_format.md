# TRS-80 CMD File Format & Loader Design

## CMD File Binary Structure

Every record follows the pattern: `[type: 1 byte] [count: 1 byte] [payload: count bytes]`

**Important:** `count = 0` means **256 bytes** follow (not zero — this is a common trap).

### Record Types

| Type (hex) | Name | Payload |
|---|---|---|
| `0x01` | Load block | `[addr_lo] [addr_hi] [data…]` — loads `count−2` bytes at that address |
| `0x02` | Transfer address + EOF | `[addr_lo] [addr_hi]` — defines the execution entry point |
| `0x03` | EOF, no transfer address | nothing — stop loading, no entry point |
| `0x04` | End of ISAM/PDS member | skipped |
| `0x05` | Module header | ASCII name string (optional, may be the first record) |
| `0x08` | ISAM directory entry | 6 or 9 bytes — member index, transfer addr, seek ptr |
| `0x0a` | End of ISAM directory | skipped |
| `0x0c` | PDS directory entry | 11 bytes — 8-char name + ISAM index + flags |
| `0x0e` | End of PDS directory | skipped |
| `0x10` | Yanked load block | same layout as `0x01` but bytes NOT stored |
| `0x1f` | Copyright block | ASCII text — printed, not loaded |

### File Identification

A CMD file is identified by the first byte being `0x01` (data record) or `0x05` (module header).
There is no magic number or fixed header beyond that.

### Data Block Count Field Quirks

- `count = 0` in a `0x01` block → 256 bytes follow including the 2-byte address → **254 bytes of data**
- `count = 2` in a `0x01` block → `count − 2 = 0` data bytes, but per xtrs loader logic this wraps to 256 data bytes
- There is **no checksum** in CMD files (unlike CAS/SYSTEM tape format)

---

## The Transfer Address Problem

The execution entry point is **optional** in CMD files. This is different from SYSTEM tape (`.cas`)
where the entry point is always present in the `0x78` EOF block.

### Cases

| Condition | Meaning |
|---|---|
| `0x02` record present | Explicit entry point — loader should jump here after loading |
| `0x03` record present | No entry point — the file is an overlay or library module |
| Multiple `0x02` records | **Last one wins.** Some files have garbage after the first `0x02`. |
| Premature EOF | Treat as load error |

### What LDOS does natively

- **Type `0x02`:** JP to the transfer address
- **Type `0x03`:** Load data, return silently to `READY>` prompt
- No execution — the program was an overlay, loaded so a parent program can call it

---

## Design Choices for mal-80 Host-Side CMD Loader

The goal is to support `--cmd <filename>` on the command line, loading the CMD file
**directly into emulated RAM** without LDOS being booted, analogous to how SYSTEM tape
intercepts work for `.cas` machine-language files.

### Choice 1: Load without LDOS

We parse and load the CMD file on the host side (in `SoftwareLoader`), write bytes directly
into `Bus` RAM, then set the Z80 PC to the transfer address. This mirrors what the SYSTEM
tape intercept does for `.cas` files.

**Rationale:** The user may not have a disk image. Keeping CMD loading independent of LDOS
means simpler CLI usage and consistent behaviour with `--load`.

**Risk / revision trigger:** If a CMD file assumes LDOS SVCs (supervisor calls) are present
(e.g., calls into 0x4000+ LDOS kernel routines), it will crash without LDOS. Some CMD files
are self-contained; others are LDOS applications. We'll discover this at test time.
If this turns out to be too limiting, we can revisit and require `--disk`.

### Choice 2: Overlay files — silently load, do not return to prompt

When a CMD file has a `0x03` record (no transfer address), we silently load its data into RAM
and continue — **not** returning to the BASIC `READY>` prompt. The rationale:

Real-world usage pattern:
1. A primary program loads (has a `0x02` entry point), starts executing
2. The primary program itself loads an overlay into RAM at a known address
3. The overlay has a `0x03` record — no transfer; the primary program calls the overlay directly

In our host-side loader we will only ever load one CMD file at startup, so the "overlay chain"
scenario doesn't arise at init time. However, if we later support CMD files calling into other
CMD files during runtime, we should **not** fall back to BASIC READY — just load and return
control to the calling program.

**Fallback for `0x03`:** If the file loaded has no transfer address, we have two sub-options:
- **(A) Halt cleanly** — print a message and stop (sensible for now)
- **(B) Jump to the lowest load address** — heuristic: the first byte written is probably code

**Initial implementation:** Use option (A) — halt with a message. Log the lowest address seen
during load so the user can see it and override manually if needed.

**Revision trigger:** If we encounter CMD files that are clearly stand-alone programs but happen
to use a `0x03` record with an implied entry at a known address, revisit option (B) or add
a `--entry 0xABCD` CLI override.

### Choice 3: Last transfer address wins

If multiple `0x02` records appear, use the last one seen. This matches LDOS native behaviour
and handles files with trailing garbage after the first transfer record.

**Revision trigger:** Not expected. This is the documented spec behaviour.

### Choice 4: Module header (`0x05`) handling

Treat `0x05` records as informational — log the module name if present, then continue parsing.
Do not require a `0x05` record (many CMD files start directly with `0x01`).

---

## CMD vs CAS (SYSTEM Tape) Comparison

| Feature | CAS (SYSTEM tape) | CMD (disk) |
|---|---|---|
| Leader/sync | 256× `0x00` + `0xA5` + `0x55` | None |
| Data block marker | `0x3C` | `0x01` |
| Data field | `[count] [load_lo] [load_hi] [data] [checksum]` | `[count] [addr_lo] [addr_hi] [data]` |
| Entry point | **Always present** — `0x78 [lo] [hi]` | **Optional** — `0x02` or absent (`0x03`) |
| Checksum | Yes — `(hi + lo + Σdata) mod 256` | None |
| Name field | Fixed 6-byte ASCII | Optional `0x05` module header |
| Emulator intercept | `SoftwareLoader::on_system_entry()` | New: `SoftwareLoader::load_cmd_file()` |

---

## References

- Tim Mann's `xtrs` source: `load_cmd.c` / `cmd.c` — the authoritative open-source TRS-80 CMD loader
- *LDOS Quarterly*, Vol 1 No 4 (April 1, 1982) — CMD format specification
- xtrs `-x` flag: `stopxfer=1` — stops at first `0x02` record instead of last

---

## Implementation Details (mal-80)

### Files changed

| File | Role |
|---|---|
| `src/miniz.h` / `src/miniz.c` | miniz 3.1.1 — public-domain single-file zip library |
| `Makefile` | `miniz.o` build rule; linked into final binary |
| `src/SoftwareLoader.hpp` | `CmdSource` struct; `load_cmd_file`, `cmd_loaded`, `on_svc_entry` public methods; `VFile` virtual file table; private helpers |
| `src/SoftwareLoader.cpp` | All new CMD parsing and SVC intercept logic (~380 lines) |
| `src/Emulator.cpp` | `--cmd` CLI arg; `load_cmd_file()` call after ROM/disk init; RST 28h intercept in `step_frame` |

### `CmdSource` struct

```cpp
struct CmdSource {
    std::string zip_path;  // empty → not from a zip
    std::string entry;     // fs path (if zip empty) or path-inside-zip
    std::string dir;       // directory containing the zip or the file
    bool from_zip() const { return !zip_path.empty(); }
};
```

### Phase 1 — `resolve_cmd_source(arg)` — four-step resolution

1. `arg` is an existing file on disk → direct `CmdSource`
2. Walk path right-to-left, check if `<prefix>.zip` exists and contains the suffix entry (case-insensitive). First match wins.
   - Example: `--cmd /games/advent/start.cmd` → checks `/games/advent.zip` for entry `start.cmd`
3. Bare name → prefix search in `software/*.cmd` (case-insensitive, shortest stem match)
4. Bare name → prefix search inside `software/*.zip` for matching `.cmd` entries

### Phase 1 — `parse_and_load_cmd(buf, bus, cpu, label)`

Parses the record stream per the format table above:
- `0x01` → write `count-2` data bytes into bus RAM; track lowest address loaded
- `0x02` → store exec address (overwrite; last wins)
- `0x03` → stop; print message with lowest address as hint; return false
- `0x05` → log module name; continue
- `0x10` → yanked block; skip the data (do not store)
- `0x1f` → copyright text; skip
- unknown → skip `count` bytes; warn once

Count=0 means 256 frame bytes; for `0x01`, data = `max(frame-2, 256)` to handle the count=2 wrap (xtrs quirk).

On success: `cpu.set_pc(exec_addr)`, return `true`.
On `0x03` or missing exec: log lowest address seen, return `false`.

### Phase 2 — RST 28h SVC intercept (`on_svc_entry`)

Activates when `cmd_loaded_` is true and PC == `0x0028` (before `cpu_.step()`).

The LDOS SVC convention:
```
RST  28h        ; push (PC+1), jump to 0x0028
DEFB svc#       ; SVC number — stacked return addr points here
```

Without LDOS present, `0x0028` contains ROM code we must not execute. The intercept:
1. Reads SP to find stacked return address → reads the `DEFB` byte = SVC number
2. Dispatches on SVC number
3. Advances the stacked return address past the `DEFB`, then fakes a `RET` (sets PC = incremented return addr, adjusts SP)

### SVC handlers implemented

| SVC | Hex | Action |
|-----|-----|--------|
| `@CLOSE` | `0x1A` | Release the VFile slot; return A=0 |
| `@OPEN` | `0x1C` | Find sibling file by FCB name; load into VFile slot; return A=0 / store handle in FCB+0x0B |
| `@READ` | `0x21` | Copy bytes from VFile slot to DE buffer for BC count; return A=0 |
| `@LOAD` | `0x26` | Find sibling CMD file; call `parse_and_load_cmd`; if overlay (no exec) return A=0; if exec found, pop RST frame and let the new code run |
| anything else | — | Log once; fake A=0 success |

### `find_sibling_bytes(name)` — overlay file lookup

- If `cmd_source_.from_zip()`: search all entries in the same zip by filename (case-insensitive)
- Else: scan `cmd_source_.dir` directory for a file matching `name` (case-insensitive)

### Virtual file table

`std::array<std::optional<VFile>, 4>` — four concurrent open files maximum. Each `VFile` holds the filename, raw byte content, and a read position cursor. The handle index (0–3) is stored in `FCB+0x0B` of the caller's File Control Block.

### Known limitation: BC return from `@READ`

The Z80 class has no public `set_b()` / `set_c()` setter (only read-only `get_bc()`). The `@READ` SVC cannot return the "bytes not transferred" count in BC. LDOS programs that check `A=0` for success work correctly. If BC-return proves necessary, add `set_b()`/`set_c()` to `src/cpu/z80.hpp`.

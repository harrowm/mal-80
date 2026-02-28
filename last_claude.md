Phantom '0' Bug — Phased Investigation Plan
I found the smoking gun in the log. Here's what it shows right before the phantom '0' appears:


[TRAP] jumped 0x03F4 → 0x0038  SP=0x41C4  A=0x00   ← ISR fires mid-scan
[TRAP-RET] 0x004D → 0x05E3  A=0x30 ('0')            ← $KEY returns '0' to LDOS
[VRAM-0x30] 0x3F1E (col=30 row=12)                   ← '0' displayed on screen
An interrupt fires at 0x03F4 — which is inside the keyboard scanner loop — and when the ISR returns, $KEY somehow comes back with 0x30. The ISR is the trigger.

Phase 1 — Confirm the interrupt delivery address
Question: When the ISR fires at 0x03F4, what PC does our emulator push onto the stack as the return address?

0x03F4 is a 2-byte instruction (RLC C = CB 01). The emulator calls cpu_.step(), which fully executes it, then calls deliver_interrupt(), which reads cpu_.get_pc() (now pointing to the next instruction at 0x03F6). But does the ISR at 0x0038 corrupt/misuse that return address?

Action: Add a one-line log in deliver_interrupt() showing the saved return address when the interrupt fires.

Phase 2 — Trace what the ISR does with the stack
Question: The ISR fires mid-scan (inside the 0x03E3 scanner loop). The scanner has live state in registers (BC=row address, HL=prev-state pointer, D=counter, E=current row value). The ISR's register saves/restores may be incomplete, or LDOS's ISR may modify data the scanner depends on.

Action: Dump the full register state (all regs + SP) at the moment the interrupt fires at 0x03F4, and again when the ISR returns.

Phase 3 — Identify what the ISR writes to the typeahead buffer
Question: The ISR calls 0x4DA6 (special-key detector). Does it also call a routine that deposits a character into the typeahead ring buffer? The [RAM-0x30] 0x4E00 log line (immediately after the phantom key is returned) suggests 0x4E00 is part of that buffer.

Action: Add a write log for the range 0x4E00–0x4E20 (and nearby) to see if the ISR writes 0x30 there during this specific interrupt.

Phase 4 — Fix
Once we know which of these is true:

(A) The ISR deposits '0' directly into a typeahead buffer that $KEY drains → fix by correcting what the ISR scanner sees (keyboard hardware state at interrupt time)
(B) The ISR corrupts scanner state (registers/prev-state buffer) so the foreground scanner misreads a key → fix by ensuring the ISR fully saves/restores state, or by making the ISR not run the keyboard scanner when the foreground scanner is mid-scan
(C) Our interrupt delivery saves the wrong return PC, causing the ISR's RET to land somewhere that returns a stale '0' → fix the PC saved in deliver_interrupt()
Shall I proceed with Phase 1 now?

yes
Read Emulator.cpp (lines 214-238)
Edit Emulator.cpp
Added 5 lines
Those are just IDE include-path squiggles (SDL.h not on the VSCode search path) — not real compile errors. Let me verify with an actual build:

Bash
IN
make -C /Users/malcolm/mal-80 2>&1 | tail -8

OUT
clang++ -std=c++20 -O3 -flto -Wall -Wextra -I/opt/homebrew/include/SDL2 -D_THREAD_SAFE -arch arm64 -c src/Emulator.cpp -o build/Emulator.o
clang++ build/video/Display.o build/cpu/z80.o build/Emulator.o build/system/Bus.o build/SoftwareLoader.o build/Sound.o build/Debugger.o build/KeyInjector.o build/fdc/FDC.o build/main.o -o mal-80 -O3 -flto -L/opt/homebrew/lib -lSDL2 -arch arm64
Clean build. Now I also need to check whether cpu_.get_bc() and cpu_.get_hl() exist on the Z80 class:

Bash
IN
grep -n "get_bc\|get_hl\|get_a\b" /Users/malcolm/mal-80/src/cpu/z80.hpp | head -20

OUT
30:    uint8_t get_a() const { return reg.a; }
38:    uint16_t get_bc() const { return reg.bc; }
40:    uint16_t get_hl() const { return reg.hl; }
All good.

What to do: Run the emulator with the LDOS disk, wait for the phantom '0' to appear at the date prompt, then quit. Look for [INT-MID-SCAN] lines in the output.

What we expect to see:

The ret= value tells us exactly which instruction the scanner was interrupted at
BC= shows which keyboard row was being scanned (0x38xx)
HL= shows where in the prev-state buffer it was writing
The key question: does the ISR fire with ret=0x03F7 or 0x03F6 (just after the scanner stored the current row's value to the prev-state buffer) — or does it fire before that store? If the ISR fires before the store, and the ISR's own scanner updates the same prev-state slot, the foreground scanner will overwrite it with stale data when it resumes.

here is the log
API Error: Rate limit reached
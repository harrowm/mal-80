Now I have both the boot_trace and trace.log. Let me analyze them carefully.

KBD-SCAN bytes 0x4DA0-0x4E20:


CB 01 6E C0 2B C9 E5 21 23 44 3A 80 38 E6 03 ED 44 3A 01 38 CB 47 28 04 30 02 CB CE 3A 40 38 CB 47 28 02 CB D6 CB 57 F5 28 0B 38 09 3A 0F 43 CB 67 20 02 CB C6 F1 E1 C9 00 ...


Looking at the byte sequence, I need to find where the actual entry point is. The bytes starting at 0x4DA0 look like they might include some padding or data before the actual code begins, so the real entry point is likely at 0x4DA6 where the actual routine starts.


So the bytes from 0x4DA0 through 0x4DA5 form the tail end of another function—a sequence of rotate, load, conditional return, decrement, and return instructions. Then at 0x4DA6, a new function begins with a push operation followed by loading the address 0x4423 into HL.
A 80 38 = LD A, (0x3880)  ← read keyboard row 7 (SHIFT, etc.)
bytes[13] at 0x4DAD = E6 03 = AND 0x03  ← mask to bits 0-1 (SHIFT and...?)
bytes[15] at 0x4DAF = ED 44 = NEG  ← A = -A = 0 - A
bytes[17] at 0x4DB1 = 3A 01 38 = LD A, (0x3801)  ← read keyboard row 0
bytes[20] at 0x4DB4 = CB 47 = BIT 0, A  ← test bit 0 (@)
bytes[22] at 0x4DB6 = 28 04 =



Continuing to trace through the disassembly, the code reads keyboard row 7 (the SHIFT key area), masks the lower two bits, negates the result, then reads keyboard row 0 and tests bit 0 to check for the @ key press. I need to trace through this keyboard scanning routine more carefully. Starting at 0x4DA6, I'm pushing HL onto the stack, then loading the address 0x4423 into HL as a working area. The code reads keyboard row 7 (the SHIFT key row) from hardware, masks the relevant bits, and applies a NEG operation to convert the result into a flag that indicates whether SHIFT is pressed.

Then it reads keyboard row 0 to check if the @ key is pressed, testing bit 0 of that row. If @ is pressed and the SHIFT condition from earlier is met, it sets bit 1 at the working address 0x4423. After that, it continues by reading keyboard row 6 to check for Enter, Clear, Break, and the arrow keys.

The NEG instruction is interesting here—it's being used to transform the masked SHIFT bits into a carry flag or condition that affects subsequent key detection logic. A`. After `AND 0x03`:
- If SHIFT not pressed: A = 0, NEG → 0x00 (carry=0, since 0-0=0)
- If SHIFT pressed (row 7 bit 0 = 1): A = 0x01 after AND, NEG → 0xFF (carry=1, since 0-1=255 with borrow)

Wait: `NEG` computes: A = -A = 0 - A. The carry flag is set if A was non-zero (i.e., there was a borrow). Actually for NEG:
- If A = 0: result = 0, carry = 0 (no borrow)
- If A != 0: carry = 1 (there was a borrow)

So NEG of SHIFT state: carry = 1 if SHIFT was pressed, carry = 0 if not. Then for key '@' (row 0, bit 0): `BIT 0, A` checks if bit 0 is set (@ pressed), and if so Z=0. The `JR Z, 0x4DBC` skips if @ isn't pressed, while `JR NC, 0x4DBC` skips if SHIFT isn't pressed. The `SET 1, (HL)` only executes when both @ and SHIFT are pressed. This seems odd for normal key detection—it's treating SHIFT as a modifier that gates whether other keys register.

So the logic is: SHIFT acts as a modifier key. When SHIFT is held, pressing @ sets bit 1 at address 0x4423 (instead of the normal bit 0). This is a shifted key variant.

I see now—the code is detecting shifted key combinations. The SHIFT key modifies which bit gets set for other keys.

This function builds a complete bitmask table of all currently-pressed keys, accounting for shift modifiers. I need to keep disassembling to see how this table is used downstream.

Looking at the trace log more carefully, I can see the keyboard scanning loop executing repeatedly. Let me parse through the register values to understand what's happening at each iteration—the trace format shows the program counter, stack pointer, and all the CPU registers at each step, so I should be able to follow the exact state as keys are being detected. I'm parsing through the Z80 CPU state at this instruction. The opcode `0A` is a load instruction that reads from the memory address stored in the BC register pair—in this case 0x3804, which corresponds to keyboard row 2. The emulator's keyboard matrix returns 0x00 for that row, so register A gets loaded with zero.

The next instruction `5F` copies that zero value into register E. Then at 0x03ED, the `AE` opcode performs an XOR operation between the accumulator and the value at memory address 0x4038, which holds the previous keyboard scan state for this row. I'm checking what that previous state was to determine if any key transitions occurred. The XOR operation with zero leaves A unchanged at 0x00, and the flags reflect this—the Z flag is set because the result is zero, while the P/V flag indicates even parity. Next comes a load instruction that writes the E register into memory at the address pointed to by HL.

After that AND operation between A and E (both zero), the Z flag remains set, so the conditional jump doesn't trigger. Now I need to find where A actually becomes non-zero during a key detection to understand the critical path through this routine.

Let me examine the trace data more systematically, looking for the first scan cycle where the AND result differs from zero. I'm tracing through the Z80 assembly execution and realizing the keyboard memory address 0x3801 should return zeros, but the trace shows A=0x00 after the load instruction, which actually matches what I'd expect from reading that keyboard row. Let me look more carefully at the opcode bytes in the trace to understand what instruction is actually being executed at each step. So the trace shows register state before each instruction executes—at 0x03EB the A register holds 0x01 from the previous instruction, then LD A,(BC) reads 0x00 from memory address [0x3801], leaving A=0x00 and F unchanged at 0x93.

Moving forward, at 0x03EC the instruction LD E,A copies that 0x00 into E, and then at 0x03ED we're XORing A with the value stored at [0x4036], which should be the previous keyboard scan state—I need to trace through what that memory location actually contains to understand the result. The flags after XOR are correct for a zero result. The next instruction stores that zero back to memory, then an AND operation with E also produces zero, maintaining the same flag state through the sequence. Now I'm looking at what happens when the scan reaches row 7 (the shift key row at 0x3880). The instruction at that address is a conditional jump that gets executed, and the program then hits a return instruction, suggesting the scan loop exits after processing all rows. The S flag is set (F=0x80), which means the JP P instruction—which jumps only when the sign flag is clear—won't be taken. So execution continues straight to the RET instruction at 0x03F9 instead of jumping to 0xEB.

Now I'm tracing through the row 7 keyboard scan at address 0x3880 to understand what's happening in the sequence before that jump instruction. I'm seeing BC=0x3840 at address 0x03EB, and tracing through the execution shows the AND E instruction produces 0x00, so the conditional jump at 0x03F0 isn't taken. Let me follow the next instruction at 0x03F2 to understand what happens in this row 6 scan cycle. I'm realizing I need to be more careful about instruction boundaries here. The `CB 01` at 0x03F4 is a complete 2-byte instruction (RLC C), so what follows at 0x03F5 should be a fresh instruction, not a continuation. Let me retrace this sequence to make sure I'm reading the opcodes correctly and understanding how the row counter and buffer pointer are advancing through this loop. Looking at the trace data, the PC jumps from 0x03F6 to 0x03EB, which means the JP P instruction was actually taken—so the target address is 0x03EB, not what I initially thought. I can see this is the complete scanner loop structure—it reads each hardware row, compares with the previous state to detect new key presses, and uses a carry flag rotation to cycle through all 8 rows before looping back to the start at 0x03EB.

Now I'm reconsidering the flag logic after the rotate instruction—specifically whether `JP P` tests the sign flag or carry flag, and tracing through what happens as the row select bit rotates left through different values like 0x20 and 0x40. After the RLC instruction, C becomes 0x01 with S=0, so the JP P branch is taken and we loop back—but now I'm realizing BC would be 0x3801 again, which means we'd be rescanning the same row we just processed at the start of the loop.

Looking at the actual trace data for row 7, I see the RET instruction executes at 0x03F9, not another jump. The key insight is that at row 7 with C=0x80, after RLC the carry is set and S=1 (not 0 as I thought), so JP P is NOT taken and we fall through to the return instead. So with C=0x40, rotating left gives C=0x80, which sets the S flag to 1. But I need to recalculate the parity flag—0x80 has only one bit set, making it odd parity, so P should be 0, not 1. 0x80 (bit 7 set → S flag = 1!)
- Old bit 7 = 0 → CY = 0
- F = S=1, Z=0, H=0, PV=parity(0x80)=odd(0), C=0, N=0
- PV for rotates: PV = P (parity), but actually for RLC the PV flag equals parity of result

Actually for Z80 rotates (RLC, RRC, RL, RR, SLA, SRA, SRL):
- S: set if result bit 7 = 1 (S is set for 0x80)
- Z: set if result = 0
- H: reset (0)
- P/V: set if parity of result is even
- N: reset (0)  
- C: carries

So when I rotate 0x40 left with RLC, I get 0x80 with the carry from bit 7 being 0. The flags work out to S=1 (since bit 7 is now set), Z=0, H=0, P/V=0 (0x80 has odd parity), N=0, and C=0, giving F=0x80 which matches the trace. Now at 0x03F6 the JP P instruction tests the sign flag—since S=1, this means the result is negative. negative", and JP P condition is "sign = 0" (positive). So JP P NOT taken when S=1. Fall through to 0x03F9 = RET!

So the loop terminates when RLC C produces a result with S=1 (bit 7 set = high bit). This happens when C becomes 0x80 (row 7 = 0x3880). After scanning row 7 and doing RLC (which wraps 0x80 to 0x01 with CY=1... wait no, I'm confused again.

Wait, let me re-read the trace. At 0x03F4 (RLC C), BC was 0x3840 (C=0x40 = row 6 = 0x3840). After RLC C: C=0x80, F=0x80 (S=1). This means we're about to scan row 7 (C=0x80), but the JP P check exits the loop before we actually load and process that row. So row 7 never gets scanned in this iteration—the loop terminates as soon as the rotate operation sets the sign flag, preventing the final row from being examined. , the trace showed BC=0x3880 which would mean C=0x80 (row 7), but I need to check the exact timing—that value appears before RLC executes, so the loop might actually include row 7 after all, with the rotation wrapping C back to 0x01 and keeping S=0 for another iteration. I'm tracing through the register progression more carefully—the loop scans rows 0 through 6, and when the RLC operation on row 6's address bit produces C=0x80 with the S flag set, the JP P instruction doesn't jump (since S=1 means negative), so execution falls through to RET and exits the loop. Row 7 at 0x3880 (the SHIFT key row) gets handled separately outside this scanning routine. NC C at... wait, actually the initial BC value.

From KBD-RINGBUF, `01 01 38` at 0x03E6 = `LD BC, 0x3801`. So BC starts at 0x3801.

Loop iteration 1 (BC=0x3801, row 0):
- Scan [0x3801] (row 0: @, A-G)
- After: INC D (D=1), INC L (L=0x37), RLC C: C=0x01 → C=0x02, CY=0, S=0
- JP P (S=0) taken → loop to 0x03EB

Continuing through iterations 2-4, each row scan increments D, shifts L forward, and rotates C left while keeping the sign flag clear, so the loop keeps jumping back.

By iteration 5, I'm scanning the digit row at 0x3810, and iteration 6 handles the punctuation row at 0x3820 before moving to row 6 at 0x3840.

In row 6, I'm scanning the special keys—Enter, Clear, Break, arrows, and space—and this is where something critical happens: the rotate left on C produces a sign flag, which causes the jump condition to fail and the loop to exit with D=7. Row 7 (the SHIFT row at 0x3880) never gets scanned, and when the function returns, I can see the register state confirms D=7 and the loop has terminated.

Now I'm back at the caller at 0x03DD after the RET. The scanner returned with A=0x00 (no key pressed) and the flags show S=1, Z=0, which the caller will use to decide what to do next. I'm looking at the next instructions to see how the caller responds to these flags. Looking at the stack unwinding after the scanner returns, the caller restores DE, IX, HL twice, and BC before executing RET. The function returns with the zero flag set and sign flag clear, then control flows back to the OR A instruction at 0x004C in the $KEY routine.

Since OR A with A=0x00 sets the zero flag, the conditional return at 0x004D doesn't trigger, and the loop branches back to call $KBD again. The scanner found no key pressed in this iteration, so it correctly returns with A=0x00 and the routine continues polling.

Now I'm tracing through the second pass starting around tick 8524060 from the next ISR call to 0x03E3. I need to find where A becomes non-zero after the AND E instruction at 0x03F0, which would cause the conditional jump at 0x03FA to execute. Scanning through the trace data... but it looks like the 500-entry buffer ends at tick 8525928 before capturing the actual phantom '0' event. The phantom keystroke must occur after the trace fills up, so I don't have visibility into the exact moment it gets detected.

Looking at the pattern across all the scanner runs in the trace, each row shows A equals the hardware value of 0x00, gets XORed with the previous state (also 0x00), then ANDed with E to produce 0x00, so the jump never triggers. The scanner itself isn't finding any keys. But wait—I need to check what the ISR actually calls. It's invoking 0x4DA6, not 0x03E3 directly, so I should be examining that function instead. Looking at the byte sequence starting at 0x4DA6, I'm disassembling the keyboard scanner routine—it pushes HL, loads a memory address, then reads the SHIFT key row and masks specific bits to check the modifier state. I'm realizing this isn't a simple keyboard scan that tracks state changes—it's building a bitmask of currently-pressed keys stored at memory location 0x4423. Let me trace through the full disassembly to understand the complete logic. Continuing through the keyboard matrix, I'm checking the Enter key in row 6 and setting a flag if it's pressed, then examining the Break key in the same row. I'm seeing the function wrap up by restoring the registers and returning, but I'm realizing this might not be a simple key scanner after all—the ISR checks the Z flag after calling this, which suggests it's signaling something about whether a special key was found, though the logic here seems to be building a bitmask of key states rather than returning a character code. 0x4DA6 is actually a special-key detector for Enter, Break, and arrow keys rather than a general keyboard scanner, so it's not responsible for the 0x30 appearing in the typeahead buffer. The function reads row 6 of the keyboard matrix and if nothing's pressed there, it returns with Z=1, causing the interrupt service routine to exit without finding any key. I need to check what Bus.cpp is actually returning for address 0x3840 to understand where the unexpected value is coming from. I'm diving into the DCB handler at 0x03C2 to trace how it reads from the keyboard DCB at 0x4015, examining the instruction sequence and register states at each step. Tracing through the assembly instructions and stack operations...

I'm working through the instruction sequence more carefully now. The `DD E5` at address 0x03C3 is a two-byte instruction that pushes IX onto the stack, which means the next instruction `D5` (PUSH DE) actually starts at 0x03C5, not 0x03C4. Let me realign the program counter and stack pointer values with the correct byte boundaries. OP=`E5 D5` → Hmm, this seems wrong. If PUSH IX is at 0x03C3-0x03C4, then the next instruction is at 0x03C5.

Oh wait, I think the "PC" in the trace is the PC BEFORE the instruction executes. And "OP" is the bytes at that PC. So:
- At tick 8523097, PC=0x03C3, executing `DD E5` = PUSH IX. After this, PC=0x03C5.
- At tick 8523101, PC=0x03C4?? That can't be right if PUSH IX is 2 bytes.

Actually wait, let me re-read the trace. PC=0x03C3 with OP=DD E5 = PUSH IX, and SP goes from 0x41CE to 0x41CC (pushing 2 bytes). The next line shows SP=0x41CC which matches, but PC=0x03C4 doesn't make sense for a 2-byte instruction—unless the emulator is recording PC as the second byte of the instruction, which would be a bug. Or maybe the instruction at 0x03C3 is just `DD` (the IX prefix) and then something else happens at 0x03C4? I see the issue now—the trace is recording PC snapshots at different points in the instruction fetch cycle. When the CPU reads the `DD` prefix byte at 0x03C3, it increments PC to 0x03C4, then reads the `E5` byte and increments again to 0x03C5. The trace entries are capturing these intermediate PC values rather than the final address, which explains why I'm seeing 0x03C3 for the `DD E5` instruction and 0x03C4 for what follows. So the Z80 emulator treats DD/FD prefixes as separate execution steps—each call to `step()` handles one part, which is why the debugger records both the prefix byte and the actual instruction. Now I need to trace through the function starting at 0x03C2 using the bytes from the KBD-RINGBUF data.

Looking at the actual trace output, I see PUSH HL at 0x03C2, then PUSH IX at 0x03C3-0x03C4, followed by PUSH DE at 0x03C5, and then POP IX at 0x03C6—which is strange because it immediately pops the value it just pushed onto the stack. I'm tracing through the stack state more carefully now. After PUSH DE puts 0x0108 at address 0x41C8, the subsequent POP IX instruction reads from that same address, so IX gets loaded with 0x0108—which appears to be the intended behavior of this sequence. I'm getting tangled up in the stack operations here. Let me trace through this more carefully: PUSH IX stores the address 0x4015 to the stack, then PUSH DE follows, but I need to verify exactly what's happening to the stack pointer and what values are being written where. I'm seeing that D5 is the PUSH DE instruction, not PUSH BC, so DE=0x0108 means D=0x01 and E=0x08. Let me trace back through the earlier steps to verify where this value came from—the LD B,0x01 instruction shows B being set to 0x01, and I need to understand how that relates to the current DE value. So after `LD B, 0x01`, BC = 0x0108. But original BC was 0x0708 (B=0x07, C=0x08). After `LD B, 0x01`, B becomes 0x01, so BC = 0x0108. ✓

Now I'm parsing the register dump more carefully—I was miscounting the column positions. Looking at the spacing, DE is actually 0x4015 and HL is 0x4E01, not what I thought before. Let me verify the full breakdown: PC=0013, SP=41D0, AF=0044, BC=0708, DE=4015, HL=4E01, IX=4015, IY=0000, with the opcode being C5 06.

At the $KBD entry point (0x002B), the instruction `LD DE, 0x4015` is about to execute, and DE is already 0x4015, which is interesting since the instruction will set it to the same value.

Moving to the dispatch at 0x0013, I see `PUSH BC` saving BC=0708 to the stack, which decrements SP from 0x41D0 to 0x41CE. Then at 0x0014, the next instruction `LD B, 0x01` is being loaded.

At 0x0016, B has been set to 0x01 while C remains 0x08, giving BC=0108. The `JR +0x2E` instruction jumps forward 46 bytes from 0x0018, landing at address 0x0046 where I see the start of another instruction sequence.

From 0x0046, the `JP 0x03C2` instruction jumps to address 0x03C2. Looking at the state there, I can see the registers are now AF=0044, BC=0108, DE=4015, HL=4E01, with IX and IY also set. Breaking down the register values: A=0x00 and F=0x44 from AF, B=0x01 (indicating a read operation) and C=0x08 from BC, and D=0x40 with E=0x15 from DE.

The key insight is that DE=0x4015 points to the DCB address—the dispatch code must have set this up. At 0x03C2, the code pushes HL, IX, and DE onto the stack in sequence, which is interesting because both IX and DE hold the same value 0x4015. PUSH IX pushes 0x4015, then PUSH DE also pushes 0x4015? Looking at the trace, after POP IX completes at 03C8 with SP=0x41CA, it popped from 0x41C8—which should contain the last pushed value. But that was DE=0x4015, so why does IX still equal 0x4015 after the pop?

Actually, I think I'm misreading the sequence. Let me reconsider: PUSH IX puts 0x4015 on the stack, then PUSH DE puts another 0x4015 on top. When POP IX executes, it pops the topmost value (0x4015 from DE) into IX, leaving the original PUSH IX value still on the stack. So IX gets set to DE's value, which happens to be the same as its previous value—the code is effectively copying DE into IX, and in this case they're already equal.

Now I'm looking at what comes next at 0x03C8, where it appears to push DE again and load HL with some value.

Looking at the trace data, I can see that at 0x03C9 the instruction is `LD HL, 0x03DD` based on the register state showing HL=0x03DD after that instruction executes.

Then at 0x03CC, `E5` pushes HL onto the stack, moving SP from 0x41C8 to 0x41C6.

At 0x03CD, the `4F` instruction loads A into C, so C becomes 0x00.

At 0x03CE, `1A` loads the value at address DE (0x4015) into A, which gives A = 0x01 from the keyboard work area.

At 0x03CF, the trace confirms A is now 0x01, and `A0` performs a bitwise AND between A and B (both 0x01), keeping A at 0x01.

At 0x03D0, I'm checking the next instruction and register state...

At 0x03D1, after the compare instruction `B8`, the Z flag is set (both values equal), so the conditional jump `JP NZ` won't execute since the zero flag is already set.

Moving to 0x03D4, I'm comparing A (0x01) with 0x02, which sets the carry flag since A is less than the immediate value.

Then at 0x03D6, I'm loading a value from memory into the L register using an indexed addressing mode with IX plus an offset of 1.

Looking at the trace data, after loading L from address 0x4016, I can see HL becomes 0x03E3, which means H must have been loaded with 0x03 at the previous instruction 0x03D7. I'm confirming the DCB structure at 0x4015 contains a function pointer at offsets +1 and +2 that points to 0x03E3, which is the keyboard scanner routine in ROM shadow. The next instruction at 0x03DC loads this address and executes it with a JP (HL) instruction. , RET). The scanner returns with A=0x00 and the Z flag clear based on the final state, but let me trace through what A actually contains when it exits. Looking at the keyboard scanning loop starting at 0x03E3, it loads the previous keyboard state, sets up the first row counter, and then iterates through the matrix checking for key presses—if a key is found it jumps to the handler at 0x03FA, otherwise it continues scanning.

When no key is detected across all rows, the loop exits via a conditional jump once the row counter has been rotated to 0x80, and at that point A holds the result of the final AND operation with row 6's scan data, which is 0x00. The RET instruction then returns to the DCB handler at 0x03DD, where the registers are restored from the stack.

Now I'm tracing back through the call chain to figure out where the final RET actually returns to—it looks like the initial CALL came from the $KEY routine at 0x0049, which pushed the return address 0x004C before jumping into the dispatch at 0x002B. I need to carefully trace through the stack state at the critical jump point. Before the JP (HL) instruction at 0x03DC, the stack pointer is at 0x41C6, and I'm working backwards through the push operations to understand what values are actually on the stack—the PUSH HL at 0x03CC pushed 0x03DD, and PUSH DE at 0x03C8 pushed 0x4015, but I need to re-examine the full sequence from the entry point at 0x03C2 where SP was 0x41CE to account for all the stack manipulations.

Looking at the trace data from the CALL 0x002B instruction at address 0x0049, the return address 0x004C gets pushed onto the stack, moving SP from 0x41D2 to 0x41D0, and then the code loads DE with 0.

From there, a JR instruction jumps to 0x0013 without changing the stack, where PUSH BC decrements SP to 0x41CE, followed by another jump to 0x03C2. At that address, I'm pushing HL and IX onto the stack, then doing a push-pop sequence with DE and IX that effectively leaves SP at 0x41CA. Let me retrace this section more carefully to make sure I have the stack state right. with SP=0x41CE:
1. 03C2: PUSH HL → SP = 0x41CC (HL = 0x4E01 on stack)
2. 03C3-03C4: PUSH IX → SP = 0x41CA (IX = 0x4015 on stack)
3. 03C5: PUSH DE → SP = 0x41C8 (DE = 0x4015 on stack)
4. 03C6-03C7: POP IX → SP = 0x41CA, IX = 0x4015 (popped DE which was 0x4015)
5. 03C8: PUSH DE → SP = 0x41C8 (DE = 0x4015 on stack again)
6. 03C9-03CB: LD HL, 0x03DD (no stack change)
7. 03CC: PUSH HL → SP = 0x41C6 (HL = 0x03DD on stack)
8. 03CD: LD C, A (no stack change)
9. 03CE: LD A, (DE) = LD A, [0x4015] = 0x01
10. 03CF: AND B → A=0x01 AND B=0x01 = 0x01
11. 03D0: CP B → A=0x01, B=0x01, result=0, Z=1
12. 03D1: C2 33 03 → JP NZ, 0x0333 → Z=1, NOT taken
13. 03D4: FE 02 → CP 0x02, A=0x01, result < 0x02, C=1
14. 03D6-03D8: LD L, (IX+1) → L = [0x4016] = 0xE3

I'm verifying the register state through these operations—IX stays at 0x4015 throughout since both the initial push and the pop from DE preserve that same address, so the indexed load from IX+1 correctly accesses 0x4016.

15. 03DC: JP (HL) jumps to 0x03E3 without pushing a return address, and when that address is entered, the stack contains the return address 0x03DD from the earlier CALL, along with the saved DE and IX values below it. then step 5 pushed DE back onto the stack at 0x41C8. After step 7, I pushed HL (0x03DD) at SP=0x41C6, so now the stack has HL at the top, followed by the DE value, the original IX, and further down the earlier HL and BC values from the initial PUSH instructions.

When the scanner function returns with RET at 0x03F9, it pops that top value 0x03DD and jumps back there. At 0x03DD, the code then pops DE, IX, and HL in sequence to restore those registers from the stack before continuing.

Now I'm realizing there's an issue with how the stack values are being interpreted—when BC=0x0708 was pushed earlier, the bytes got stored in little-endian order with the low byte at 0x41CE and high byte at 0x41CF, so when HL pops from 0x41CE, it's actually reading 0x0708 back, which doesn't seem right for what should be happening here. So BC was 0x0708, and the POP HL instruction correctly retrieved that value. Now I'm tracing through the next POP BC instruction at 0x03E1, which pops the return address 0x004C from the stack—that's the address right after the original CALL instruction that pushed it there.

Then RET at 0x03E2 pops from the stack position above that, which should contain the return address back to the main LDOS routine at 0x004C. I'm trying to verify that the stack manipulation throughout this call chain actually returns control to the right place. I'm tracing through the stack and return addresses more carefully now. At 0x0049, the $KEY routine executes a CALL to 0x002B (which is $KBD), pushing 0x004C as the return address. But before that, the LDOS code jumped to 0x0049 with SP already at 0x41D2, so I need to figure out what return address was pushed by that initial jump and whether it's still on the stack. TRAP fires because the interrupt caused a jump from 0x4537 (ISR RET) to 0x0049. But that's the EI;RET restoring PC to 0x0049. Not a CALL to $KEY from LDOS.

Oh! I see now. The code at 0x4537 = ISR RET. The interrupt had fired while the main code was at 0x0049 (the first instruction of $KEY). The ISR ran and returned to 0x0049. Then the trace shows PC resuming at 0x0049.

But the LDOS main code made a CALL to $KEY (0x0049) from somewhere. Looking at the trace, the call to $KEY would have happened before we entered the trace window. The SP=0x41D2 at 0x0049 includes the return address pushed by the CALL to $KEY.

Now I'm tracking the stack pointer through the nested calls and pushes: $KEY calls $KBD at 0x002B, pushing the return address 0x004C and moving SP to 0x41D0. Inside $KBD, PUSH BC drops it to 0x41CE. Then through the various pushes at 0x03C2, the stack gets deeper. When the RET instruction executes after all the pops, I'm working backwards through the stack frame to verify where SP ends up. The RET chain correctly returns to 0x004C with A=0x00, so the Z flag is set and the loop continues. The hardware scanner at 0x03E3 is returning no key found, but I still need to figure out where the 0x30 value is actually coming from—it's not from the scanner itself, so I need to trace back through the DCB handler at 0x03.

Looking at the DCB comparison logic, I'm loading the status byte from 0x4015 into A, masking it with B=0x01 to get 0x01, then comparing against B. Since they're equal, the jump at 0x03D1 isn't taken, and I'm checking if A is less than 0x02 at 0x03D4.

Continuing from 0x03D6, I'm loading the function pointer from DCB offset +1:+2 (at 0x4016-0x4017), which points to 0x03E3, then jumping there. This means when B=0x01 (read operation), the AND operation preserves the status bit and calls the read handler. But if B=0x02 (write operation), the AND would give 0x00, and the comparison would fail differently, routing to a different handler.

Now I'm wondering what happens if [0x4015] contains something other than 0x01—like 0x30—and how the ISR updates the DCB when it puts characters into the typeahead buffer through the 0x4750 routine.

I think I've been conflating two separate components: the hardware keyboard scanner at 0x03E3 that detects physical key presses and translates them to ASCII, feeding into a software typeahead buffer, and then a separate reader that pulls characters back out. These might be unified or distinct functions within the DCB handler.

Looking at the scanner routine more closely, it's loading the previous keyboard state and then iterating through the hardware rows to detect which keys have changed, building up a counter of new key presses. Looking at the KBD-WORK memory dump, I can see the raw bytes stored at that address range, but the jump condition at 0x03F0 is never actually triggered since A stays at 0x00 throughout execution. Yet the phantom '0' still appears somewhere within the $KEY loop itself, which suggests the issue originates from a different code path than I initially thought.

Tracing through the scanner invocations more carefully, I notice the same pattern repeating over and over—the scanner at 0x03E3 cycles through all the row addresses and consistently returns with A=0x00. But looking back at the very first scanner invocation in the trace, something might be different about how those initial rows are being scanned compared to subsequent iterations. I'm trying to parse the CPU trace format correctly—the columns are AF, BC, DE, HL, IX, IY in sequence, so I need to recount which values correspond to which registers at each instruction to track what's actually happening with the DE register and understand why it seems to change unexpectedly. Looking at the register state when entering 0x03E3, I see DE is 0x4015, so D=0x40 and E=0x15. Then at 0x03E9, the instruction `LD D, 0x00` resets D to zero, leaving DE=0x0015. This suggests D is being used as a counter that gets initialized to zero while E retains its original value of 0x15.

Moving through the code, at 0x03EB the instruction reads from memory address BC (which points to 0x3801) and gets 0x00 into A, then immediately overwrites E with that value at 0x03EC. So E becomes the current row scan result, meaning DE is structured as D holding the row counter and E holding the scan value for each iteration.

When the scanner finishes and returns with A=0x00 (no key detected), the DCB handler at 0x03DD pops DE back from the stack—which restores the original 0x4015 that was pushed earlier at 0x03C8.

The RET at 0x03E2 fires with A still at 0x00 since nothing modified it during the restore sequence. Back at 0x004C, the OR A instruction sets the Z flag, so the RET NZ doesn't execute and the loop continues. The scanner is working correctly for the empty hardware case, but somehow A is becoming 0x30 somewhere in the process—that's what I need to trace down to explain the phantom '0' appearing.

Now I'm realizing the key handler at 0x03FA is where the actual key press logic happens. The JR NZ at 0x03F0 jumps there with a displacement of 0x08, but the dump cuts off before showing what's in that handler. I need to see what code lives at 0x03FA to understand how the key value gets converted to 0x30.

Let me think about this differently—for the $KBD call to return 0x30, either the hardware scanner finds a key and processes it through that handler, or there's another code path that returns 0x30 without even calling the scanner, or maybe the initial state of the key buffer at 0x4036-0x403D is non-zero and affects the result.

Looking at option (c) more carefully: the KBD-WORK buffer at 0x4036 starts with all zeros for rows 0-6, so when the hardware returns 0x00 for all rows and XORs against the previous state of 0x00, I get 0x00, which means the JR NZ condition is never taken. But wait—I'm looking at the trace again and I notice the scanner visits row 4 where the '0' key is located (BC=0x3810), so I need to verify what the actual scan value is at that address.

Looking back at the initial trace entry at tick 8522612, I can see BC=0x3804 with HL=0x4038, which means we're scanning row 2 (the P-W row) and the previous state pointer is at the third row's entry in the buffer.

The trace only captures the last 500 instructions, ending at tick 8525928, but the phantom '0' appears after that point—so it's not visible in the log itself. Every row in the visible trace shows A=0x00 or E=0x00, indicating no key press detected, which makes the appearance of 0x30 even more puzzling.

Looking at the final instruction in the trace, I'm at row 4 (the '0' key row based on BC=0x3810), executing the second byte of an RLC C instruction at 0x03F5. The next instruction would be a conditional jump based on the carry flag, which gets set by rotating C=0x10 to C=0x20.

The jump is taken, looping back to scan row 5, and the keyboard scanning continues through several more rows before returning. After the scan completes and cycles back through the main loop, an interrupt fires—and this happens multiple times. The phantom '0' character appears in the typeahead buffer specifically after the third consecutive interrupt, which means the ISR itself must be writing 0x30 somewhere. I need to trace through what the ISR at 0x4518 is actually doing: it pushes registers, reads from 0x37E0 to check a timer bit, and stores something at 0x404—that's where I should focus to find what's generating this spurious character.

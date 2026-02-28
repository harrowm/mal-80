No, **the ROM does not get copied to RAM** when you connect a TRS-80 Expansion Interface (E/I). That is a common misconception, often confused with IBM PC architecture where "shadowing" copies BIOS to RAM for speed.

In the TRS-80, ROM and RAM are physically separate chips. The CPU accesses them based on **memory mapping logic**, not by copying data.

Here is the technical breakdown of how it works and how to implement it in your emulator.

---

### 1. Does ROM Get Copied to RAM?
**No.**
*   **Physical Separation:** The ROM chips (containing BASIC/Monitor) and RAM chips are distinct hardware components.
*   **No Automatic Copy:** Plugging in the Expansion Interface does not trigger a hardware copy operation.
*   **Software Copy:** Some programs *may* choose to copy routines from ROM to RAM manually (to modify them or run faster), but this is done via software instructions (`LDI`, `LDIR`), not by the hardware connection itself.

---

### 2. How "Shadow RAM" (Memory Banking) Works
In TRS-80 terminology, this is called **Memory Banking** or **RAM/ROM Switching**, not shadow RAM. It allows the same address space to show either ROM or RAM depending on a control bit.

#### **TRS-80 Model III / Model 4 (The Common Case)**
This is where the switching is most prominent.
*   **Address Range:** `$4000` – `$7FFF` (16KB region).
*   **Default State:** This region contains **ROM** (Level II BASIC).
*   **Banked State:** This region can be switched to **RAM**.
*   **Control Mechanism:**
    *   Controlled by **Bit 7 of Port `$FF`**.
    *   `OUT ($FF), A`
    *   If Bit 7 is `0`: ROM is visible at `$4000–$7FFF`.
    *   If Bit 7 is `1`: RAM is visible at `$4000–$7FFF`.
*   **Why?** This allows programs to use the extra 16KB of RAM for data/storage while keeping the ROM available for BASIC commands when needed.

#### **TRS-80 Model I with Expansion Interface**
*   **Address Range:** `$4000` – `$7FFF`.
*   **Behavior:** On a standard 16KB Model I, this space is **empty** (floating bus). The Expansion Interface fills this space with **RAM**.
*   **ROM Overlap:** The Expansion Interface *also* contains ROM (Disk BASIC), but it maps to specific locations (like `$1B00–$1BFF`), not the entire 16KB block.
*   **Switching:** There is less "switching" here compared to the Model III; it's more about enabling the RAM decode logic for that address range.

---

### 3. Implementing This in Your Emulator

Since you are building a Z80 emulator, you need to implement **memory banking logic**. You cannot just map static arrays for ROM and RAM.

#### **Step 1: Add a Memory Map State**
You need a variable to track the current memory configuration.

```cpp
class TRS80 {
    uint8_t* rom;      // 16KB BASIC ROM
    uint8_t* ram;      // 48KB+ RAM
    uint8_t* exp_ram;  // 16KB Expansion RAM (Model I E/I or Model III Bank)
    
    // Memory Banking State
    bool bank_4000_is_ram;  // False = ROM, True = RAM
};
```

#### **Step 2: Hook the Control Port**
You need to intercept `OUT` instructions to the hardware control port.

*   **Model III:** Port `$FF`
*   **Model I:** Often controlled via the Expansion Interface latch (varies by revision, often Port `$FF` or decoded via address lines).

```cpp
void TRS80::out(uint16_t port, uint8_t value) {
    if (port == 0xFF) { // Model III Memory Control
        // Bit 7 controls the $4000-$7FFF bank
        bank_4000_is_ram = (value & 0x80) != 0;
        
        // Other bits control cassette, etc.
    }
    // Handle other ports...
}
```

#### **Step 3: Update Read/Write Logic**
Your memory access functions must check the banking state for the specific address range.

```cpp
uint8_t TRS80::read_memory(uint16_t addr) {
    // 0x0000 - 0x3FFF: Always ROM
    if (addr < 0x4000) {
        return rom[addr];
    }
    
    // 0x4000 - 0x7FFF: Switchable (Model III)
    if (addr >= 0x4000 && addr < 0x8000) {
        if (bank_4000_is_ram) {
            return exp_ram[addr - 0x4000]; // Read from RAM
        } else {
            return rom[addr];              // Read from ROM
        }
    }
    
    // 0x8000 - 0xFFFF: Always RAM
    return ram[addr - 0x8000];
}

void TRS80::write_memory(uint16_t addr, uint8_t value) {
    // ROM is read-only (unless banking allows write to RAM shadow)
    if (addr < 0x4000) {
        return; // Ignore write to ROM
    }
    
    if (addr >= 0x4000 && addr < 0x8000) {
        if (bank_4000_is_ram) {
            exp_ram[addr - 0x4000] = value; // Write to RAM
        } else {
            return; // Ignore write to ROM space
        }
    } else {
        ram[addr - 0x8000] = value;
    }
}
```

---

### 4. Why This Matters for Your Emulator
1.  **Disk Operating System (TRSDOS):** DOS often switches the bank to RAM to load system variables, then switches back to ROM to call BASIC routines. If your emulator doesn't support banking, DOS will crash.
2.  **Memory Size Checks:** Programs often check available RAM. If banking isn't implemented, they will see less memory than expected.
3.  **Video RAM:** Note that TRS-80 Video RAM is separate (usually `$4000–$4300` in Model I, but mapped differently in Model III). Don't confuse system RAM banking with Video RAM mapping.

### Summary
*   **No copying happens.**
*   **It is memory banking.**
*   **Implement a state flag** controlled by `OUT` instructions.
*   **Check that flag** in your `read_memory` and `write_memory` functions for the `$4000–$7FFF` range.

This approach will give you accurate hardware behavior without unnecessary data copying.
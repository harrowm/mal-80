// src/system/Bus.cpp
#include "Bus.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

Bus::Bus() {
    reset();
}

Bus::Bus(bool flat_memory) : flat_mode(flat_memory) {
    reset();
    if (flat_mode) {
        flat_mem.fill(0x00);
    }
}

Bus::~Bus() {}

void Bus::reset() {
    rom.fill(0x00);
    vram.fill(0x20);  // Fill VRAM with spaces (0x20)
    ram.fill(0x00);
    global_t_states = 0;
    current_scanline = 0;
    t_states_in_scanline = 0;
    int_pending = false;
    iff_enabled = true;
    cassette_motor_on = false;
    cassette_data_out = 0;
}

void Bus::load_rom(const std::string& path, uint16_t offset) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open ROM file: " + path);
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (offset + size > ROM_SIZE) {
        throw std::runtime_error("ROM too large for memory map");
    }

    file.read(reinterpret_cast<char*>(rom.data()) + offset, size);
    std::cout << "Loaded ROM: " << path << " (" << size << " bytes)" << std::endl;
}

// ============================================================================
// MEMORY READ (With TRS-80 Video Contention)
// ============================================================================
uint8_t Bus::read(uint16_t addr, bool is_m1) {
    // Flat memory mode: simple 64KB RAM
    if (flat_mode) {
        return flat_mem[addr];
    }

    // Check for video bus contention (TRS-80 Model I specific)
    if (should_insert_wait_state(addr, is_m1)) {
        // Insert 2 wait states during M1 cycle on visible scanlines
        global_t_states += 2;
        update_video_timing(2);
    }

    uint8_t value = 0x00;

    if (addr <= ROM_END) {
        // ROM Access (0x0000 - 0x2FFF)
        value = rom[addr];
    } else if (addr >= KEYBOARD_START && addr <= KEYBOARD_END) {
        // Keyboard (memory-mapped at 0x3800-0x3BFF)
        // Address bits 0-7 select which row(s) to scan
        if (keyboard_matrix) {
            uint8_t row_select = addr & 0xFF;
            value = 0x00;  // No keys pressed (active high)
            for (int row = 0; row < 8; row++) {
                if (row_select & (1 << row)) {
                    value |= keyboard_matrix[row];
                }
            }
        } else {
            value = 0x00;  // No keyboard connected
        }
    } else if (addr >= VRAM_START && addr <= VRAM_END) {
        // Video RAM (0x3C00 - 0x3FFF)
        value = vram[addr - VRAM_START];
    } else if (addr >= RAM_START && addr <= RAM_END) {
        // User RAM (0x4000 - 0xFFFF)
        value = ram[addr - RAM_START];
    } else {
        // Unmapped memory (0x3000-0x37FF)
        value = 0xFF;
    }

    global_t_states++;
    update_video_timing(1);

    return value;
}

// ============================================================================
// MEMORY WRITE
// ============================================================================
void Bus::write(uint16_t addr, uint8_t val) {
    // Flat memory mode: simple 64KB RAM
    if (flat_mode) {
        flat_mem[addr] = val;
        return;
    }

    // Write contention is less critical than read, but track timing
    global_t_states++;
    update_video_timing(1);

    if (addr >= VRAM_START && addr <= VRAM_END) {
        // Video RAM writes (0x3C00-0x3FFF)
        vram[addr - VRAM_START] = val;
    } else if (addr >= RAM_START && addr <= RAM_END) {
        // User RAM writes (0x4000-0xFFFF)
        ram[addr - RAM_START] = val;
    }
    // ROM and keyboard are read-only, writes ignored
}

// ============================================================================
// TICK COUNTER (Called from CPU after each instruction)
// ============================================================================
void Bus::add_ticks(int t) {
    global_t_states += t;
    update_video_timing(t);
}

// ============================================================================
// VIDEO TIMING & SCANLINE TRACKING
// ============================================================================
void Bus::update_video_timing(int t_states) {
    t_states_in_scanline += t_states;

    // Check if we've completed this scanline
    if (t_states_in_scanline >= VIDEO_T_STATES_PER_SCANLINE) {
        t_states_in_scanline -= VIDEO_T_STATES_PER_SCANLINE;
        current_scanline++;

        // Check if we've completed a full frame
        if (current_scanline >= VIDEO_TOTAL_SCANLINES) {
            current_scanline = 0;
            // Trigger V-Blank interrupt at end of frame
            if (iff_enabled) {
                int_pending = true;
            }
        }
    }
}

// ============================================================================
// VIDEO CONTENTION LOGIC (The Heart of TRS-80 Accuracy)
// ============================================================================
bool Bus::should_insert_wait_state(uint16_t /*addr*/, bool is_m1) const {
    // Contention only happens during M1 (opcode fetch) cycles
    if (!is_m1) {
        return false;
    }

    // Contention only happens during visible scanlines
    if (!is_visible_scanline()) {
        return false;
    }

    // Contention happens during specific T-states in the scanline
    // TRS-80 Model I: Video accesses RAM during T-states 3-4 of M1 cycle
    // This is simplified; real hardware is more complex
    uint16_t t_in_line = t_states_in_scanline % VIDEO_T_STATES_PER_SCANLINE;

    // Video contention window (approximate - adjust based on testing)
    // Typically occurs during the middle of each scanline
    constexpr uint16_t CONTENTION_START = 30;
    constexpr uint16_t CONTENTION_END   = 90;

    if (t_in_line >= CONTENTION_START && t_in_line <= CONTENTION_END) {
        return true;
    }

    return false;
}

bool Bus::is_visible_scanline() const {
    return (current_scanline >= VIDEO_SCANLINE_START &&
            current_scanline < VIDEO_SCANLINE_END);
}

uint8_t Bus::get_vram_byte(uint16_t vram_addr) const {
    if (vram_addr < VRAM_SIZE) {
        return vram[vram_addr];
    }
    return 0x20;  // Space for out-of-range
}

// ============================================================================
// PORT I/O (Cassette & Other)
// ============================================================================
uint8_t Bus::read_port(uint8_t port) {
    // TRS-80 Model I uses port 0xFF for cassette
    if (port == 0xFF) {
        uint8_t val = 0x00;
        if (cassette_motor_on) val |= 0x01;
        val |= (cassette_data_out << 1);
        // Bit 7 would be cassette data in (requires audio simulation)
        return val;
    }
    return 0xFF;  // Unmapped ports
}

void Bus::write_port(uint8_t port, uint8_t val) {
    if (port == 0xFF) {
        // Bit 0: Cassette motor
        cassette_motor_on = (val & 0x01);
        // Bit 1: Cassette data out
        cassette_data_out = (val >> 1) & 0x01;
    }
}

// ============================================================================
// INTERRUPT HANDLING
// ============================================================================
void Bus::trigger_interrupt() {
    if (iff_enabled) {
        int_pending = true;
    }
}
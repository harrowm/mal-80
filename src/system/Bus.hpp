// src/system/Bus.hpp
#pragma once
#include <array>
#include <cstdint>
#include <vector>

// ============================================================================
// TRS-80 MODEL I MEMORY MAP
// ============================================================================
// 0x0000 - 0x2FFF : 12KB ROM (Level I/II BASIC)
// 0x3000 - 0x37FF : Unused / Mirrored
// 0x3800 - 0x3BFF : Memory-mapped keyboard (active-low, 8 rows)
// 0x3C00 - 0x3FFF : Video RAM (1KB - 64 chars × 16 lines)
// 0x4000 - 0xFFFF : User RAM (up to 48KB)
// ============================================================================

constexpr uint16_t ROM_START     = 0x0000;
constexpr uint16_t ROM_END       = 0x2FFF;
constexpr uint16_t ROM_SIZE      = 0x3000;  // 12KB

constexpr uint16_t KEYBOARD_START = 0x3800;
constexpr uint16_t KEYBOARD_END   = 0x3BFF;

constexpr uint16_t VRAM_START    = 0x3C00;
constexpr uint16_t VRAM_END      = 0x3FFF;
constexpr uint16_t VRAM_SIZE     = 0x0400;  // 1KB

constexpr uint16_t RAM_START     = 0x4000;
constexpr uint16_t RAM_END       = 0xFFFF;
constexpr uint16_t RAM_SIZE      = 0xC000;  // 48KB max

constexpr uint16_t VIDEO_SCANLINE_START = 48;   // First visible scanline
constexpr uint16_t VIDEO_SCANLINE_END   = 48 + 192; // End of visible area
constexpr uint16_t VIDEO_TOTAL_SCANLINES = 262;  // NTSC total
constexpr uint16_t VIDEO_T_STATES_PER_SCANLINE = 114; // Approx T-states per line
constexpr uint16_t VIDEO_T_STATES_PER_FRAME = 29498; // Total T-states per 60Hz frame

class Bus {
public:
    Bus();
    ~Bus();

    // Z80 Interface (called from CPU)
    uint8_t read(uint16_t addr, bool is_m1 = false);
    void write(uint16_t addr, uint8_t val);
    void add_ticks(int t);

    // System Interface (called from Main Loop)
    void reset();
    void load_rom(const std::string& path, uint16_t offset = ROM_START);
    void trigger_interrupt();
    bool interrupt_pending() const { return int_pending; }
    void clear_interrupt() { int_pending = false; }

    // Video Interface (called from Display)
    uint64_t get_global_t_states() const { return global_t_states; }
    uint16_t get_current_scanline() const { return current_scanline; }
    bool is_visible_scanline() const;
    uint8_t get_vram_byte(uint16_t vram_addr) const;
    void set_keyboard_matrix(uint8_t* km) { keyboard_matrix = km; }

    // Cassette Interface (Port 0xFF)
    uint8_t read_port(uint8_t port);
    void write_port(uint8_t port, uint8_t val);

    // Memory Access for Debugging
    const std::array<uint8_t, ROM_SIZE>& get_rom() const { return rom; }
    const std::array<uint8_t, RAM_SIZE>& get_ram() const { return ram; }

private:
    // =========================================================================
    // MEMORY ARRAYS (The actual memory storage lives here)
    // =========================================================================
    std::array<uint8_t, ROM_SIZE> rom;   // 12KB ROM
    std::array<uint8_t, VRAM_SIZE> vram; // 1KB Video RAM (0x3C00-0x3FFF)
    std::array<uint8_t, RAM_SIZE> ram;   // 48KB User RAM (0x4000-0xFFFF)

    // =========================================================================
    // KEYBOARD MATRIX (memory-mapped at 0x3800-0x3BFF)
    // =========================================================================
    uint8_t* keyboard_matrix = nullptr;  // Pointer to 8-byte keyboard matrix

    // =========================================================================
    // TIMING & STATE
    // =========================================================================
    uint64_t global_t_states = 0;       // Total T-states since reset
    uint16_t current_scanline = 0;      // Current video scanline (0-261)
    uint16_t t_states_in_scanline = 0;  // T-states within current scanline
    bool int_pending = false;           // Interrupt pending flag
    bool iff_enabled = true;            // Interrupts enabled (simplified)

    // =========================================================================
    // CASSETTE STATE
    // =========================================================================
    bool cassette_motor_on = false;
    uint8_t cassette_data_out = 0;

    // =========================================================================
    // VIDEO CONTENTION LOGIC
    // =========================================================================
    bool should_insert_wait_state(uint16_t addr, bool is_m1) const;
    void update_video_timing(int t_states);
    void check_video_contention();
};
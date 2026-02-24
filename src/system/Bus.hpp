// src/system/Bus.hpp
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "../fdc/FDC.hpp"

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

// ============================================================================
// CASSETTE TIMING CONSTANTS (500 baud FSK at 1.77408 MHz)
// ============================================================================
constexpr uint64_t CAS_BIT_PERIOD    = 3548;   // T-states per bit at 500 baud
constexpr uint64_t CAS_HALF_0        = 1774;   // Half-period for bit=0 signal
constexpr uint64_t CAS_HALF_1        = 887;    // Half-period for bit=1 signal
constexpr uint64_t CAS_CYCLE_THRESH  = 2600;   // Threshold to distinguish short/long cycles
constexpr uint64_t CAS_IDLE_TIMEOUT  = 200000; // ~113ms idle → stop recording

enum class CassetteState { IDLE, PLAYING, RECORDING };

constexpr uint16_t VIDEO_SCANLINE_START = 48;   // First visible scanline
constexpr uint16_t VIDEO_SCANLINE_END   = 48 + 192; // End of visible area
constexpr uint16_t VIDEO_TOTAL_SCANLINES = 262;  // NTSC total
constexpr uint16_t VIDEO_T_STATES_PER_SCANLINE = 114; // Approx T-states per line
constexpr uint16_t VIDEO_T_STATES_PER_FRAME = 29498; // Total T-states per 60Hz frame

class Bus {
public:
    Bus();
    explicit Bus(bool flat_memory);  // Flat 64KB mode for CP/M tests
    ~Bus();

    // Z80 Interface (called from CPU)
    uint8_t read(uint16_t addr, bool is_m1 = false);
    void write(uint16_t addr, uint8_t val);
    void add_ticks(int t);

    // System Interface (called from Main Loop)
    void reset();
    void load_rom(const std::string& path, uint16_t offset = ROM_START);
    void trigger_interrupt();
    bool interrupt_pending() const { return int_pending || fdc_.intrq_pending(); }
    void clear_interrupt() { int_pending = false; }  // clears timer; FDC INTRQ clears on status read

    // Video Interface (called from Display)
    uint64_t get_global_t_states() const { return global_t_states; }
    uint16_t get_current_scanline() const { return current_scanline; }
    bool is_visible_scanline() const;
    uint8_t get_vram_byte(uint16_t vram_addr) const;
    void set_keyboard_matrix(uint8_t* km) { keyboard_matrix = km; }

    // Cassette / Sound Interface (Port 0xFF)
    uint8_t read_port(uint8_t port);
    void write_port(uint8_t port, uint8_t val);
    // Bit 1 of port 0xFF is the cassette data output line.
    // Games toggle this at audio frequencies to produce sound.
    bool get_sound_bit() const { return (cas_prev_port_val & 0x02) != 0; }

    // Disk Interface
    bool load_disk(int drive, const std::string& path);
    bool fdc_present() const { return fdc_.is_present(); }

    // Cassette File Operations
    bool load_cas_file(const std::string& path);
    bool save_cas_file(const std::string& path);
    void start_playback();
    void start_recording();
    void stop_cassette();
    CassetteState get_cassette_state() const { return cas_state; }
    void set_cas_filename(const std::string& name) { cas_filename = name; }
    const std::string& get_cas_filename() const { return cas_filename; }
    std::string get_cassette_status() const;
    bool is_recording_idle() const;   // True if recording but no activity for timeout
    bool is_playback_done() const;    // True if playback data exhausted

    // Cassette diagnostic accessors
    const std::vector<uint8_t>& get_cas_data() const { return cas_data; }
    uint64_t get_cas_playback_start() const { return cas_playback_start_t; }
    // Compute which byte/bit the FSK signal is currently at
    void get_cas_position(size_t& byte_idx, int& bit_idx, bool& expected_bit) const;
    // Realign CAS clock so current time sits at the start of the next byte
    void realign_cas_clock();

    // Side-effect-free memory read (for filename extraction)
    uint8_t peek(uint16_t addr) const;

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
    bool int_pending = false;           // Interrupt pending flag (cleared on delivery)
    bool int_for_latch = false;         // Disk-expansion latch bit (cleared by reading 0x37E0)
    bool iff_enabled = true;            // Interrupts enabled (simplified)

    // =========================================================================
    // CASSETTE STATE
    // =========================================================================
    CassetteState cas_state = CassetteState::IDLE;
    std::string cas_filename;               // Current filename (trimmed)

    // Playback (CLOAD)
    std::vector<uint8_t> cas_data;          // CAS file contents
    uint64_t cas_playback_start_t = 0;      // T-state when playback started

    // Recording (CSAVE)
    std::vector<uint8_t> cas_rec_data;      // Recorded bytes
    uint64_t cas_last_cycle_t = 0;          // T-state of last cycle-start edge
    int cas_rec_cycle_count = 0;            // Cycles since last bit decoded
    uint8_t cas_rec_byte = 0;              // Byte being assembled
    int cas_rec_bit_count = 0;             // Bits assembled (0-7)
    uint8_t cas_prev_port_val = 0;         // Previous port 0xFF value
    uint64_t cas_last_activity_t = 0;      // Last port write T-state

    // Diagnostic
    int cas_port_read_log_count = 0;       // Port read log limiter
    mutable size_t cas_last_logged_byte = SIZE_MAX; // Last byte position logged

    // Cassette signal helpers
    bool get_cassette_signal() const;       // Compute bit 7 for playback
    void on_cassette_write(uint8_t val);    // Handle port write for recording
    void on_cycle_start();                  // Cycle-start edge detected
    void record_bit(bool bit);             // Accumulate one decoded bit
    void flush_recording();                // Flush partial byte + save file

    // =========================================================================
    // VIDEO CONTENTION LOGIC
    // =========================================================================
    bool should_insert_wait_state(uint16_t addr, bool is_m1) const;
    void update_video_timing(int t_states);
    void check_video_contention();

    // =========================================================================
    // ROM SHADOW RAM (expansion interface RAM-over-ROM)
    // =========================================================================
    // On real hardware the expansion interface can remap the first 4KB of RAM
    // over the ROM, allowing LDOS to install its interrupt handler at 0x0038.
    // We implement this as a simple write-through shadow: any write to the ROM
    // area (0x0000-0x2FFF) is stored here, and reads prefer it over ROM.
    std::array<uint8_t, ROM_SIZE> rom_shadow_{};
    std::array<bool, ROM_SIZE>    rom_shadow_active_{};

    // =========================================================================
    // DISK CONTROLLER
    // =========================================================================
    FDC fdc_;

    // =========================================================================
    // FLAT MEMORY MODE (for CP/M test programs like ZEXALL)
    // =========================================================================
    bool flat_mode = false;
    std::array<uint8_t, 65536> flat_mem{};

public:
    // Direct access to flat memory (for loading .COM files)
    uint8_t* get_flat_memory() { return flat_mem.data(); }
    bool is_flat_mode() const { return flat_mode; }
};
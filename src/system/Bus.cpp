// src/system/Bus.cpp
#include "Bus.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <filesystem>

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
    rom_shadow_.fill(0x00);
    rom_shadow_active_.fill(false);
    global_t_states = 0;
    current_scanline = 0;
    t_states_in_scanline = 0;
    int_pending = false;
    int_for_latch = false;
    iff_enabled = true;
    cas_state = CassetteState::IDLE;
    cas_data.clear();
    cas_rec_data.clear();
    cas_prev_port_val = 0;
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
        // ROM access — prefer shadow RAM if LDOS has written here
        value = rom_shadow_active_[addr] ? rom_shadow_[addr] : rom[addr];
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
    } else if (addr >= 0x37E0 && addr <= 0x37EF) {
        // Disk controller registers (expansion interface, memory-mapped)
        if (addr <= 0x37E3) {
            // IRQ status register: bit7=60Hz timer tick, bit6=FDC INTRQ.
            // int_for_latch is set with int_pending but is NOT auto-cleared on
            // interrupt delivery — it persists until software reads this register.
            // LDOS's ISR reads 0x37E0 to determine which source fired (timer vs FDC).
            value = (int_for_latch ? 0x80 : 0x00) | (fdc_.intrq_pending() ? 0x40 : 0x00);
            int_pending   = false;   // also clear delivery flag
            int_for_latch = false;   // reading 0x37E0 clears the timer latch
        } else if (addr <= 0x37E7) {
            // 0x37E4-0x37E7: cassette select / misc control — open bus
            value = 0xFF;
        } else if (addr <= 0x37EB) {
            // 0x37E8-0x37EB: Centronics parallel printer port status register.
            // Bits 7-4 when no printer connected (Centronics lines at idle/open):
            //   bit7=0 (/BUSY=low, printer not busy)
            //   bit6=0 (PE=low, no paper-end)
            //   bit5=1 (SELECT pulled high)
            //   bit4=1 (/ERROR pulled high → logic 1 = no fault)
            // = 0x30 upper nibble.  The Level II ROM disk/printer routine at 0x05D1
            // polls this in a loop until (status & 0xF0) == 0x30 before sending
            // a character to the printer.  Returning 0x30 means "ready / no printer."
            value = 0x30;
        } else {
            value = fdc_.read(addr);   // 0x37EC-0x37EF: FDC registers
        }
    } else {
        // Unmapped memory (0x3000-0x37DF)
        value = 0xFF;
    }

    // NOTE: Do NOT increment global_t_states here.
    // add_ticks() after cpu.step() already accounts for all instruction cycles.
    // Double-counting breaks cassette FSK timing (signal advances too fast,
    // causing the ROM bit-read routine to misread 1-bits as 0-bits).

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

    if (addr <= ROM_END) {
        // ROM-range write: shadow with RAM (expansion interface RAM-over-ROM).
        // LDOS installs its interrupt handler at 0x0038 this way.
        rom_shadow_[addr]        = val;
        rom_shadow_active_[addr] = true;
        return;
    } else if (addr >= 0x37E0 && addr <= 0x37EF) {
        // Disk controller registers (expansion interface)
        fdc_.write(addr, val);
    } else if (addr >= VRAM_START && addr <= VRAM_END) {
        // Video RAM writes (0x3C00-0x3FFF)
        vram[addr - VRAM_START] = val;
    } else if (addr >= RAM_START && addr <= RAM_END) {
        // User RAM writes (0x4000-0xFFFF)
        ram[addr - RAM_START] = val;
    }
    // ROM and keyboard are read-only, writes ignored
}

bool Bus::load_disk(int drive, const std::string& path) {
    return fdc_.load_disk(drive, path);
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
                int_pending   = true;
                int_for_latch = true;   // Disk-expansion latch: visible via 0x37E0 bit 7
            }
        }
    }
}

// ============================================================================
// VIDEO CONTENTION LOGIC (TRS-80 Model I)
// ============================================================================
// On real hardware, contention only occurs when the CPU accesses video RAM
// (0x3C00-0x3FFF) while the video controller is also reading it for display.
// ROM (0x0000-0x2FFF) and regular RAM (0x4000+) are NEVER contended.
// Incorrectly applying contention to ROM addresses destroys cassette FSK
// timing — the random +2T penalties on CTBIT delay loops cause every byte
// to be misread.
// ============================================================================
bool Bus::should_insert_wait_state(uint16_t addr, bool is_m1) const {
    // Contention only happens during M1 (opcode fetch) cycles
    if (!is_m1) {
        return false;
    }

    // Contention ONLY applies to video RAM addresses (0x3C00-0x3FFF)
    if (addr < VRAM_START || addr > VRAM_END) {
        return false;
    }

    // Contention only happens during visible scanlines
    if (!is_visible_scanline()) {
        return false;
    }

    // Contention happens during specific T-states in the scanline
    uint16_t t_in_line = t_states_in_scanline % VIDEO_T_STATES_PER_SCANLINE;

    // Video contention window (approximate)
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
    if (port == 0xFF) {
        uint8_t val = cas_prev_port_val & 0x7F;  // Echo current output bits
        // Bit 7: cassette data input (FSK signal during playback)
        bool sig = get_cassette_signal();
        if (sig) {
            val |= 0x80;
        }
        return val;
    }
    return 0xFF;  // Unmapped ports
}

void Bus::write_port(uint8_t port, uint8_t val) {
    if (port == 0xFF) {
        on_cassette_write(val);
        cas_prev_port_val = val;
    }
}

// ============================================================================
// SIDE-EFFECT-FREE MEMORY READ (for PC watch / filename extraction)
// ============================================================================
uint8_t Bus::peek(uint16_t addr) const {
    if (flat_mode) return flat_mem[addr];
    if (addr <= ROM_END)
        return rom_shadow_active_[addr] ? rom_shadow_[addr] : rom[addr];
    if (addr >= KEYBOARD_START && addr <= KEYBOARD_END) return 0x00;
    if (addr >= VRAM_START && addr <= VRAM_END) return vram[addr - VRAM_START];
    if (addr >= RAM_START) return ram[addr - RAM_START];
    return 0xFF;
}

// ============================================================================
// CASSETTE FSK SIGNAL GENERATION (Playback → Port 0xFF Bit 7)
// ============================================================================
// Generates a square wave encoding each bit in the CAS data:
//   bit=0: one cycle (half-period = 1774 T-states)
//   bit=1: two cycles (half-period = 887 T-states)
// The ROM's bit-read routine detects a rising edge, delays ~2476 T-states,
// then samples bit 7. With these timings:
//   bit=0 → sample falls in LOW phase → reads 0
//   bit=1 → sample falls in 2nd HIGH phase → reads 1
// ============================================================================
bool Bus::get_cassette_signal() const {
    if (cas_state != CassetteState::PLAYING || cas_data.empty()) {
        // When no cassette is playing, generate a toggling signal.
        // This prevents the ROM's CTBIT wait-for-HIGH loop (0x0241-0x0244)
        // from hanging forever. The toggling causes the ROM to eventually
        // detect bad data and return with an error/timeout.
        return (global_t_states / 1000) % 2 == 0;
    }

    uint64_t elapsed = global_t_states - cas_playback_start_t;

    // Lead-in: one half-period of LOW before first data bit.
    // The ROM's edge detector (wait-for-HIGH loop at 0x0243) would
    // otherwise catch the signal already HIGH at elapsed=0, causing
    // a false lock and a persistent 1-bit shift in all data reads.
    if (elapsed < CAS_HALF_0) {
        return false;  // LOW during lead-in
    }
    uint64_t data_elapsed = elapsed - CAS_HALF_0;

    uint64_t t_per_byte = CAS_BIT_PERIOD * 8;

    size_t byte_idx = data_elapsed / t_per_byte;

    // Use actual data, or pad with 0x00 after end (keeps ROM edge-detector alive)
    uint8_t current_byte;
    if (byte_idx < cas_data.size()) {
        current_byte = cas_data[byte_idx];
    } else {
        current_byte = 0x00;  // Pad with zeros so ROM can still detect edges
    }

    uint64_t byte_offset = data_elapsed % t_per_byte;
    int bit_idx = static_cast<int>(byte_offset / CAS_BIT_PERIOD);  // 0-7
    uint64_t bit_offset = byte_offset % CAS_BIT_PERIOD;

    bool bit_val = (current_byte >> (7 - bit_idx)) & 1;
    uint64_t half_period = bit_val ? CAS_HALF_1 : CAS_HALF_0;
    uint64_t phase = bit_offset / half_period;

    return (phase % 2 == 0);  // HIGH on even phases, LOW on odd
}

void Bus::get_cas_position(size_t& byte_idx, int& bit_idx, bool& expected_bit) const {
    if (cas_state != CassetteState::PLAYING || cas_data.empty()) {
        byte_idx = 0; bit_idx = 0; expected_bit = false;
        return;
    }
    uint64_t elapsed = global_t_states - cas_playback_start_t;
    // Account for lead-in period
    if (elapsed < CAS_HALF_0) {
        byte_idx = 0; bit_idx = 0; expected_bit = false;
        return;
    }
    uint64_t data_elapsed = elapsed - CAS_HALF_0;
    uint64_t t_per_byte = CAS_BIT_PERIOD * 8;
    byte_idx = data_elapsed / t_per_byte;
    uint64_t byte_offset = data_elapsed % t_per_byte;
    bit_idx = static_cast<int>(byte_offset / CAS_BIT_PERIOD);
    uint8_t current_byte = (byte_idx < cas_data.size()) ? cas_data[byte_idx] : 0x00;
    expected_bit = (current_byte >> (7 - bit_idx)) & 1;
}

void Bus::realign_cas_clock() {
    if (cas_state != CassetteState::PLAYING || cas_data.empty()) return;
    uint64_t elapsed = global_t_states - cas_playback_start_t;
    if (elapsed < CAS_HALF_0) return;  // Still in lead-in
    uint64_t data_elapsed = elapsed - CAS_HALF_0;
    uint64_t t_per_byte = CAS_BIT_PERIOD * 8;
    size_t byte_idx = data_elapsed / t_per_byte;
    uint64_t byte_offset = data_elapsed % t_per_byte;

    // If we're not at a byte boundary, snap back to the start of the current byte
    if (byte_offset > 0) {
        uint64_t target_data_elapsed = byte_idx * t_per_byte;
        // Shift playback start so that "now" corresponds to the current byte boundary
        cas_playback_start_t = global_t_states - target_data_elapsed - CAS_HALF_0;
        std::cerr << "[CAS] Realigned clock: byte " << byte_idx << std::endl;
    }
}

// ============================================================================
// CASSETTE RECORDING (CSAVE → Decode FSK from Port Writes)
// ============================================================================
// Tracks rising edges on bit 0 of port 0xFF output. Measures intervals
// between consecutive cycle starts to determine bit values:
//   Short interval (<2600T) after a clock → second cycle → bit=1
//   Long interval (>2600T) → single cycle → bit=0
// ============================================================================
void Bus::on_cassette_write(uint8_t val) {
    if (cas_state != CassetteState::RECORDING) return;

    uint8_t new_bits = val & 0x03;
    uint8_t old_bits = cas_prev_port_val & 0x03;

    cas_last_activity_t = global_t_states;

    // Detect rising edge on bit 0 (neutral/negative → positive)
    if ((new_bits & 0x01) && !(old_bits & 0x01)) {
        on_cycle_start();
    }
}

void Bus::on_cycle_start() {
    uint64_t now = global_t_states;

    if (cas_last_cycle_t == 0) {
        // First cycle ever — just start counting
        cas_last_cycle_t = now;
        cas_rec_cycle_count = 1;
        return;
    }

    uint64_t interval = now - cas_last_cycle_t;
    cas_last_cycle_t = now;

    if (interval > CAS_IDLE_TIMEOUT) {
        // Very long gap — reset (new block or leader restart)
        cas_rec_cycle_count = 1;
        return;
    }

    if (interval > CAS_CYCLE_THRESH) {
        // LONG interval: previous bit had only one cycle → bit=0
        if (cas_rec_cycle_count == 1) {
            record_bit(false);
        }
        cas_rec_cycle_count = 1;
    } else {
        // SHORT interval
        cas_rec_cycle_count++;
        if (cas_rec_cycle_count == 2) {
            // Two cycles close together → bit=1
            record_bit(true);
            cas_rec_cycle_count = 0;
        }
    }
}

void Bus::record_bit(bool bit) {
    cas_rec_byte = (cas_rec_byte << 1) | (bit ? 1 : 0);
    cas_rec_bit_count++;
    if (cas_rec_bit_count == 8) {
        cas_rec_data.push_back(cas_rec_byte);
        cas_rec_byte = 0;
        cas_rec_bit_count = 0;
        if (cas_rec_data.size() % 512 == 0) {
            fprintf(stderr, "[CSAVE] Progress: %zu bytes recorded\n", cas_rec_data.size());
        }
    }
}

// ============================================================================
// CASSETTE FILE I/O
// ============================================================================
bool Bus::load_cas_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cassette: Cannot open " << path << std::endl;
        return false;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    cas_data.resize(size);
    file.read(reinterpret_cast<char*>(cas_data.data()), size);

    std::cout << "Cassette: Loaded " << path << " (" << size << " bytes)" << std::endl;
    return true;
}

bool Bus::save_cas_file(const std::string& path) {
    // Ensure the directory exists
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cassette: Cannot write " << path << std::endl;
        return false;
    }
    file.write(reinterpret_cast<const char*>(cas_rec_data.data()), cas_rec_data.size());
    std::cout << "Cassette: Saved " << path << " (" << cas_rec_data.size() << " bytes)" << std::endl;
    return true;
}

void Bus::start_playback() {
    if (cas_data.empty()) {
        std::cerr << "Cassette: No data loaded for playback" << std::endl;
        return;
    }
    cas_state = CassetteState::PLAYING;
    cas_playback_start_t = global_t_states;
    cas_port_read_log_count = 0;
    cas_last_logged_byte = SIZE_MAX;
}

void Bus::start_recording() {
    cas_state = CassetteState::RECORDING;
    cas_rec_data.clear();
    cas_rec_byte = 0;
    cas_rec_bit_count = 0;
    cas_rec_cycle_count = 0;
    cas_last_cycle_t = 0;
    cas_last_activity_t = global_t_states;
}

void Bus::stop_cassette() {
    if (cas_state == CassetteState::RECORDING) {
        flush_recording();
    }
    cas_state = CassetteState::IDLE;
}

void Bus::flush_recording() {
    // Flush the last pending bit (if single cycle was the last thing)
    if (cas_rec_cycle_count == 1) {
        record_bit(false);
    }
    // Flush any partial byte
    if (cas_rec_bit_count > 0) {
        cas_rec_byte <<= (8 - cas_rec_bit_count);
        cas_rec_data.push_back(cas_rec_byte);
        cas_rec_bit_count = 0;
    }
    if (!cas_filename.empty() && !cas_rec_data.empty()) {
        save_cas_file("software/" + cas_filename + ".cas");
    }
    std::cout << "Cassette: Recording stopped (" << cas_rec_data.size() << " bytes)" << std::endl;
}

std::string Bus::get_cassette_status() const {
    switch (cas_state) {
        case CassetteState::PLAYING:   return "PLAY: " + cas_filename;
        case CassetteState::RECORDING: return "REC: " + cas_filename;
        default:                       return "";
    }
}

bool Bus::is_recording_idle() const {
    return cas_state == CassetteState::RECORDING &&
           global_t_states - cas_last_activity_t > CAS_IDLE_TIMEOUT;
}

bool Bus::is_playback_done() const {
    if (cas_state != CassetteState::PLAYING || cas_data.empty()) return false;
    uint64_t elapsed = global_t_states - cas_playback_start_t;
    // Allow 500 extra zero-byte padding after data ends for ROM to finish
    uint64_t total = static_cast<uint64_t>(cas_data.size() + 500) * CAS_BIT_PERIOD * 8;
    return elapsed >= total;
}

// ============================================================================
// INTERRUPT HANDLING
// ============================================================================
void Bus::trigger_interrupt() {
    if (iff_enabled) {
        int_pending   = true;
        int_for_latch = true;
    }
}
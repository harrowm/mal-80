// src/main.cpp
#include <iostream>
#include <signal.h>
#include <execinfo.h>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <algorithm>
#include <SDL.h>
#include "cpu/z80.hpp"
#include "system/Bus.hpp"
#include "video/Display.hpp"

// ============================================================================
// ROM entry points for cassette operations (Level II BASIC)
// ============================================================================
constexpr uint16_t ROM_SYNC_SEARCH   = 0x0293;  // CLOAD sync search entry
constexpr uint16_t ROM_WRITE_LEADER  = 0x0284;  // CSAVE write leader+sync entry
constexpr uint16_t ROM_FILENAME_PTR  = 0x40A7;  // 2-byte pointer to 6-char filename

// Extract the BASIC filename from RAM (6 chars, space-padded, trimmed)
static std::string extract_filename(const Bus& bus) {
    uint16_t ptr = bus.peek(ROM_FILENAME_PTR) | (bus.peek(ROM_FILENAME_PTR + 1) << 8);
    // Skip leading quote if present
    if (bus.peek(ptr) == '"') ptr++;
    std::string result;
    for (int i = 0; i < 6; i++) {
        uint8_t ch = bus.peek(ptr + i);
        if (ch == 0x00 || ch == '"') break;
        if (ch < 0x20 || ch > 0x7E) break;
        result += static_cast<char>(ch);
    }
    // Trim trailing spaces
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

// Find a CAS file: try lowercase.cas then UPPERCASE.CAS directly.
// If filename is empty (bare CLOAD), pick the first .cas alphabetically.
static std::string find_cas_file(const std::string& filename) {
    namespace fs = std::filesystem;
    std::cout << "[CLOAD] Searching for filename: '" << filename << "'" << std::endl;
    if (!fs::exists("software")) return "";

    std::vector<std::string> cas_files;
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (auto& e : fs::directory_iterator("software")) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".cas") continue;
        std::string fname = e.path().stem().string();
        std::string fname_lower = fname;
        std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);
        if (lower.empty() || fname_lower.find(lower) == 0) {
            cas_files.push_back(e.path().string());
        }
    }
    if (cas_files.empty()) {
        std::cout << "[CLOAD] No matching .cas file found for: '" << filename << "'" << std::endl;
        return "";
    }
    std::sort(cas_files.begin(), cas_files.end());
    std::cout << "[CLOAD] Partial match, picking: '" << cas_files.front() << "'" << std::endl;
    return cas_files.front();
}

static void crash_handler(int sig) {
    void* bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(1);
}

int main(int /*argc*/, char* /*argv*/[]) {
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGABRT, crash_handler);
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║         Welcome to Mal-80              ║" << std::endl;
    std::cout << "║      TRS-80 Model I Emulator           ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;

    Bus bus;
    Z80 cpu(bus);
    Display display;

    // Initialize display
    if (!display.init("Mal-80 - TRS-80 Emulator")) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }

    // Load ROM
    try {
        bus.load_rom("roms/level2.rom");
    } catch (const std::exception& e) {
        std::cerr << "ROM Load Failed: " << e.what() << std::endl;
        std::cerr << "Place your TRS-80 ROM in roms/level2.rom" << std::endl;
    }

    cpu.reset();

    // Keyboard matrix (8×8)
    uint8_t keyboard_matrix[8];
    std::memset(keyboard_matrix, 0x00, sizeof(keyboard_matrix));
    bus.set_keyboard_matrix(keyboard_matrix);

    // Emulation loop
    uint64_t frame_t_states = 0;
    constexpr uint64_t T_STATES_PER_FRAME = 29498;  // 60Hz
    CassetteState prev_cas_state = CassetteState::IDLE;

    // CLOAD state tracking
    bool cload_active = false;
    bool cload_realigned = false;
    int cload_byte_count = 0;
    size_t cload_sync_pos = 0;

    while (display.is_running()) {
        // Handle input
        display.handle_events(keyboard_matrix);

        // Run CPU for one video frame
        frame_t_states = 0;
        while (frame_t_states < T_STATES_PER_FRAME) {
            // --- PC Watch: intercept CLOAD/CSAVE entry points ---
            uint16_t pc = cpu.get_pc();

            if (pc == ROM_SYNC_SEARCH && bus.get_cassette_state() == CassetteState::IDLE) {
                std::string fname = extract_filename(bus);
                std::string path = find_cas_file(fname);
                if (!path.empty() && bus.load_cas_file(path)) {
                    bus.set_cas_filename(fname.empty() ? "(auto)" : fname);
                    bus.start_playback();

                    const auto& cas = bus.get_cas_data();
                    cload_active = true;
                    cload_realigned = false;
                    cload_byte_count = 0;
                    cload_sync_pos = 0;
                    for (size_t i = 0; i < cas.size(); i++) {
                        if (cas[i] == 0xA5) { cload_sync_pos = i; break; }
                    }
                    size_t data_bytes = cas.size() - cload_sync_pos - 1;
                    std::cout << "CLOAD: " << path << " (" << data_bytes << " bytes)" << std::endl;
                } else {
                    std::cout << "CLOAD: no .cas file found" << std::endl;
                }
            }

            // Realign CAS clock on first CASIN call after CLOAD starts
            if (cload_active && bus.get_cassette_state() == CassetteState::PLAYING) {
                if (pc == 0x0235 && !cload_realigned) {
                    bus.realign_cas_clock();
                    cload_realigned = true;
                }

                // 0x0240 = RET from CASIN: a full byte has been read
                if (pc == 0x0240) {
                    uint8_t actual = cpu.get_a();
                    size_t exp_idx = cload_sync_pos + 1 + cload_byte_count;
                    const auto& cas = bus.get_cas_data();
                    uint8_t expected = (exp_idx < cas.size()) ? cas[exp_idx] : 0xFF;
                    size_t total = cas.size() - cload_sync_pos - 1;

                    if (actual != expected) {
                        fprintf(stderr, "[CLOAD] MISMATCH byte %d/%zu: got 0x%02X expected 0x%02X\n",
                                cload_byte_count, total, actual, expected);
                    }
                    if (cload_byte_count % 512 == 0) {
                        fprintf(stderr, "[CLOAD] Progress: %d / %zu bytes (%.0f%%)\n",
                                cload_byte_count, total,
                                100.0 * cload_byte_count / total);
                    }
                    cload_byte_count++;
                }
            }
            // Turn off tracking when playback stops
            if (cload_active && bus.get_cassette_state() == CassetteState::IDLE) {
                fprintf(stderr, "[CLOAD] Complete: %d bytes read\n", cload_byte_count);
                cload_active = false;
            }

            if (pc == ROM_WRITE_LEADER && bus.get_cassette_state() == CassetteState::IDLE) {
                std::string fname = extract_filename(bus);
                bus.set_cas_filename(fname);
                bus.start_recording();
                std::cout << "CSAVE: recording" << (fname.empty() ? "" : " \"" + fname + "\"") << std::endl;
            }

            int ticks = cpu.step();
            bus.add_ticks(ticks);
            frame_t_states += ticks;

            // Check for interrupts
            if (bus.interrupt_pending()) {
                bus.clear_interrupt();
                // TODO: Trigger Z80 INT (requires CPU interrupt support)
            }

            // Auto-stop recording after idle timeout
            if (bus.is_recording_idle()) {
                bus.stop_cassette();
            }

            // Auto-stop playback when data exhausted
            if (bus.is_playback_done()) {
                bus.stop_cassette();
            }
        }

        // Update title bar with cassette status
        CassetteState cur_cas_state = bus.get_cassette_state();
        if (cur_cas_state != prev_cas_state) {
            std::string status = bus.get_cassette_status();
            if (status.empty()) {
                display.set_title("Mal-80 - TRS-80 Emulator");
            } else {
                display.set_title("Mal-80 - " + status);
            }
            prev_cas_state = cur_cas_state;
        }

        // Render frame
        display.render_frame(bus);


    }

    display.cleanup();
    std::cout << "Mal-80 shutdown complete." << std::endl;
    return 0;
}
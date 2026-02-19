
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <signal.h>
#include <execinfo.h>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <algorithm>
#include <queue>
#include <cctype>
#include <chrono>
#include <array>
#include <SDL.h>
#include "cpu/z80.hpp"
#include "system/Bus.hpp"
#include "video/Display.hpp"

// LOPHD (0x02CE): turns on cassette motor for SYSTEM load — intercept here
// before it calls CSRDON (0x0293), so CLOAD intercept never fires for SYSTEM.
constexpr uint16_t ROM_SYSTEM_ENTRY   = 0x02CE;

// Parse a TRS-80 SYSTEM (machine language) .cas file and load into RAM.
//
// CAS format for SYSTEM files:
//   [256 × 0x00 leader] [0xA5 sync] [0x55 type] [6-byte name]
//   repeated: [0x3C] [count: 0=256] [load_lo] [load_hi] [data...] [checksum]
//   checksum = (load_hi + load_lo + sum(data_bytes)) mod 256
//   EOF:       [0x78] [exec_lo] [exec_hi]
//
// Returns true on success (PC set to exec address), false on error.
static bool load_system_cas(const std::string& path, Bus& bus, Z80& cpu) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[SYSTEM] Failed to open: " << path << std::endl;
        return false;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    size_t i = 0;
    // Skip leader (0x00 bytes)
    while (i < buf.size() && buf[i] == 0x00) i++;

    if (i >= buf.size() || buf[i] != 0xA5) {
        std::cerr << "[SYSTEM] No sync byte (0xA5) in: " << path << std::endl;
        return false;
    }
    i++;  // sync

    if (i >= buf.size() || buf[i] != 0x55) {
        std::cerr << "[SYSTEM] Not a SYSTEM file (expected 0x55, got 0x"
                  << std::hex << (int)buf[i] << ") in: " << path << std::endl;
        return false;
    }
    i++;  // type byte

    if (i + 6 > buf.size()) {
        std::cerr << "[SYSTEM] Truncated filename in: " << path << std::endl;
        return false;
    }
    std::string cas_name(buf.begin() + i, buf.begin() + i + 6);
    i += 6;

    uint16_t exec_addr = 0;
    bool found_exec = false;
    int blocks_loaded = 0;

    while (i < buf.size()) {
        uint8_t marker = buf[i++];

        if (marker == 0x3C) {
            // Data block: [count] [load_lo] [load_hi] [data×count] [checksum]
            if (i + 3 > buf.size()) {
                std::cerr << "[SYSTEM] Truncated data block header" << std::endl;
                return false;
            }
            uint8_t  count       = buf[i++];
            uint8_t  load_lo     = buf[i++];
            uint8_t  load_hi     = buf[i++];
            uint16_t load_addr   = load_lo | (load_hi << 8);
            size_t   actual_count = (count == 0) ? 256 : count;

            if (i + actual_count + 1 > buf.size()) {
                std::cerr << "[SYSTEM] Truncated data block data" << std::endl;
                return false;
            }

            // Verify checksum before writing
            uint8_t computed = (load_hi + load_lo) & 0xFF;
            for (size_t j = 0; j < actual_count; j++)
                computed = (computed + buf[i + j]) & 0xFF;
            uint8_t stored = buf[i + actual_count];
            if (computed != stored) {
                std::cerr << "[SYSTEM] Checksum error block at 0x" << std::hex
                          << load_addr << ": computed 0x" << (int)computed
                          << " stored 0x" << (int)stored << std::endl;
                // continue anyway — same as real ROM behaviour
            }

            for (size_t j = 0; j < actual_count; j++)
                bus.write(static_cast<uint16_t>(load_addr + j), buf[i + j]);

            i += actual_count + 1;  // data + checksum
            blocks_loaded++;

        } else if (marker == 0x78) {
            // EOF block: [exec_lo] [exec_hi]
            if (i + 2 > buf.size()) {
                std::cerr << "[SYSTEM] Truncated EOF block" << std::endl;
                return false;
            }
            exec_addr = buf[i] | (buf[i + 1] << 8);
            i += 2;
            found_exec = true;
            break;

        } else {
            std::cerr << "[SYSTEM] Unknown block marker 0x" << std::hex
                      << (int)marker << " at offset " << std::dec << (i - 1) << std::endl;
            return false;
        }
    }

    if (!found_exec) {
        std::cerr << "[SYSTEM] No EOF block (0x78) found in: " << path << std::endl;
        return false;
    }

    cpu.set_pc(exec_addr);
    std::cout << "[SYSTEM] Loaded '" << cas_name << "' from " << path
              << " (" << blocks_loaded << " blocks), exec 0x"
              << std::hex << exec_addr << std::endl;
    return true;
}

// Peek at a .cas file's type byte (after leader + 0xA5 sync).
// Returns true if it's a SYSTEM (machine language) file (type byte == 0x55).
static bool is_system_cas(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    size_t i = 0;
    while (i < buf.size() && buf[i] == 0x00) i++;  // skip leader
    if (i >= buf.size() || buf[i] != 0xA5) return false;
    i++;  // sync
    return i < buf.size() && buf[i] == 0x55;
}

// ============================================================================
// ROM entry points for cassette operations (Level II BASIC)
// ============================================================================
constexpr uint16_t ROM_SYNC_SEARCH   = 0x0293;  // CLOAD sync search entry
constexpr uint16_t ROM_WRITE_LEADER  = 0x0284;  // CSAVE write leader+sync entry
constexpr uint16_t ROM_KEY           = 0x0049;  // $KEY: wait-for-keypress, returns ASCII in A
constexpr uint16_t ROM_BASIC_READY   = 0x1A19;  // BASIC warm restart (prints READY, loops)
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

// Find a CAS file matching filename prefix (case-insensitive).
// If filename is empty (bare CLOAD), pick the first .cas alphabetically.
static std::string find_cas_file(const std::string& filename, const char* tag = "CLOAD") {
    namespace fs = std::filesystem;
    std::cout << "[" << tag << "] Searching for filename: '" << filename << "'" << std::endl;
    if (!fs::exists("software")) return "";

    std::vector<std::string> cas_files;
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (auto& e : fs::directory_iterator("software")) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".cas" && ext != ".bas") continue;
        std::string fname = e.path().stem().string();
        std::string fname_lower = fname;
        std::transform(fname_lower.begin(), fname_lower.end(), fname_lower.begin(), ::tolower);
        if (lower.empty() || fname_lower.find(lower) == 0) {
            cas_files.push_back(e.path().string());
        }
    }
    if (cas_files.empty()) {
        std::cout << "[" << tag << "] No matching .cas file found for: '" << filename << "'" << std::endl;
        return "";
    }
    std::sort(cas_files.begin(), cas_files.end());
    std::cout << "[" << tag << "] Picking: '" << cas_files.front() << "'" << std::endl;
    return cas_files.front();
}

// Enqueue characters from a string into the type queue.
// Uppercases a-z (TRS-80 has no lowercase keys), maps \n -> 0x0D (Enter),
// skips \r (handles Windows CRLF files), drops other non-printable chars.
static void enqueue_text(std::queue<uint8_t>& q, const std::string& text) {
    for (unsigned char c : text) {
        if (c >= 'a' && c <= 'z')  q.push(c - 32);   // uppercase
        else if (c == '\n')         q.push(0x0D);      // Enter
        else if (c == '\r')         continue;           // strip CR
        else if (c >= 0x20)         q.push(c);          // printable ASCII
    }
}

// Load a plain-text BASIC file into the type queue.
// Prepends "NEW\n" to clear any existing program first.
static void load_bas_file(const std::string& path, std::queue<uint8_t>& q) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[BAS] Failed to open: " << path << std::endl;
        return;
    }
    enqueue_text(q, "NEW\n");
    std::string line;
    int lines = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // strip CR
        if (!line.empty()) {
            enqueue_text(q, line + "\n");
            lines++;
        }
    }
    std::cout << "[BAS] Queued " << lines << " lines ("
              << q.size() << " chars) from " << path << std::endl;
}

// ============================================================================
// Circular trace buffer — records the last N instructions before a freeze
// ============================================================================
constexpr size_t TRACE_BUF_SIZE = 500;

struct TraceEntry {
    uint16_t pc, sp;
    uint8_t  a, f, b, c, d, e, h, l;
    uint16_t ix, iy;
    uint8_t  i_reg, im;
    bool     iff1, iff2, halted;
    uint64_t ticks;  // cumulative T-states at this instruction
};

static void dump_trace(const std::array<TraceEntry, TRACE_BUF_SIZE>& buf,
                       size_t head, size_t count, const Bus& bus) {
    std::ofstream out("trace.log", std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[TRACE] Could not open trace.log\n";
        return;
    }
    out << "# Mal-80 freeze trace — last " << count << " instructions\n";
    out << "# TICKS       PC   SP   AF   BC   DE   HL   IX   IY  I IM IFF OP\n";

    size_t start = (count < TRACE_BUF_SIZE) ? 0 : head;
    for (size_t n = 0; n < count; n++) {
        const TraceEntry& e = buf[(start + n) % TRACE_BUF_SIZE];
        uint8_t op0 = bus.peek(e.pc);
        uint8_t op1 = bus.peek(e.pc + 1);
        char line[128];
        snprintf(line, sizeof(line),
            "%12llu  %04X %04X  %02X%02X %04X %04X %04X  %04X %04X  %02X %d %d%d  %02X %02X%s%s\n",
            (unsigned long long)e.ticks,
            e.pc, e.sp,
            e.a, e.f, (e.b << 8) | e.c, (e.d << 8) | e.e, (e.h << 8) | e.l,
            e.ix, e.iy,
            e.i_reg, e.im, (int)e.iff1, (int)e.iff2,
            op0, op1,
            e.halted ? " HALT" : "",
            e.iff1   ? "" : " DI");
        out << line;
    }
    out.close();
    std::cerr << "[TRACE] Dumped " << count << " instructions to trace.log\n";
}

static void crash_handler(int sig) {
    void* bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(1);
}

enum class SpeedMode { NORMAL, TURBO };

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGABRT, crash_handler);
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║         Welcome to Mal-80              ║" << std::endl;
    std::cout << "║      TRS-80 Model I Emulator           ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;

    // Parse command-line arguments
    std::string cli_load_name;  // value of --load <name>, empty if not given
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc) {
            cli_load_name = argv[++i];
        }
    }

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
    constexpr uint64_t TURBO_T_STATES     = T_STATES_PER_FRAME * 100;
    constexpr int      TURBO_RENDER_EVERY = 10;
    constexpr int      NORMAL_FRAME_US    = 16667;   // ~60Hz in microseconds

    SpeedMode user_speed = SpeedMode::NORMAL;  // future control-panel hook
    SpeedMode cur_speed  = SpeedMode::NORMAL;
    int  turbo_render_count = 0;
    auto frame_start = std::chrono::steady_clock::now();

    CassetteState prev_cas_state = CassetteState::IDLE;
    SpeedMode     prev_speed     = SpeedMode::NORMAL;

    // CLOAD state tracking
    bool cload_active = false;
    bool cload_realigned = false;
    int cload_byte_count = 0;
    size_t cload_sync_pos = 0;


    // SYSTEM state: set when 0x02CE fires, cleared on success or when CLOAD
    // intercept sees it (to suppress CLOAD from also firing for the same file).
    bool system_active = false;

    // Keyboard injection queue: drained one char at a time via $KEY intercept.
    // Used to type BASIC programs loaded from .bas text files.
    std::queue<uint8_t> type_queue;

    // --load autoload state
    std::string cli_autoload_path;  // non-empty: CSRDON intercept uses this path
    bool cli_autorun = false;       // true: queue "RUN\n" when cassette load finishes

    if (!cli_load_name.empty()) {
        std::string path = find_cas_file(cli_load_name, "LOAD");
        if (path.empty()) {
            std::cerr << "[LOAD] No file found matching: " << cli_load_name << std::endl;
        } else {
            namespace fs = std::filesystem;
            std::string ext = fs::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".cas") {
                if (is_system_cas(path)) {
                    // Machine-language file: type SYSTEM then filename separately.
                    // TRS-80 SYSTEM command is interactive: "SYSTEM\n" triggers the
                    // loader which prints "*?" and reads the filename via $KEY.
                    // Leading \n answers the ROM cold-boot "MEMORY SIZE?" prompt first.
                    namespace fs = std::filesystem;
                    std::string stem = fs::path(path).stem().string();
                    enqueue_text(type_queue, "\nSYSTEM\n" + stem + "\n");
                } else {
                    // BASIC cassette: let CSRDON intercept handle playback, then autorun
                    cli_autoload_path = path;
                    enqueue_text(type_queue, "CLOAD\n");
                    cli_autorun = true;
                }
            } else if (ext == ".bas") {
                // Text BASIC file: inject keystrokes then RUN
                load_bas_file(path, type_queue);
                enqueue_text(type_queue, "RUN\n");
            }
        }
    }

    // Circular trace buffer
    std::array<TraceEntry, TRACE_BUF_SIZE> trace_buf{};
    size_t   trace_head  = 0;
    size_t   trace_count = 0;
    uint64_t total_ticks = 0;

    // Freeze detector: if all PCs in the last FREEZE_WINDOW steps fall within
    // a 64-byte range, count T-states spent there; dump after FREEZE_TICKS.
    constexpr size_t   FREEZE_WINDOW    = 64;
    constexpr uint64_t FREEZE_TICKS     = 3'000'000;  // ~1.7 seconds of game time
    uint64_t           freeze_ticks_acc = 0;
    bool               freeze_dumped    = false;
    // Rolling PC window for freeze detection
    std::array<uint16_t, FREEZE_WINDOW> pc_window{};
    size_t pc_win_pos = 0;
    bool   pc_win_full = false;

    while (display.is_running()) {
        // Handle input
        display.handle_events(keyboard_matrix);

        // Auto-select speed: turbo while keyboard injection is active
        SpeedMode desired = !type_queue.empty() ? SpeedMode::TURBO : user_speed;
        if (desired != cur_speed) {
            cur_speed = desired;
            turbo_render_count = 0;
            frame_start = std::chrono::steady_clock::now();
        }

        // Run CPU for one video frame worth of T-states (or 100× in turbo)
        uint64_t t_budget = (cur_speed == SpeedMode::TURBO) ? TURBO_T_STATES : T_STATES_PER_FRAME;
        frame_t_states = 0;
        while (frame_t_states < t_budget) {
            // --- PC Watch: intercept CLOAD/CSAVE entry points ---
            uint16_t pc = cpu.get_pc();


            // SYSTEM fast loader: intercept at LOPHD (0x02CE) before cassette
            // motor turns on, so CSRDON (0x0293) is never reached for SYSTEM.
            if (pc == ROM_SYSTEM_ENTRY) {
                system_active = true;  // suppress CLOAD intercept if load fails
                std::string fname = extract_filename(bus);
                std::string path = find_cas_file(fname, "SYSTEM");
                if (!path.empty()) {
                    if (load_system_cas(path, bus, cpu)) {
                        system_active = false;  // success — CLOAD won't fire
                    }
                    // on failure: leave system_active=true so CLOAD is skipped
                }
                // no file found: system_active stays true, CLOAD skipped
            }

            if (pc == ROM_SYNC_SEARCH && bus.get_cassette_state() == CassetteState::IDLE) {
                if (system_active) {
                    // CSRDON reached from SYSTEM path after a failed fast-load;
                    // skip CLOAD setup so we don't try to play a SYSTEM file as BASIC.
                    system_active = false;
                } else {
                    // Use CLI-supplied path if set, otherwise search by filename in RAM
                    std::string fname;
                    std::string path;
                    if (!cli_autoload_path.empty()) {
                        path = cli_autoload_path;
                        cli_autoload_path.clear();
                        std::cout << "[CLOAD] Using CLI autoload: " << path << std::endl;
                    } else {
                        fname = extract_filename(bus);
                        path = find_cas_file(fname);
                    }
                    if (path.empty()) {
                        std::cout << "CLOAD: no file found" << std::endl;
                    } else {
                        namespace fs = std::filesystem;
                        std::string ext = fs::path(path).extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        if (ext == ".bas") {
                            // Text BASIC file: queue keystrokes and return to READY
                            load_bas_file(path, type_queue);
                            cpu.set_pc(ROM_BASIC_READY);
                        } else {
                            // Binary .cas file: normal cassette FSK playback
                            if (bus.load_cas_file(path)) {
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
                            }
                        }
                    }
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
                if (cli_autorun) {
                    enqueue_text(type_queue, "RUN\n");
                    cli_autorun = false;
                }
            }

            if (pc == ROM_WRITE_LEADER && bus.get_cassette_state() == CassetteState::IDLE) {
                std::string fname = extract_filename(bus);
                bus.set_cas_filename(fname);
                bus.start_recording();
                std::cout << "CSAVE: recording" << (fname.empty() ? "" : " \"" + fname + "\"") << std::endl;
            }

            // $KEY intercept (0x0049): inject queued characters one at a time.
            // $KEY is the wait-for-keypress routine used by BASIC line input.
            // It is NOT called by INKEY$ (which uses $KBD/0x002B directly),
            // so this only fires during BASIC command/line input — safe for games.
            if (pc == ROM_KEY && !type_queue.empty()) {
                uint8_t ch = type_queue.front();
                type_queue.pop();
                // Fake a RET: pop the caller's return address and jump there
                uint16_t sp = cpu.get_sp();
                uint16_t ret_addr = bus.peek(sp) | (bus.peek(sp + 1) << 8);
                cpu.set_sp(sp + 2);
                cpu.set_pc(ret_addr);
                cpu.set_a(ch);
                // Don't call cpu.step() this iteration — we already changed PC
                bus.add_ticks(10);  // approximate cost of the intercepted call
                frame_t_states += 10;
                continue;
            }

            // Record this instruction in the circular trace buffer
            {
                TraceEntry& te = trace_buf[trace_head];
                te.pc = pc; te.sp = cpu.get_sp();
                te.a  = cpu.get_a();  te.f = cpu.get_f();
                te.b  = cpu.get_b();  te.c = cpu.get_c();
                te.d  = cpu.get_d();  te.e = cpu.get_e();
                te.h  = cpu.get_h();  te.l = cpu.get_l();
                te.ix = cpu.get_ix(); te.iy = cpu.get_iy();
                te.i_reg = cpu.get_i(); te.im = cpu.get_im();
                te.iff1 = cpu.get_iff1(); te.iff2 = cpu.get_iff2();
                te.halted = cpu.get_halted();
                te.ticks = total_ticks;
                trace_head = (trace_head + 1) % TRACE_BUF_SIZE;
                if (trace_count < TRACE_BUF_SIZE) trace_count++;
            }

            // Freeze detection: count how many consecutive steps landed on the
            // same PC.  A HALT loop or any single tight spin will trip this fast.
            // Also catches multi-instruction tight loops via the 64-byte window
            // check (kept as secondary test).
            if (!freeze_dumped) {
                // Fast path: same PC repeated
                static uint16_t last_freeze_pc = 0xFFFF;
                static uint64_t same_pc_streak  = 0;
                if (pc == last_freeze_pc) {
                    same_pc_streak++;
                } else {
                    last_freeze_pc = pc;
                    same_pc_streak = 0;
                }
                // Slow path: PC stays within 64-byte window
                pc_window[pc_win_pos] = pc;
                pc_win_pos = (pc_win_pos + 1) % FREEZE_WINDOW;
                if (!pc_win_full && pc_win_pos == 0) pc_win_full = true;

                bool tight = (same_pc_streak > 100'000);  // single-address loop
                if (!tight && pc_win_full) {
                    uint16_t lo = pc_window[0], hi = pc_window[0];
                    for (uint16_t p : pc_window) {
                        if (p < lo) lo = p;
                        if (p > hi) hi = p;
                    }
                    if (hi - lo < 64) {
                        freeze_ticks_acc += 4;
                    } else {
                        freeze_ticks_acc = 0;
                    }
                    tight = (freeze_ticks_acc >= FREEZE_TICKS);
                }

                if (tight) {
                    std::cerr << "[FREEZE] Detected at PC=0x"
                              << std::hex << pc << std::dec
                              << " streak=" << same_pc_streak
                              << " ticks=" << total_ticks << "\n";
                    dump_trace(trace_buf, trace_head, trace_count, bus);
                    freeze_dumped = true;
                }
            }

            int ticks = cpu.step();
            bus.add_ticks(ticks);
            frame_t_states += ticks;
            total_ticks += ticks;

            // Deliver maskable interrupt if pending and CPU has interrupts enabled.
            // TRS-80 Model 1 uses IM 1: RST 38h (push PC, jump to 0x0038).
            // Z80 INT acceptance: IFF2 ← IFF1 (saved for RETI/RETN), IFF1 ← 0.
            // CRITICAL: do NOT set IFF2=false — RETI does IFF1=IFF2, so if IFF2
            // is cleared here, RETI permanently disables interrupts.
            if (bus.interrupt_pending() && cpu.get_iff1()) {
                bus.clear_interrupt();
                cpu.set_iff2(cpu.get_iff1());  // save IFF1 (true) into IFF2
                cpu.set_iff1(false);           // disable interrupts for ISR
                if (cpu.get_halted()) {
                    // Wake from HALT: execution continues at instruction after HALT
                    cpu.set_halted(false);
                    cpu.set_pc(cpu.get_pc() + 1);
                }
                // Push current PC onto stack
                uint16_t sp  = cpu.get_sp() - 2;
                uint16_t ret = cpu.get_pc();
                bus.write(sp,     ret & 0xFF);
                bus.write(sp + 1, ret >> 8);
                cpu.set_sp(sp);
                cpu.set_pc(0x0038);  // IM1 vector
                bus.add_ticks(13);   // IM1 latency: 2 (sample) + 11 (push+jump)
                frame_t_states += 13;
                total_ticks    += 13;
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

        // Update title bar when cassette state or speed mode changes
        CassetteState cur_cas_state = bus.get_cassette_state();
        if (cur_cas_state != prev_cas_state || cur_speed != prev_speed) {
            std::string status   = bus.get_cassette_status();
            std::string speed_tag = (cur_speed == SpeedMode::TURBO) ? " [TURBO]" : "";
            if (status.empty()) {
                display.set_title("Mal-80 - TRS-80 Emulator" + speed_tag);
            } else {
                display.set_title("Mal-80 - " + status + speed_tag);
            }
            prev_cas_state = cur_cas_state;
            prev_speed     = cur_speed;
        }

        // Render frame: always in NORMAL mode; in TURBO mode render every Nth frame
        bool should_render = (cur_speed == SpeedMode::NORMAL) ||
                             (++turbo_render_count % TURBO_RENDER_EVERY == 0);
        if (should_render) {
            display.render_frame(bus);
        }

        // Frame pacing: in NORMAL mode sleep to maintain ~60Hz
        if (cur_speed == SpeedMode::NORMAL) {
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - frame_start).count();
            if (elapsed < NORMAL_FRAME_US) {
                SDL_Delay(static_cast<uint32_t>((NORMAL_FRAME_US - elapsed) / 1000));
            }
        }
        frame_start = std::chrono::steady_clock::now();


    }

    // Always dump trace on exit so we can inspect the last N instructions
    // even if the freeze detector didn't trigger.
    if (trace_count > 0) {
        dump_trace(trace_buf, trace_head, trace_count, bus);
    }

    display.cleanup();
    std::cout << "Mal-80 shutdown complete." << std::endl;
    return 0;
}
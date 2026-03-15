#include "Emulator.hpp"
#include <iostream>
#include <cstring>
#include <fstream>
#include <SDL.h>
#include "tinyfiledialogs.h"

static constexpr uint64_t T_STATES_PER_FRAME = 29498;        // ~60 Hz
static constexpr uint64_t TURBO_T_STATES     = T_STATES_PER_FRAME * 100;
static constexpr int      TURBO_RENDER_EVERY = 10;
static constexpr int      NORMAL_FRAME_US    = 16667;         // ~60 Hz in µs

Emulator::Emulator() : cpu_(bus_) {
    std::memset(keyboard_matrix_, 0, sizeof(keyboard_matrix_));
}

bool Emulator::init(int argc, char* argv[]) {
    std::cout << "╔════════════════════════════════════════╗\n"
              << "║         Welcome to Mal-80              ║\n"
              << "║      TRS-80 Model I Emulator           ║\n"
              << "╚════════════════════════════════════════╝\n";

    // Parse CLI arguments
    std::string cli_load_name;
    std::string cli_cmd_arg;
    std::string cli_disk_path[4];
    int         cli_phosphor  = 2;  // default: green
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout <<
                "Usage: mal-80 [options]\n"
                "\n"
                "Options:\n"
                "  --load <name>       Auto-load a file from software/ on startup.\n"
                "                      Case-insensitive prefix match; supports .cas and .bas.\n"
                "                      e.g. --load scarfman\n"
                "\n"
                "  --cmd <arg>         Load a .cmd binary (machine-language disk program)\n"
                "                      directly into RAM and start executing. <arg> can be:\n"
                "                        - A direct file path: --cmd /path/to/game.cmd\n"
                "                        - A zip-embedded path: --cmd /path/game/start.cmd\n"
                "                          (reads from /path/game.zip if it exists)\n"
                "                        - A bare name searched in software/ and software/*.zip\n"
                "                          e.g. --cmd adventure\n"
                "                      Overlay files loaded at runtime via RST 28h SVC\n"
                "                      intercept (@OPEN/@READ/@CLOSE/@LOAD) are resolved\n"
                "                      from the same zip or directory automatically.\n"
                "\n"
                "  --disk <path>       Mount a JV1 disk image on drive 0 (boot drive).\n"
                "  --disk0 <path>      Mount a JV1 disk image on drive 0.\n"
                "  --disk1 <path>      Mount a JV1 disk image on drive 1.\n"
                "  --disk2 <path>      Mount a JV1 disk image on drive 2.\n"
                "  --disk3 <path>      Mount a JV1 disk image on drive 3.\n"
                "                      JV1 format: 35 tracks x 10 sectors x 256 bytes.\n"
                "\n"
                "  --auto-ldos-date    Auto-inject today's date/time when LDOS asks 'Date ?'.\n"
                "\n"
                "  --colour <name>     Set the phosphor colour on startup.\n"
                "  --color  <name>     <name> is one of: white, amber, green (default: green).\n"
                "\n"
                "  --help, -h          Print this help and exit.\n"
                "\n"
                "Hotkeys (in emulator window):\n"
                "  F5           @ key      F8           Quit\n"
                "  F6           0 key      F9           Toggle CRT effects\n"
                "  F7           Dump RAM   Shift+F9     Cycle phosphor colour\n"
                "  F10          Warm boot  Shift+F10    Hard reset\n"
                "  Ctrl+V       Paste clipboard as keystrokes\n"
                "  Ctrl+0..3    Mount disk image on drive 0-3\n"
                "  Shift+F11    Hotkey help overlay\n";
            return false;  // exit cleanly without running the emulator
        } else if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc)
            cli_load_name = argv[++i];
        else if (std::strcmp(argv[i], "--cmd") == 0 && i + 1 < argc)
            cli_cmd_arg = argv[++i];
        else if (std::strcmp(argv[i], "--disk") == 0 && i + 1 < argc)
            cli_disk_path[0] = argv[++i];  // --disk defaults to drive 0
        else if (std::strcmp(argv[i], "--disk0") == 0 && i + 1 < argc)
            cli_disk_path[0] = argv[++i];
        else if (std::strcmp(argv[i], "--disk1") == 0 && i + 1 < argc)
            cli_disk_path[1] = argv[++i];
        else if (std::strcmp(argv[i], "--disk2") == 0 && i + 1 < argc)
            cli_disk_path[2] = argv[++i];
        else if (std::strcmp(argv[i], "--disk3") == 0 && i + 1 < argc)
            cli_disk_path[3] = argv[++i];
        else if (std::strcmp(argv[i], "--auto-ldos-date") == 0)
            auto_ldos_date_ = true;
        else if ((std::strcmp(argv[i], "--colour") == 0 ||
                  std::strcmp(argv[i], "--color")  == 0) && i + 1 < argc) {
            std::string c = argv[++i];
            if      (c == "white" || c == "0") cli_phosphor = 0;
            else if (c == "amber" || c == "1") cli_phosphor = 1;
            else if (c == "green" || c == "2") cli_phosphor = 2;
            else std::cerr << "[WARN] Unknown colour '" << c
                           << "' — use white, amber, or green\n";
        }
    }
    if (!display_.init("Mal-80 - TRS-80 Emulator")) {
        std::cerr << "Failed to initialize display\n";
        return false;
    }
    display_.set_phosphor_mode(cli_phosphor);

    try {
        bus_.load_rom("roms/level2.rom");
    } catch (const std::exception& e) {
        std::cerr << "ROM Load Failed: " << e.what() << "\n"
                  << "Place your TRS-80 ROM in roms/level2.rom\n";
        display_.cleanup();
        return false;
    }

    cpu_.reset();
    bus_.set_keyboard_matrix(keyboard_matrix_);
    frame_start_ = std::chrono::steady_clock::now();

    for (int drive = 0; drive < 4; drive++) {
        if (!cli_disk_path[drive].empty()) {
            if (!bus_.load_disk(drive, cli_disk_path[drive]))
                std::cerr << "Warning: failed to load disk" << drive << ": " << cli_disk_path[drive] << "\n";
        }
    }

    if (!cli_load_name.empty())
        loader_.setup_from_cli(cli_load_name, injector_);

    if (!cli_cmd_arg.empty())
        loader_.load_cmd_file(cli_cmd_arg, bus_, cpu_);

    sound_.init();  // non-fatal: logs a warning if SDL audio unavailable
    return true;
}

void Emulator::run() {
    while (display_.is_running()) {
        display_.handle_events(keyboard_matrix_);

        // ── Process emulator actions triggered by hotkeys ─────────────────
        {
            int drive_out = -1;
            switch (display_.pop_action(drive_out)) {

            case DisplayAction::SOFT_RESET:
                // Warm boot: jump straight to the BASIC READY prompt,
                // preserving the program in RAM.  Don't reset CPU registers
                // or RAM — BASIC's stack and variables stay intact.
                cpu_.set_pc(0x1A19);
                cpu_.set_iff1(false);
                cpu_.set_iff2(false);
                cpu_.set_halted(false);
                bus_.stop_cassette();
                display_.release_all_keys(keyboard_matrix_);
                injector_.clear();
                cur_speed_          = user_speed_;
                turbo_render_count_ = 0;
                frame_start_        = std::chrono::steady_clock::now();
                sound_.clear();
                break;

            case DisplayAction::HARD_RESET:
                bus_.hard_reset();
                cpu_.reset();
                display_.release_all_keys(keyboard_matrix_);
                injector_.clear();
                total_ticks_        = 0;
                prev_pc_            = 0;
                ldos_date_injected_ = false;
                cur_speed_          = user_speed_;
                turbo_render_count_ = 0;
                frame_start_        = std::chrono::steady_clock::now();
                sound_.clear();
                break;

            case DisplayAction::MOUNT_DISK:
                if (drive_out >= 0 && drive_out < 4) {
                    static const char* filters[] = {"*.dsk","*.dmk","*.imd","*.jv1"};
                    char dlg_title[32];
                    snprintf(dlg_title, sizeof(dlg_title), "Mount disk — drive %d", drive_out);
                    const char* path = tinyfd_openFileDialog(
                        dlg_title, "disks/", 4, filters, "Disk images", 0);
                    if (path && *path) {
                        if (!bus_.load_disk(drive_out, path))
                            std::cerr << "[DISK] Failed to mount: " << path << "\n";
                    }
                    // Reset frame timer: dialog may have taken several seconds
                    frame_start_ = std::chrono::steady_clock::now();
                }
                break;

            case DisplayAction::PASTE_CLIPBOARD: {
                char* text = SDL_GetClipboardText();
                if (text && *text)
                    injector_.enqueue(std::string(text));
                if (text) SDL_free(text);
                break;
            }

            case DisplayAction::DUMP_RAM: {
                // Dump the full 64KB address space (as seen by the Z80 via peek)
                // to memdump.bin so it can be compared against xtrs.
                std::ofstream f("memdump.bin", std::ios::binary);
                if (f) {
                    for (int a = 0; a < 65536; a++) {
                        uint8_t b = bus_.peek(static_cast<uint16_t>(a));
                        f.put(static_cast<char>(b));
                    }
                    fprintf(stderr, "[DUMP] memdump.bin written (64KB, PC=0x%04X SP=0x%04X)\n",
                            cpu_.get_pc(), cpu_.get_sp());
                    std::cerr << "[DUMP] memdump.bin written — compare with xtrs using:\n"
                              << "       cmp memdump.bin xtrs_dump.bin  OR  xxd memdump.bin | diff - <(xxd xtrs_dump.bin)\n";
                } else {
                    std::cerr << "[DUMP] ERROR: could not write memdump.bin\n";
                }
                break;
            }

            case DisplayAction::NONE:
            default:
                break;
            }
        }

        // Auto-select speed: turbo while keyboard injection is active
        SpeedMode desired = injector_.is_active() ? SpeedMode::TURBO : user_speed_;
        if (desired != cur_speed_) {
            // When returning to normal speed, discard any silence that
            // accumulated during turbo so game audio starts immediately.
            if (desired == SpeedMode::NORMAL)
                sound_.clear();
            cur_speed_          = desired;
            turbo_render_count_ = 0;
            frame_start_        = std::chrono::steady_clock::now();
        }

        uint64_t t_budget = (cur_speed_ == SpeedMode::TURBO)
                            ? TURBO_T_STATES : T_STATES_PER_FRAME;
        step_frame(t_budget);

        // Only push audio to SDL in normal mode.  In turbo mode the Z80 runs
        // at 100× speed, making all tones inaudible — don't fill the queue
        // with silence that would delay real game audio.
        if (cur_speed_ == SpeedMode::NORMAL)
            sound_.flush();

        update_title();

        bool should_render = (cur_speed_ == SpeedMode::NORMAL) ||
                             (++turbo_render_count_ % TURBO_RENDER_EVERY == 0);
        if (should_render)
            display_.render_frame(bus_);

        // Per-frame VRAM scan: detect LDOS "Date ?" prompt and auto-inject date/time.
        if (auto_ldos_date_ && !ldos_date_injected_) {
            for (int row = 0; row < 16; row++) {
                for (int col = 0; col < 60; col++) {
                    uint16_t off = (uint16_t)(row * 64 + col);
                    auto r = [&](int o) -> uint8_t {
                        return bus_.get_vram_byte((uint16_t)(off + o));
                    };
                    // "Date ?" = 0x44 0x61 0x74 0x65 0x20 0x3F
                    if (r(0)==0x44 && r(1)==0x61 && r(2)==0x74 && r(3)==0x65 &&
                        r(4)==0x20 && r(5)==0x3F) {
                        ldos_date_injected_ = true;
                        injector_.enqueue("01/01/84\n00:00:00\ndir :0\n");
                        goto date_scan_done;
                    }
                }
            }
            date_scan_done:;
        }

        if (cur_speed_ == SpeedMode::NORMAL)
            pace_frame();
        frame_start_ = std::chrono::steady_clock::now();
    }

    debugger_.dump(bus_);
    sound_.cleanup();
    display_.cleanup();
    std::cout << "Mal-80 shutdown complete.\n";
}

void Emulator::step_frame(uint64_t t_budget) {
    uint64_t frame_ts = 0;
    while (frame_ts < t_budget) {
        uint16_t pc = cpu_.get_pc();

        prev_pc_ = pc;
        bus_.set_cpu_pc(pc);

        loader_.on_system_entry(pc, cpu_, bus_);
        loader_.on_cload_entry(pc, cpu_, bus_, injector_);
        loader_.on_cload_tracking(pc, cpu_, bus_, injector_);
        loader_.on_csave_entry(pc, bus_);

        // Phase 2: intercept RST 28h when a CMD was loaded (no LDOS present)
        // to handle @OPEN/@READ/@CLOSE/@LOAD overlay SVCs transparently.
        if (loader_.cmd_loaded() && pc == 0x0028) {
            loader_.on_svc_entry(cpu_, bus_);
            continue;  // skip cpu_.step() — we faked the RST
        }

        if (injector_.handle_intercept(pc, cpu_, bus_, frame_ts))
            continue;

        debugger_.record(cpu_, total_ticks_);

        int ticks = cpu_.step();

        bus_.add_ticks(ticks);
        frame_ts     += ticks;
        total_ticks_ += ticks;

        // Sample the sound bit after the instruction (port 0xFF may have changed).
        // Mute during cassette I/O (FSK signal would be noise) and turbo mode
        // (Z80 running 100× fast makes all tones inaudibly high).
        bool sound_active = (cur_speed_ == SpeedMode::NORMAL) &&
                            (bus_.get_cassette_state() == CassetteState::IDLE);
        sound_.update(bus_.get_sound_bit(), ticks, sound_active);

        deliver_interrupt(frame_ts);

        if (bus_.is_recording_idle() || bus_.is_playback_done())
            bus_.stop_cassette();
    }
}

void Emulator::deliver_interrupt(uint64_t& frame_ts) {
    if (!bus_.interrupt_pending() || !cpu_.get_iff1()) return;
    // Real Z80 never accepts an interrupt between a prefix byte and its operand.
    if (cpu_.has_prefix_pending()) return;

    bus_.clear_interrupt();
    cpu_.set_iff2(cpu_.get_iff1());  // save IFF1 into IFF2 before disabling
    cpu_.set_iff1(false);

    if (cpu_.get_halted()) {
        cpu_.set_halted(false);
        cpu_.set_pc(cpu_.get_pc() + 1);  // resume after HALT
    }

    // Push PC and jump to IM1 vector (RST 38h)
    uint16_t sp  = cpu_.get_sp() - 2;
    uint16_t ret = cpu_.get_pc();
    bus_.write(sp,     ret & 0xFF);
    bus_.write(sp + 1, ret >> 8);
    cpu_.set_sp(sp);
    cpu_.set_pc(0x0038);

    constexpr int IM1_LATENCY = 13;  // 2 sample + 11 push+jump
    bus_.add_ticks(IM1_LATENCY);
    frame_ts     += IM1_LATENCY;
    total_ticks_ += IM1_LATENCY;
}

void Emulator::update_title() {
    CassetteState cur_cas = bus_.get_cassette_state();
    std::string   disk0   = bus_.get_disk_name(0);

    if (cur_cas == prev_cas_state_ && cur_speed_ == prev_speed_ &&
        disk0   == prev_disk0_name_)
        return;

    // Base: "Mal-80 [disk.dsk]" or just "Mal-80"
    std::string base = "Mal-80";
    if (!disk0.empty()) {
        size_t pos = disk0.rfind('/');
        std::string fname = (pos != std::string::npos) ? disk0.substr(pos + 1) : disk0;
        base += " [" + fname + "]";
    }

    std::string status    = bus_.get_cassette_status();
    std::string speed_tag = (cur_speed_ == SpeedMode::TURBO) ? " [TURBO]" : "";
    display_.set_title(status.empty()
        ? base + " - TRS-80 Emulator" + speed_tag
        : base + " - " + status + speed_tag);

    prev_cas_state_  = cur_cas;
    prev_speed_      = cur_speed_;
    prev_disk0_name_ = disk0;
}

void Emulator::pace_frame() {
    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       now - frame_start_).count();
    if (elapsed < NORMAL_FRAME_US)
        SDL_Delay(static_cast<uint32_t>((NORMAL_FRAME_US - elapsed) / 1000));
}

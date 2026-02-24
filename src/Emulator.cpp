#include "Emulator.hpp"
#include <iostream>
#include <cstring>
#include <SDL.h>

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
    std::string cli_disk_path;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc)
            cli_load_name = argv[++i];
        else if (std::strcmp(argv[i], "--disk") == 0 && i + 1 < argc)
            cli_disk_path = argv[++i];
    }

    if (!display_.init("Mal-80 - TRS-80 Emulator")) {
        std::cerr << "Failed to initialize display\n";
        return false;
    }

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

    if (!cli_disk_path.empty()) {
        if (!bus_.load_disk(0, cli_disk_path))
            std::cerr << "Warning: failed to load disk image: " << cli_disk_path << "\n";
    }

    if (!cli_load_name.empty())
        loader_.setup_from_cli(cli_load_name, injector_);

    sound_.init();  // non-fatal: logs a warning if SDL audio unavailable
    return true;
}

void Emulator::run() {
    while (display_.is_running()) {
        display_.handle_events(keyboard_matrix_);

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
    static uint16_t prev_pc = 0;
    static bool been_in_ram = false;   // armed once LDOS is running in RAM
    while (frame_ts < t_budget) {
        uint16_t pc = cpu_.get_pc();
        if (pc >= 0x4000) been_in_ram = true;
        if (been_in_ram && pc < 0x0100 && prev_pc >= 0x0100) {
            // Skip 0x004C (@DSPLY) — high-frequency display calls are noise
            if (pc != 0x004C) {
                fprintf(stderr, "[TRAP] jumped 0x%04X → 0x%04X  SP=0x%04X  A=0x%02X\n",
                        prev_pc, pc, cpu_.get_sp(), cpu_.get_a());
            }
            // One-shot: dump ROM shadow at $KEY on first call to see what LDOS installed
            if (pc == 0x0049) {
                static bool dumped_key_shadow = false;
                if (!dumped_key_shadow) {
                    dumped_key_shadow = true;
                    fprintf(stderr, "[KEY-SHADOW] bytes 0x0049-0x0060:");
                    for (uint16_t i = 0x0049; i <= 0x0060; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                }
            }
            if (pc == 0x002B) {
                static bool dumped_kbd_shadow = false;
                if (!dumped_kbd_shadow) {
                    dumped_kbd_shadow = true;
                    fprintf(stderr, "[KBD-SHADOW] bytes 0x002B-0x0048:");
                    for (uint16_t i = 0x002B; i <= 0x0048; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                    // Dump the common buffer-read dispatch at 0x0013-0x002A
                    fprintf(stderr, "[DISPATCH]   bytes 0x0013-0x002A:");
                    for (uint16_t i = 0x0013; i <= 0x002A; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                    // Dump LDOS keyboard workspace: buffer head/tail/data
                    fprintf(stderr, "[KBD-WORK]   bytes 0x4010-0x4040:");
                    for (uint16_t i = 0x4010; i <= 0x4040; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                    // Dump keyboard ring buffer (DCB+1:+2 = 0x03E3, within ROM shadow)
                    fprintf(stderr, "[KBD-RINGBUF] bytes 0x03DF-0x03F0:");
                    for (uint16_t i = 0x03DF; i <= 0x03F0; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                    // Dump start of actual LDOS ISR at 0x4518
                    fprintf(stderr, "[ISR-CODE]   bytes 0x4515-0x4570:");
                    for (uint16_t i = 0x4515; i <= 0x4570; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                    // Dump keyboard scanner at 0x4DA6 (called from ISR every timer tick)
                    fprintf(stderr, "[KBD-SCAN]   bytes 0x4DA0-0x4E20:");
                    for (uint16_t i = 0x4DA0; i <= 0x4E20; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                    // Extend workspace to see timer-handler dispatch table (timer handler at 0x405B)
                    fprintf(stderr, "[KBD-WORK2]  bytes 0x4041-0x4070:");
                    for (uint16_t i = 0x4041; i <= 0x4070; i++)
                        fprintf(stderr, " %02X", bus_.peek(i));
                    fprintf(stderr, "\n");
                }
            }
        }
        // Log when ROM routines return to >256 address with a non-zero A.
        // Catches keyboard scan routine (0x004C/$KEY) returning a key code.
        if (been_in_ram && prev_pc < 0x0100 && pc >= 0x0100) {
            uint8_t a = cpu_.get_a();
            if (a != 0) {
                fprintf(stderr, "[TRAP-RET] 0x%04X → 0x%04X  A=0x%02X ('%c')\n",
                        prev_pc, pc, a,
                        (a >= 0x20 && a < 0x7F) ? (char)a : '?');
            }
        }
        prev_pc = pc;

        loader_.on_system_entry(pc, cpu_, bus_);
        loader_.on_cload_entry(pc, cpu_, bus_, injector_);
        loader_.on_cload_tracking(pc, cpu_, bus_, injector_);
        loader_.on_csave_entry(pc, bus_);

        if (injector_.handle_intercept(pc, cpu_, bus_, frame_ts))
            continue;

        debugger_.record(cpu_, total_ticks_);
        if (debugger_.check_freeze(pc))
            debugger_.dump(bus_);

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
    if (cur_cas == prev_cas_state_ && cur_speed_ == prev_speed_) return;

    std::string status    = bus_.get_cassette_status();
    std::string speed_tag = (cur_speed_ == SpeedMode::TURBO) ? " [TURBO]" : "";
    display_.set_title(status.empty()
        ? "Mal-80 - TRS-80 Emulator" + speed_tag
        : "Mal-80 - " + status + speed_tag);

    prev_cas_state_ = cur_cas;
    prev_speed_     = cur_speed_;
}

void Emulator::pace_frame() {
    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       now - frame_start_).count();
    if (elapsed < NORMAL_FRAME_US)
        SDL_Delay(static_cast<uint32_t>((NORMAL_FRAME_US - elapsed) / 1000));
}

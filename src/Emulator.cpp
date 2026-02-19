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
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc)
            cli_load_name = argv[++i];
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

    if (!cli_load_name.empty())
        loader_.setup_from_cli(cli_load_name, injector_);

    return true;
}

void Emulator::run() {
    while (display_.is_running()) {
        display_.handle_events(keyboard_matrix_);

        // Auto-select speed: turbo while keyboard injection is active
        SpeedMode desired = injector_.is_active() ? SpeedMode::TURBO : user_speed_;
        if (desired != cur_speed_) {
            cur_speed_          = desired;
            turbo_render_count_ = 0;
            frame_start_        = std::chrono::steady_clock::now();
        }

        uint64_t t_budget = (cur_speed_ == SpeedMode::TURBO)
                            ? TURBO_T_STATES : T_STATES_PER_FRAME;
        step_frame(t_budget);

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
    display_.cleanup();
    std::cout << "Mal-80 shutdown complete.\n";
}

void Emulator::step_frame(uint64_t t_budget) {
    uint64_t frame_ts = 0;
    while (frame_ts < t_budget) {
        uint16_t pc = cpu_.get_pc();

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

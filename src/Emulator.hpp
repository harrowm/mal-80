#pragma once
#include "system/Bus.hpp"
#include "cpu/z80.hpp"
#include "video/Display.hpp"
#include "SoftwareLoader.hpp"
#include "KeyInjector.hpp"
#include "Debugger.hpp"
#include "Sound.hpp"
#include <chrono>
#include <cstring>

enum class SpeedMode { NORMAL, TURBO };

// Top-level emulator.  Owns the Bus, CPU, Display and all subsystems.
// Call init() once, then run() to enter the main loop.
class Emulator {
public:
    Emulator();
    bool init(int argc, char* argv[]);
    void run();

private:
    // Member declaration order matters: bus_ must precede cpu_ so that
    // bus_ is fully constructed before cpu_(bus_) runs.
    Bus     bus_;
    Z80     cpu_;
    Display display_;

    SoftwareLoader loader_;
    KeyInjector    injector_;
    Debugger       debugger_;
    Sound          sound_;

    uint8_t keyboard_matrix_[8]{};

    SpeedMode user_speed_         = SpeedMode::NORMAL;
    SpeedMode cur_speed_          = SpeedMode::NORMAL;
    int       turbo_render_count_ = 0;
    std::chrono::steady_clock::time_point frame_start_;
    uint64_t  total_ticks_        = 0;

    CassetteState prev_cas_state_ = CassetteState::IDLE;
    SpeedMode     prev_speed_     = SpeedMode::NORMAL;
    uint16_t      prev_pc_        = 0;
    bool          ldos_active_        = false;
    bool          ldos_date_injected_ = false;
    bool          auto_ldos_date_     = false;
    bool          sys12_dispatched_   = false;  // set after first JP(HL)=0x4E00

    void step_frame(uint64_t t_budget);
    void deliver_interrupt(uint64_t& frame_ts);
    void update_title();
    void pace_frame();
};

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
    std::string cli_disk_path[4];
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc)
            cli_load_name = argv[++i];
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
    }
    fprintf(stderr, "[INIT] auto_ldos_date=%s\n", auto_ldos_date_ ? "YES" : "no");

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

    for (int drive = 0; drive < 4; drive++) {
        if (!cli_disk_path[drive].empty()) {
            if (!bus_.load_disk(drive, cli_disk_path[drive]))
                std::cerr << "Warning: failed to load disk" << drive << ": " << cli_disk_path[drive] << "\n";
        }
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

        // Per-frame VRAM scan: detect LDOS Date? prompt and auto-inject
        // Also do ongoing VDUMP to track LDOS state
        if (auto_ldos_date_) {
            if (!ldos_date_injected_) {
                static bool vram_scan_started = false;
                if (!vram_scan_started) {
                    vram_scan_started = true;
                    fprintf(stderr, "[VDUMP] VRAM scan active\n");
                }
                bool found = false;
                for (int row = 0; row < 16 && !found; row++) {
                    for (int col = 0; col < 60 && !found; col++) {
                        uint16_t off = (uint16_t)(row * 64 + col);
                        auto r = [&](int o) -> uint8_t {
                            return bus_.get_vram_byte((uint16_t)(off + o)); // raw, no masking
                        };
                        // LDOS 5.3.1 shows "Date ? _" = 44 61 74 65 20 3F
                        // (D=0x44, a=0x61, t=0x74, e=0x65, space=0x20, ?=0x3F)
                        if (r(0)==0x44 && r(1)==0x61 && r(2)==0x74 && r(3)==0x65 &&
                            r(4)==0x20 && r(5)==0x3F) {
                            found = true;
                            fprintf(stderr, "[AUTO] 'Date ?' detected at VRAM row=%d col=%d, injecting date\n", row, col);
                        }
                    }
                }
                if (found) {
                    ldos_date_injected_ = true;
                    // Use \n for Enter (enqueue() maps \n→0x0D, strips \r)
                    injector_.enqueue("01/01/84\n00:00:00\n");
                }
            }

            // Every 60 frames (~1s) dump VRAM so we can see what's going on
            static int vram_dbg_countdown = 1;
            if (--vram_dbg_countdown <= 0) {
                vram_dbg_countdown = 60;
                // Find last non-empty row and dump it
                for (int row = 15; row >= 0; row--) {
                    bool has_content = false;
                    for (int i = 0; i < 64; i++) {
                        uint8_t b = bus_.get_vram_byte((uint16_t)(row * 64 + i));
                        if (b != 0 && b != 0x20) { has_content = true; break; }
                    }
                    if (has_content) {
                        fprintf(stderr, "[VDUMP] row%d hex: ", row);
                        for (int i = 0; i < 64; i++) {
                            uint8_t b = bus_.get_vram_byte((uint16_t)(row * 64 + i));
                            if (b != 0 && b != 0x20)
                                fprintf(stderr, "%02X ", b);
                            else
                                fprintf(stderr, ".. ");
                        }
                        fprintf(stderr, "\n");
                        fprintf(stderr, "[VDUMP] row%d txt: \"", row);
                        for (int i = 0; i < 64; i++) {
                            uint8_t b = bus_.get_vram_byte((uint16_t)(row * 64 + i));
                            uint8_t c = b & 0x7F;
                            fprintf(stderr, "%c", (c >= 32 && c < 127) ? c : '.');
                        }
                        fprintf(stderr, "\"\n");
                        break;
                    }
                }
            }
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

        // Watchpoint: catch any jump to the ROM reset vector (LDOS crash to "Memory Size?")
        if (pc <= 0x0003 && prev_pc_ > 0x0100) {
            uint8_t svclo = bus_.read(0x50B6), svchi = bus_.read(0x50B7);
            fprintf(stderr, "[CRASH] PC 0x%04X→0x%04X  SVC=0x%02X%02X (ptr=0x%04X)\n",
                    prev_pc_, pc, svchi, svclo, (uint16_t)(svclo|(svchi<<8)));
            // Dump LDOS memory variables area (0x4040-0x406F)
            fprintf(stderr, "[CRASH] RAM 0x4040-0x407F:\n");
            for (int a = 0x4040; a <= 0x407F; a += 16) {
                fprintf(stderr, "[CRASH]   %04X:", a);
                for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "[CRASH] RAM 0x4080-0x40FF:\n");
            for (int a = 0x4080; a <= 0x40FF; a += 16) {
                fprintf(stderr, "[CRASH]   %04X:", a);
                for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                fprintf(stderr, "\n");
            }
            // Dump RAM around the corruption site (0x4CDB) and SVC region
            fprintf(stderr, "[CRASH] RAM 0x4CC0-0x4CFF:\n");
            for (int a = 0x4CC0; a <= 0x4CFF; a += 16) {
                fprintf(stderr, "[CRASH]   %04X:", a);
                for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                fprintf(stderr, "  ");
                for (int i = 0; i < 16; i++) { uint8_t b = bus_.peek((uint16_t)(a+i)) & 0x7F; fprintf(stderr, "%c", b>=32&&b<127?b:'.'); }
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "[CRASH] RAM 0x50A0-0x50CF:\n");
            for (int a = 0x50A0; a <= 0x50CF; a += 16) {
                fprintf(stderr, "[CRASH]   %04X:", a);
                for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                fprintf(stderr, "  ");
                for (int i = 0; i < 16; i++) { uint8_t b = bus_.peek((uint16_t)(a+i)) & 0x7F; fprintf(stderr, "%c", b>=32&&b<127?b:'.'); }
                fprintf(stderr, "\n");
            }
            debugger_.dump(bus_);
        }
        // Track when LDOS kernel first runs
        if (!ldos_active_ && pc >= 0x4400 && pc < 0x6000) {
            ldos_active_ = true;
            fprintf(stderr, "[LDOS] kernel active at PC=0x%04X\n", pc);
        }

        // Log every transition from any address into high RAM (> 0x7FFF).
        // Fires when prev_pc was NOT in high RAM and pc IS in high RAM.
        if (pc > 0x7FFF && pc < 0xFFFF && prev_pc_ <= 0x7FFF) {
            static int highcall_count = 0;
            if (highcall_count < 10) {
                highcall_count++;
                uint16_t sp = cpu_.get_sp();
                uint16_t ret_lo = bus_.peek(sp), ret_hi = bus_.peek((uint16_t)(sp+1));
                uint16_t ret_addr = (uint16_t)(ret_lo | (ret_hi << 8));
                fprintf(stderr, "[HIGHCALL#%d] 0x%04X→0x%04X  SP=0x%04X ret=0x%04X  AF=%02X%02X BC=%04X DE=%04X HL=%04X\n",
                        highcall_count, prev_pc_, pc, sp, ret_addr,
                        cpu_.get_a(), cpu_.get_f(), cpu_.get_bc(), cpu_.get_de(), cpu_.get_hl());
                // Dump stack
                fprintf(stderr, "[HIGHCALL#%d] stack[SP..SP+15]:", highcall_count);
                for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(sp+i)));
                fprintf(stderr, "\n");
                // Dump 48 bytes at the target address to see what code is there
                fprintf(stderr, "[HIGHCALL#%d] RAM at 0x%04X..+47:\n", highcall_count, pc);
                for (int row = 0; row < 48; row += 16) {
                    uint16_t base = (uint16_t)(pc + row);
                    fprintf(stderr, "[HIGHCALL#%d]   %04X:", highcall_count, base);
                    for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(base+i)));
                    fprintf(stderr, "\n");
                }
            }
        }

        prev_pc_ = pc;
        bus_.set_cpu_pc(pc);

        // Dense per-instruction trace of the boot-loader decision range (0x4200-0x427F).
        // Skip inner copy loops to avoid log flooding.
        // Also log the JP(HL) at 0x4278 to capture the final dispatch target.
        if (pc >= 0x4200 && pc <= 0x427F) {
            // Skip the inner loops: PATCH data copy (56/59/5A/5B) and SKIP-N (62/66/69)
            bool skip_inner = (pc == 0x4256 || pc == 0x4259 || pc == 0x425A || pc == 0x425B
                             || pc == 0x4262 || pc == 0x4266 || pc == 0x4269);
            static int boot_trace_count = 0;
            if (!skip_inner && boot_trace_count < 200) {
                ++boot_trace_count;
                fprintf(stderr, "[BT#%04d] PC=%04X  AF=%02X%02X BC=%04X DE=%04X HL=%04X IX=%04X SP=%04X\n",
                        boot_trace_count, pc,
                        cpu_.get_a(), cpu_.get_f(),
                        cpu_.get_bc(), cpu_.get_de(), cpu_.get_hl(),
                        cpu_.get_ix(), cpu_.get_sp());
            } else if (!skip_inner && boot_trace_count == 200) {
                ++boot_trace_count;
                fprintf(stderr, "[BT] trace limit reached (200 instructions in 0x4200-0x427F)\n");
            }
        }
        // Always log the JP(HL) dispatch — this is the boot loader's final jump to LDOS entry
        if (pc == 0x4278) {
            uint16_t hl = cpu_.get_hl();
            fprintf(stderr, "[JPHL] boot JP(HL)=0x%04X  AF=%02X%02X BC=%04X DE=%04X SP=%04X\n",
                    hl, cpu_.get_a(), cpu_.get_f(),
                    cpu_.get_bc(), cpu_.get_de(), cpu_.get_sp());
            // First dispatch to SYS12: dump RAM at 0x4E00 so we can see what code is there
            if (!sys12_dispatched_ && hl == 0x4E00) {
                sys12_dispatched_ = true;
                fprintf(stderr, "[SYS12] RAM 0x4E00..0x4EFF at first dispatch:\n");
                for (int a = 0x4E00; a <= 0x4EFF; a += 16) {
                    fprintf(stderr, "[SYS12]   %04X:", a);
                    for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                    fprintf(stderr, "\n");
                }
            }
            // Second dispatch — HL=0x0000 means we're about to crash; dump context
            if (sys12_dispatched_ && hl == 0x0000) {
                fprintf(stderr, "[SYS12] FATAL: second dispatch HL=0x0000 — dumping 0x4200..0x42FF (T20/S8 content):\n");
                for (int a = 0x4200; a <= 0x42FF; a += 16) {
                    fprintf(stderr, "[SYS12]   %04X:", a);
                    for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                    fprintf(stderr, "\n");
                }
                uint16_t sp = cpu_.get_sp();
                fprintf(stderr, "[SYS12] stack at SP=0x%04X:", sp);
                for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(sp+i)));
                fprintf(stderr, "\n");
            }
        }
        // After first dispatch to 0x4E00, trace all instructions in 0x4000-0x5FFF.
        // Skip the FDC service routines in the boot-loader (0x42AC-0x42FF) to keep log manageable.
        // Also skip the inner byte-copy loop at 0x46CB-0x46D4 (runs thousands of times per sector).
        if (sys12_dispatched_ && pc >= 0x4000 && pc <= 0x5FFF) {
            bool skip_fdc   = (pc >= 0x42AC && pc <= 0x42FF)
                           || (pc >= 0x46CB && pc <= 0x46D4);  // inner byte-copy loop
            bool skip_inner = (pc == 0x4256 || pc == 0x4259 || pc == 0x425A || pc == 0x425B
                             || pc == 0x4262 || pc == 0x4266 || pc == 0x4269);
            static int s12_count = 0;
            if (!skip_fdc && !skip_inner) {
                if (s12_count < 500) {
                    ++s12_count;
                    fprintf(stderr, "[S12#%05d] PC=%04X  AF=%02X%02X BC=%04X DE=%04X HL=%04X SP=%04X\n",
                            s12_count, pc,
                            cpu_.get_a(), cpu_.get_f(),
                            cpu_.get_bc(), cpu_.get_de(), cpu_.get_hl(),
                            cpu_.get_sp());
                } else if (s12_count == 50000) {
                    ++s12_count;
                    fprintf(stderr, "[S12] trace limit reached (50000 instructions)\n");
                }
            }
        }

        // One-time dump of code at 0x4777 the moment PC first arrives there.
        // This reveals what the second-stage loader code actually contains in RAM.
        {
            static bool at_4777_dumped = false;
            if (sys12_dispatched_ && !at_4777_dumped && pc == 0x4777) {
                at_4777_dumped = true;
                fprintf(stderr, "[4777ENTRY] second-stage code — RAM dump 0x4600-0x4900:\n");
                for (int a = 0x4600; a <= 0x4900; a += 16) {
                    fprintf(stderr, "[4777ENTRY]   %04X:", a);
                    for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                    fprintf(stderr, "\n");
                }
            }
        }

        // One-time dump of 0x4040-0x40FF right when CALL 0x4777 returns (PC=0x4E8F).
        // This tells us if the second-stage loader initialised the LDOS vector table.
        {
            static bool after_4777_dumped = false;
            if (sys12_dispatched_ && !after_4777_dumped && pc == 0x4E8F) {
                after_4777_dumped = true;
                fprintf(stderr, "[4777RET] CALL 0x4777 returned — wide dump 0x4000-0x50FF:\n");
                for (int a = 0x4000; a <= 0x50FF; a += 16) {
                    fprintf(stderr, "[4777RET]   %04X:", a);
                    for (int i = 0; i < 16; i++) fprintf(stderr, " %02X", bus_.peek((uint16_t)(a+i)));
                    fprintf(stderr, "\n");
                }
            }
        }

        // Trace instructions in 0x4900-0x4FFF (LDOS init code around the BADRET site).
        if (sys12_dispatched_ && pc >= 0x4900 && pc <= 0x4FFF) {
            static int s49_count = 0;
            if (s49_count < 500) {
                ++s49_count;
                fprintf(stderr, "[S49#%03d] PC=%04X  AF=%02X%02X BC=%04X DE=%04X HL=%04X SP=%04X\n",
                        s49_count, pc,
                        cpu_.get_a(), cpu_.get_f(),
                        cpu_.get_bc(), cpu_.get_de(), cpu_.get_hl(),
                        cpu_.get_sp());
            }
        }

        // Trace instructions in high RAM (0xF000-0xFFFE) — intentionally brief.
        if (pc >= 0xF000 && pc < 0xFFFF) {
            static int hra_count = 0;
            if (hra_count < 5) {
                ++hra_count;
                fprintf(stderr, "[HRA#%02d] PC=%04X  AF=%02X%02X BC=%04X DE=%04X HL=%04X SP=%04X\n",
                        hra_count, pc,
                        cpu_.get_a(), cpu_.get_f(),
                        cpu_.get_bc(), cpu_.get_de(), cpu_.get_hl(),
                        cpu_.get_sp());
            } else if (hra_count == 5) {
                ++hra_count;
                fprintf(stderr, "[HRA] trace limit reached (5 instructions)\n");
            }
        }

        // Track every distinct module-copy destination in the copy loop (0x4CDB).
        if (pc == 0x4CDB) {
            static uint16_t last_copy_hl = 0xFFFF;
            uint16_t hl = cpu_.get_hl();
            if (hl != last_copy_hl) {
                last_copy_hl = hl;
                const char* region = hl > 0x7FFF ? "HIGH" : "low ";
                fprintf(stderr, "[COPY] dest=0x%04X [%s]  BC=0x%04X DE=0x%04X\n",
                        hl, region, cpu_.get_bc(), cpu_.get_de());
            }
        }

        loader_.on_system_entry(pc, cpu_, bus_);
        loader_.on_cload_entry(pc, cpu_, bus_, injector_);
        loader_.on_cload_tracking(pc, cpu_, bus_, injector_);
        loader_.on_csave_entry(pc, bus_);

        if (injector_.handle_intercept(pc, cpu_, bus_, frame_ts))
            continue;

        debugger_.record(cpu_, total_ticks_);

        // Capture HL before execution so we can detect when the module copy-loop
        // INC HL (at 0x4CDF) advances past a SVC-protected byte.  After the
        // instruction executes the copy+verify is complete; restore the saved SVC
        // dispatcher byte so the chain stays valid despite module data overwrites.
        uint16_t hl_before_step = cpu_.get_hl();
        int ticks = cpu_.step();
        if (pc == 0x4CDF && hl_before_step >= 0x50B0 && hl_before_step <= 0x50B7)
            bus_.restore_svc_byte_after_verify(hl_before_step);

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

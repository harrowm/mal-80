#include "Debugger.hpp"
#include "cpu/z80.hpp"
#include "system/Bus.hpp"
#include <fstream>
#include <iostream>
#include <cstdio>

void Debugger::record(Z80& cpu, uint64_t ticks) {
    TraceEntry& te = buf_[head_];
    te.pc    = cpu.get_pc(); te.sp = cpu.get_sp();
    te.a     = cpu.get_a();  te.f  = cpu.get_f();
    te.b     = cpu.get_b();  te.c  = cpu.get_c();
    te.d     = cpu.get_d();  te.e  = cpu.get_e();
    te.h     = cpu.get_h();  te.l  = cpu.get_l();
    te.ix    = cpu.get_ix(); te.iy = cpu.get_iy();
    te.i_reg = cpu.get_i();  te.im = cpu.get_im();
    te.iff1  = cpu.get_iff1(); te.iff2 = cpu.get_iff2();
    te.halted = cpu.get_halted();
    te.ticks = ticks;
    last_ticks_ = ticks;
    head_ = (head_ + 1) % BUF_SIZE;
    if (count_ < BUF_SIZE) count_++;
}

bool Debugger::check_freeze(uint16_t pc) {
    if (dumped_) return false;

    // Fast path: same PC repeated (single-address tight loop / HALT)
    if (pc == last_pc_) {
        streak_++;
    } else {
        last_pc_ = pc;
        streak_  = 0;
    }

    // Slow path: all PCs in the rolling window fit within a 64-byte range
    pc_window_[win_pos_] = pc;
    win_pos_ = (win_pos_ + 1) % FREEZE_WINDOW;
    if (!win_full_ && win_pos_ == 0) win_full_ = true;

    // Only consider tight-loop freezes in RAM (>= 0x4000).
    // The ROM's $KEY wait loop at 0x0049 is intentional; don't false-fire on it.
    bool tight = (streak_ > 100'000) && (pc >= 0x4000);
    if (!tight && win_full_) {
        uint16_t lo = pc_window_[0], hi = pc_window_[0];
        for (uint16_t p : pc_window_) {
            if (p < lo) lo = p;
            if (p > hi) hi = p;
        }
        if (lo >= 0x4000 && hi - lo < 64) {
            ticks_acc_ += 4;
        } else {
            ticks_acc_ = 0;
        }
        tight = (ticks_acc_ >= FREEZE_TICKS);
    }

    if (tight) {
        std::cerr << "[FREEZE] Detected at PC=0x" << std::hex << pc
                  << std::dec << " streak=" << streak_
                  << " ticks=" << last_ticks_ << "\n";
        dumped_ = true;
        return true;
    }
    return false;
}

void Debugger::dump(const Bus& bus) {
    if (count_ == 0) return;

    std::ofstream out("trace.log", std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[TRACE] Could not open trace.log\n";
        return;
    }
    out << "# Mal-80 freeze trace — last " << count_ << " instructions\n";
    out << "# TICKS       PC   SP   AF   BC   DE   HL   IX   IY  I IM IFF OP\n";

    size_t start = (count_ < BUF_SIZE) ? 0 : head_;
    for (size_t n = 0; n < count_; n++) {
        const TraceEntry& e = buf_[(start + n) % BUF_SIZE];
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
    std::cerr << "[TRACE] Dumped " << count_ << " instructions to trace.log\n";
}

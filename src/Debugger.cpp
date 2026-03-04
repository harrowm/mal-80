#include "Debugger.hpp"
#include "cpu/z80.hpp"
#include "system/Bus.hpp"
#include <fstream>
#include <iostream>
#include <cstdio>

void Debugger::record(Z80& cpu, uint64_t ticks) {
    // Skip ROM-area chatter (keyboard scan ISR < 0x4000); crash path is RAM-only
    if (cpu.get_pc() < 0x4000) return;
    // Skip hot inner loops that flood the buffer without useful information:
    //   0x4F18-0x4F1F: LDOS CRC-scan inner loop (BC=~0x0F2D iterations)
    //   0x4527-0x452F: sector-bit-test inner loop
    uint16_t pc = cpu.get_pc();
    if ((pc >= 0x4F18 && pc <= 0x4F1F) || (pc >= 0x4527 && pc <= 0x452F)) return;
    TraceEntry& te = buf_[head_];
    te.pc    = pc; te.sp = cpu.get_sp();
    te.a     = cpu.get_a();  te.f  = cpu.get_f();
    te.b     = cpu.get_b();  te.c  = cpu.get_c();
    te.d     = cpu.get_d();  te.e  = cpu.get_e();
    te.h     = cpu.get_h();  te.l  = cpu.get_l();
    te.ix    = cpu.get_ix(); te.iy = cpu.get_iy();
    te.i_reg = cpu.get_i();  te.im = cpu.get_im();
    te.iff1  = cpu.get_iff1(); te.iff2 = cpu.get_iff2();
    te.halted = cpu.get_halted();
    te.ticks = ticks;
    head_ = (head_ + 1) % BUF_SIZE;
    if (count_ < BUF_SIZE) count_++;
}

void Debugger::dump(const Bus& bus) {
    dump_to(bus, "trace.log");
}

void Debugger::dump_to(const Bus& bus, const std::string& filename) {
    if (count_ == 0) return;

    std::ofstream out(filename, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[TRACE] Could not open " << filename << "\n";
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
    std::cerr << "[TRACE] Dumped " << count_ << " instructions to " << filename << "\n";
}

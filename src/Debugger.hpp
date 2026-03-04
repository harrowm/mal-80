#pragma once
#include <cstdint>
#include <array>
#include <string>

class Z80;
class Bus;

struct TraceEntry {
    uint16_t pc, sp;
    uint8_t  a, f, b, c, d, e, h, l;
    uint16_t ix, iy;
    uint8_t  i_reg, im;
    bool     iff1, iff2, halted;
    uint64_t ticks;
};

// Circular trace buffer — records last N instructions, dumps on exit.
class Debugger {
public:
    static constexpr size_t BUF_SIZE = 10000;

    // Snapshot current CPU state into the circular buffer.
    void record(Z80& cpu, uint64_t ticks);

    // Write the last N instructions to trace.log.
    void dump(const Bus& bus);
    void dump_to(const Bus& bus, const std::string& filename);

    bool has_entries() const { return count_ > 0; }

private:
    std::array<TraceEntry, BUF_SIZE> buf_{};
    size_t   head_  = 0;
    size_t   count_ = 0;
};

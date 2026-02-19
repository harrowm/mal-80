#pragma once
#include <cstdint>
#include <array>

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

// Circular trace buffer + freeze detector.
// Call record() every instruction, check_freeze() after record(),
// and dump() to write trace.log.
class Debugger {
public:
    static constexpr size_t   BUF_SIZE        = 500;
    static constexpr size_t   FREEZE_WINDOW   = 64;
    static constexpr uint64_t FREEZE_TICKS    = 3'000'000;  // ~1.7 s of game time

    // Snapshot current CPU state into the circular buffer.
    void record(Z80& cpu, uint64_t ticks);

    // Update freeze detector for the current PC.
    // Returns true the first time a freeze is detected (caller should dump).
    bool check_freeze(uint16_t pc);

    // Write the last N instructions to trace.log.
    void dump(const Bus& bus);

    bool has_entries() const { return count_ > 0; }

private:
    std::array<TraceEntry, BUF_SIZE> buf_{};
    size_t   head_  = 0;
    size_t   count_ = 0;

    // Freeze detector state
    std::array<uint16_t, FREEZE_WINDOW> pc_window_{};
    size_t   win_pos_   = 0;
    bool     win_full_  = false;
    uint64_t ticks_acc_ = 0;
    bool     dumped_    = false;
    uint16_t last_pc_   = 0xFFFF;
    uint64_t streak_    = 0;
    uint64_t last_ticks_ = 0;
};

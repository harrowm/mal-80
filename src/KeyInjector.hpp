#pragma once
#include <queue>
#include <string>
#include <cstdint>

class Z80;
class Bus;

// Manages the keyboard-injection queue used to type BASIC programs and
// commands into the emulator.  Characters are drained one at a time
// via the $KEY ROM intercept (0x0049).
class KeyInjector {
public:
    static constexpr uint16_t ROM_KEY = 0x0049;

    // Append text to the queue.  a-z are uppercased, \n → 0x0D (Enter),
    // \r is dropped, other printable ASCII passes through unchanged.
    void enqueue(const std::string& text);

    // Read a plain-text BASIC file and append it to the queue,
    // prepending "NEW\n" to clear any existing program.
    void load_bas(const std::string& path);

    bool is_active() const { return !queue_.empty(); }

    // Call every step.  If pc == ROM_KEY and the queue is non-empty,
    // pops one character, fakes a RET with it in A, advances frame_ts,
    // and returns true (caller must skip cpu.step() for this cycle).
    bool handle_intercept(uint16_t pc, Z80& cpu, Bus& bus, uint64_t& frame_ts);

private:
    std::queue<uint8_t> queue_;
};

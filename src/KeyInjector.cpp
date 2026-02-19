#include "KeyInjector.hpp"
#include "cpu/z80.hpp"
#include "system/Bus.hpp"
#include <fstream>
#include <iostream>

void KeyInjector::enqueue(const std::string& text) {
    for (unsigned char c : text) {
        if      (c >= 'a' && c <= 'z') queue_.push(c - 32);   // uppercase
        else if (c == '\n')            queue_.push(0x0D);      // Enter
        else if (c == '\r')            continue;               // strip CR
        else if (c >= 0x20)            queue_.push(c);         // printable ASCII
    }
}

void KeyInjector::load_bas(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[BAS] Failed to open: " << path << "\n";
        return;
    }
    enqueue("NEW\n");
    std::string line;
    int lines = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            enqueue(line + "\n");
            lines++;
        }
    }
    std::cout << "[BAS] Queued " << lines << " lines ("
              << queue_.size() << " chars) from " << path << "\n";
}

bool KeyInjector::handle_intercept(uint16_t pc, Z80& cpu, Bus& bus,
                                   uint64_t& frame_ts) {
    if (pc != ROM_KEY || queue_.empty()) return false;

    uint8_t  ch       = queue_.front(); queue_.pop();
    uint16_t sp       = cpu.get_sp();
    uint16_t ret_addr = bus.peek(sp) | (bus.peek(sp + 1) << 8);
    cpu.set_sp(sp + 2);
    cpu.set_pc(ret_addr);
    cpu.set_a(ch);
    bus.add_ticks(10);
    frame_ts += 10;
    return true;
}

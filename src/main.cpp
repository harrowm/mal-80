// src/main.cpp
#include <iostream>
#include <SDL.h>
#include "cpu/z80.hpp"
#include "system/Bus.hpp"
#include "video/Display.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║         Welcome to Mal-80              ║" << std::endl;
    std::cout << "║      TRS-80 Model I Emulator           ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;

    Bus bus;
    Z80 cpu(bus);
    Display display;

    // Initialize display
    if (!display.init("Mal-80 - TRS-80 Emulator")) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }

    // Load ROM
    try {
        bus.load_rom("roms/level2.rom");
    } catch (const std::exception& e) {
        std::cerr << "ROM Load Failed: " << e.what() << std::endl;
        std::cerr << "Place your TRS-80 ROM in roms/level2.rom" << std::endl;
    }

    cpu.reset();

    // Keyboard matrix (8×8)
    uint8_t keyboard_matrix[8];
    std::memset(keyboard_matrix, 0x00, sizeof(keyboard_matrix));
    bus.set_keyboard_matrix(keyboard_matrix);

    // Emulation loop
    uint64_t frame_t_states = 0;
    constexpr uint64_t T_STATES_PER_FRAME = 29498;  // 60Hz

    while (display.is_running()) {
        // Handle input
        display.handle_events(keyboard_matrix);

        // Run CPU for one video frame
        frame_t_states = 0;
        while (frame_t_states < T_STATES_PER_FRAME) {
            int ticks = cpu.step();
            bus.add_ticks(ticks);
            frame_t_states += ticks;

            // Check for interrupts
            if (bus.interrupt_pending()) {
                bus.clear_interrupt();
                // TODO: Trigger Z80 INT (requires CPU interrupt support)
            }
        }

        // Render frame
        display.render_frame(bus);


    }

    display.cleanup();
    std::cout << "Mal-80 shutdown complete." << std::endl;
    return 0;
}
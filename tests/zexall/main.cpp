// tests/zexall/main.cpp
// ZEXALL / ZEXDOC Test Harness for Mal-80 Z80 CPU
//
// Runs a CP/M .COM file (zexall.com or zexdoc.com) in a minimal CP/M
// environment with BDOS console I/O trapping.
//
// Usage: zexall_test [path-to-com-file]
//        Default: tests/zexall/zexall.com

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <chrono>
#include "../../src/cpu/z80.hpp"
#include "../../src/system/Bus.hpp"

// CP/M Memory Layout
constexpr uint16_t CPM_TPA_START  = 0x0100;  // Transient Program Area
constexpr uint16_t CPM_BDOS_ENTRY = 0x0005;  // BDOS entry point
constexpr uint16_t CPM_BIOS_WBOOT = 0x0000;  // Warm boot (program exit)

// BDOS Functions
constexpr uint8_t BDOS_C_WRITE    = 2;   // Console output: char in E
constexpr uint8_t BDOS_C_WRITESTR = 9;   // Print string at DE until '$'

static bool load_com_file(const std::string& path, uint8_t* memory) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "Error: Cannot open '%s'\n", path.c_str());
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > (0xFE00 - CPM_TPA_START)) {
        fprintf(stderr, "Error: COM file too large (%zu bytes)\n", size);
        return false;
    }

    file.read(reinterpret_cast<char*>(memory + CPM_TPA_START), size);
    printf("Loaded: %s (%zu bytes) at 0x%04X\n", path.c_str(), size, CPM_TPA_START);
    return true;
}

static void setup_cpm_page_zero(uint8_t* memory) {
    // 0x0000: RET (warm boot trap — we detect PC==0 in the loop)
    memory[0x0000] = 0xC9;  // RET

    // 0x0005: RET (BDOS trap — we intercept before executing)
    memory[0x0005] = 0xC9;  // RET

    // 0x0006-0x0007: Fake top-of-TPA address (some programs read this)
    memory[0x0006] = 0x00;
    memory[0x0007] = 0xF0;  // TPA ends at 0xF000
}

int main(int argc, char* argv[]) {
    // Determine which COM file to run
    std::string com_path = "tests/zexall/zexall.com";
    if (argc > 1) {
        com_path = argv[1];
    }

    printf("╔════════════════════════════════════════╗\n");
    printf("║     Mal-80 Z80 ZEXALL Test Runner      ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    // Create bus in flat 64KB mode (no TRS-80 memory map)
    Bus bus(true);
    uint8_t* mem = bus.get_flat_memory();

    // Load the COM file
    if (!load_com_file(com_path, mem)) {
        return 1;
    }

    // Set up CP/M page zero
    setup_cpm_page_zero(mem);

    // Create and configure CPU
    Z80 cpu(bus);
    cpu.reset();

    // Set entry point and stack
    cpu.set_pc(CPM_TPA_START);
    cpu.set_sp(0xF000);

    printf("Starting Z80 execution at 0x%04X...\n\n", CPM_TPA_START);

    auto start_time = std::chrono::steady_clock::now();
    uint64_t total_cycles = 0;
    uint64_t total_instructions = 0;
    bool running = true;

    // Track output lines for pass/fail counting
    std::string current_line;
    int test_count = 0;
    int fail_count = 0;

    while (running) {
        uint16_t pc = cpu.get_pc();

        // ── TRAP: BDOS call at 0x0005 ──────────────────────────────────
        if (pc == CPM_BDOS_ENTRY) {
            uint8_t func = cpu.get_c();
            uint16_t sp = cpu.get_sp();

            if (func == BDOS_C_WRITE) {
                // Print single character from E register
                char ch = static_cast<char>(cpu.get_e());
                putchar(ch);
                if (ch == '\n') {
                    // Check for pass/fail in completed line
                    if (current_line.find("OK") != std::string::npos) {
                        test_count++;
                    } else if (current_line.find("ERROR") != std::string::npos) {
                        test_count++;
                        fail_count++;
                    }
                    current_line.clear();
                } else {
                    current_line += ch;
                }
            } else if (func == BDOS_C_WRITESTR) {
                // Print '$'-terminated string at DE
                uint16_t addr = cpu.get_de();
                while (true) {
                    char ch = static_cast<char>(mem[addr]);
                    if (ch == '$') break;
                    putchar(ch);
                    if (ch == '\n') {
                        if (current_line.find("OK") != std::string::npos) {
                            test_count++;
                        } else if (current_line.find("ERROR") != std::string::npos) {
                            test_count++;
                            fail_count++;
                        }
                        current_line.clear();
                    } else {
                        current_line += ch;
                    }
                    addr++;
                    if (addr == 0) break;  // Wrap-around safety
                }
            }

            // Simulate RET: pop return address from stack
            uint16_t ret_addr = mem[sp] | (mem[sp + 1] << 8);
            cpu.set_sp(sp + 2);
            cpu.set_pc(ret_addr);
            continue;
        }

        // ── TRAP: Warm boot (program exit) at 0x0000 ──────────────────
        if (pc == CPM_BIOS_WBOOT) {
            // Check last line
            if (!current_line.empty()) {
                if (current_line.find("OK") != std::string::npos) {
                    test_count++;
                } else if (current_line.find("ERROR") != std::string::npos) {
                    test_count++;
                    fail_count++;
                }
            }
            printf("\n\n--- Program terminated (CP/M warm boot) ---\n");
            running = false;
            break;
        }

        // Execute one instruction
        int cycles = cpu.step();
        total_cycles += cycles;
        total_instructions++;

        // Safety: detect infinite loops (ZEXALL runs ~46 billion T-states)
        if (total_instructions > 500000000000ULL) {
            fprintf(stderr, "\nExecution limit reached\n");
            running = false;
        }
    }

    fflush(stdout);

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    printf("\n════════════════════════════════════════\n");
    printf("Results:\n");
    printf("  Tests run:    %d\n", test_count);
    printf("  Failures:     %d\n", fail_count);
    printf("  Instructions: %llu\n", (unsigned long long)total_instructions);
    printf("  T-states:     %llu\n", (unsigned long long)total_cycles);
    printf("  Wall time:    %.2f seconds\n", elapsed.count() / 1000.0);
    if (elapsed.count() > 0) {
        double mhz = (total_cycles / 1000000.0) / (elapsed.count() / 1000.0);
        printf("  Effective:    %.2f MHz\n", mhz);
    }
    printf("════════════════════════════════════════\n");

    return fail_count > 0 ? 1 : 0;
}

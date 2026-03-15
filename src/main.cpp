#include <signal.h>
#include <execinfo.h>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include "Emulator.hpp"

static void crash_handler(int sig) {
    void* bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n=== CRASH: signal %d ===\n", sig);
    backtrace_symbols_fd(bt, n, 2);
    _exit(1);
}

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS,  crash_handler);
    signal(SIGABRT, crash_handler);

    // Handle --help / -h before constructing the emulator so it exits 0.
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            Emulator emu;
            emu.init(argc, argv);  // prints help text, returns false
            return 0;
        }
    }

    Emulator emu;
    if (!emu.init(argc, argv)) return 1;
    emu.run();
    return 0;
}

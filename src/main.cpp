#include <signal.h>
#include <execinfo.h>
#include <cstdlib>
#include <unistd.h>
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

    Emulator emu;
    if (!emu.init(argc, argv)) return 1;
    emu.run();
    return 0;
}

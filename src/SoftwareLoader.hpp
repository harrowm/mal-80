#pragma once
#include <string>
#include <cstdint>

class Z80;
class Bus;
class KeyInjector;

// Handles all software loading: finding files on disk, parsing .cas/.bas
// formats, and intercepting the ROM cassette entry points so that files
// load instantly instead of through real FSK playback.
//
// Call the on_*() methods unconditionally every step; each one checks the
// current PC and cassette state internally before acting.
class SoftwareLoader {
public:
    // Translate a --load <name> CLI argument into queued keystrokes / state.
    void setup_from_cli(const std::string& name, KeyInjector& injector);

    // LOPHD (0x02CE): intercept the SYSTEM command entry point.
    void on_system_entry(uint16_t pc, Z80& cpu, Bus& bus);

    // CSRDON (0x0293): intercept the CLOAD cassette-sync-search entry point.
    void on_cload_entry(uint16_t pc, Z80& cpu, Bus& bus, KeyInjector& injector);

    // Track in-progress CLOAD byte-by-byte (progress + mismatch reporting).
    // Also handles the IDLE transition when playback finishes.
    void on_cload_tracking(uint16_t pc, Z80& cpu, Bus& bus, KeyInjector& injector);

    // CSAVE write-leader entry (0x0284): start cassette recording.
    void on_csave_entry(uint16_t pc, Bus& bus);

private:
    bool   system_active_    = false;
    bool   cload_active_     = false;
    bool   cload_realigned_  = false;
    int    cload_byte_count_ = 0;
    size_t cload_sync_pos_   = 0;
    std::string cli_autoload_path_;
    bool   cli_autorun_      = false;

    static std::string find_cas_file(const std::string& filename,
                                     const char* tag = "CLOAD");
    static bool        is_system_cas(const std::string& path);
    static bool        load_system_cas(const std::string& path,
                                       Bus& bus, Z80& cpu);
    static std::string extract_filename(const Bus& bus);
    static std::string file_ext(const std::string& path);
};

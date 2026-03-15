#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <optional>
#include <array>

class Z80;
class Bus;
class KeyInjector;

// Describes where a CMD file (and its siblings) live — either on the
// filesystem directly, or inside a zip archive.
struct CmdSource {
    std::string zip_path;  // empty → not from a zip
    std::string entry;     // fs path when zip_path empty; path-inside-zip otherwise
    std::string dir;       // directory containing the zip, or the file itself

    bool from_zip() const { return !zip_path.empty(); }
};

// Handles all software loading: finding files on disk, parsing .cas/.bas/.cmd
// formats, and intercepting the ROM cassette / RST 28h entry points so that
// files load instantly instead of through real FSK playback or LDOS.
//
// Call the on_*() methods unconditionally every step; each one checks the
// current PC and cassette state internally before acting.
class SoftwareLoader {
public:
    // Translate a --load <name> CLI argument into queued keystrokes / state.
    void setup_from_cli(const std::string& name, KeyInjector& injector);

    // --cmd <arg>: load a CMD binary (with optional zip resolution) directly
    // into RAM and set the Z80 PC to the transfer address.  Returns true on
    // success.  Stores the resolved CmdSource so Phase-2 SVC intercepts can
    // find overlay files from the same zip/directory later.
    bool load_cmd_file(const std::string& arg, Bus& bus, Z80& cpu);

    // Returns true if a CMD file was successfully loaded at init time.
    bool cmd_loaded() const { return cmd_loaded_; }

    // RST 28h intercept (Phase 2): called every step when cmd_loaded().
    // Handles @OPEN, @READ, @CLOSE, @LOAD SVCs so overlay files can be
    // loaded from the same zip/directory without LDOS.
    void on_svc_entry(Z80& cpu, Bus& bus);

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
    // --- CMD loader state ---
    bool      cmd_loaded_  = false;
    CmdSource cmd_source_;

    // Virtual file table for @OPEN / @READ / @CLOSE SVC intercepts.
    // Keyed by a simple handle index (0–3).
    struct VFile {
        std::string          name;
        std::vector<uint8_t> data;
        size_t               pos = 0;
    };
    std::array<std::optional<VFile>, 4> vfiles_;

    // Set of SVC numbers we have already logged as "unhandled" (avoids spam).
    uint32_t svc_unhandled_logged_ = 0;  // bitmask for SVCs 0-31

    // --- cassette state ---
    bool   system_active_    = false;
    bool   cload_active_     = false;
    bool   cload_realigned_  = false;
    int    cload_byte_count_ = 0;
    size_t cload_sync_pos_   = 0;
    std::string cli_autoload_path_;
    bool   cli_autorun_      = false;

    // --- CMD helpers ---
    // Resolve a CLI arg (path, zip-embedded path, or bare name) to a CmdSource.
    static CmdSource resolve_cmd_source(const std::string& arg);

    // Read raw bytes from a CmdSource (filesystem or zip extraction).
    static std::vector<uint8_t> read_source_bytes(const CmdSource& src);

    // Parse and load a CMD record stream into bus RAM.
    // Sets cpu PC if a transfer address (0x02) record is found.
    // Returns true on success (exec address found and set).
    static bool parse_and_load_cmd(const std::vector<uint8_t>& buf,
                                   Bus& bus, Z80& cpu,
                                   const std::string& label);

    // Find a sibling file (e.g. an overlay) relative to cmd_source_.
    // Returns bytes, or empty vector if not found.
    std::vector<uint8_t> find_sibling_bytes(const std::string& name) const;

    // --- CAS/BAS helpers (unchanged) ---
    static std::string find_cas_file(const std::string& filename,
                                     const char* tag = "CLOAD");
    static bool        is_system_cas(const std::string& path);
    static bool        load_system_cas(const std::string& path,
                                       Bus& bus, Z80& cpu);
    static std::string extract_filename(const Bus& bus);
    static std::string file_ext(const std::string& path);
};

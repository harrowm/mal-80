#include "SoftwareLoader.hpp"
#include "KeyInjector.hpp"
#include "cpu/z80.hpp"
#include "system/Bus.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>

extern "C" {
// Suppress unused-function warnings from miniz's inline zlib-compat wrappers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "miniz.h"
#pragma clang diagnostic pop
}

// ROM addresses this class intercepts
static constexpr uint16_t ROM_SYSTEM_ENTRY = 0x02CE;  // LOPHD — SYSTEM loader entry
static constexpr uint16_t ROM_SYNC_SEARCH  = 0x0293;  // CSRDON — CLOAD sync search
static constexpr uint16_t ROM_WRITE_LEADER = 0x0284;  // CSAVE write-leader entry
static constexpr uint16_t ROM_BASIC_READY  = 0x1A19;  // BASIC warm restart (READY prompt)
static constexpr uint16_t ROM_FILENAME_PTR = 0x40A7;  // 2-byte ptr to 6-char filename in RAM
static constexpr uint16_t ROM_CASIN_FIRST  = 0x0235;  // first call into CASIN (clock realign)
static constexpr uint16_t ROM_CASIN_RET    = 0x0240;  // RET from CASIN: one full byte read

// ============================================================================
// Private helpers
// ============================================================================

std::string SoftwareLoader::file_ext(const std::string& path) {
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string SoftwareLoader::find_cas_file(const std::string& filename,
                                          const char* tag) {
    namespace fs = std::filesystem;
    std::cout << "[" << tag << "] Searching for: '" << filename << "'\n";
    if (!fs::exists("software")) return "";

    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> matches;
    for (auto& e : fs::directory_iterator("software")) {
        if (!e.is_regular_file()) continue;
        std::string ext = file_ext(e.path().string());
        if (ext != ".cas" && ext != ".bas") continue;
        std::string stem = e.path().stem().string();
        std::string stem_lower = stem;
        std::transform(stem_lower.begin(), stem_lower.end(),
                       stem_lower.begin(), ::tolower);
        if (lower.empty() || stem_lower.find(lower) == 0)
            matches.push_back(e.path().string());
    }
    if (matches.empty()) {
        std::cout << "[" << tag << "] No match found for: '" << filename << "'\n";
        return "";
    }
    std::sort(matches.begin(), matches.end());
    std::cout << "[" << tag << "] Picking: '" << matches.front() << "'\n";
    return matches.front();
}

bool SoftwareLoader::is_system_cas(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});
    size_t i = 0;
    while (i < buf.size() && buf[i] == 0x00) i++;
    if (i >= buf.size() || buf[i] != 0xA5) return false;
    i++;
    return i < buf.size() && buf[i] == 0x55;
}

bool SoftwareLoader::load_system_cas(const std::string& path,
                                     Bus& bus, Z80& cpu) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[SYSTEM] Failed to open: " << path << "\n";
        return false;
    }
    std::vector<uint8_t> buf(
        (std::istreambuf_iterator<char>(file)), {});

    size_t i = 0;
    while (i < buf.size() && buf[i] == 0x00) i++;  // skip leader

    if (i >= buf.size() || buf[i] != 0xA5) {
        std::cerr << "[SYSTEM] No sync byte (0xA5) in: " << path << "\n";
        return false;
    }
    i++;  // sync

    if (i >= buf.size() || buf[i] != 0x55) {
        std::cerr << "[SYSTEM] Not a SYSTEM file in: " << path << "\n";
        return false;
    }
    i++;  // type byte

    if (i + 6 > buf.size()) {
        std::cerr << "[SYSTEM] Truncated filename in: " << path << "\n";
        return false;
    }
    std::string cas_name(buf.begin() + i, buf.begin() + i + 6);
    i += 6;

    uint16_t exec_addr  = 0;
    bool     found_exec = false;
    int      n_blocks   = 0;

    while (i < buf.size()) {
        uint8_t marker = buf[i++];

        if (marker == 0x3C) {
            if (i + 3 > buf.size()) {
                std::cerr << "[SYSTEM] Truncated block header\n";
                return false;
            }
            uint8_t  count     = buf[i++];
            uint8_t  load_lo   = buf[i++];
            uint8_t  load_hi   = buf[i++];
            uint16_t load_addr = load_lo | (load_hi << 8);
            size_t   n         = (count == 0) ? 256 : count;

            if (i + n + 1 > buf.size()) {
                std::cerr << "[SYSTEM] Truncated block data\n";
                return false;
            }
            uint8_t cksum = (load_hi + load_lo) & 0xFF;
            for (size_t j = 0; j < n; j++) cksum = (cksum + buf[i + j]) & 0xFF;
            if (cksum != buf[i + n])
                std::cerr << "[SYSTEM] Checksum error at 0x"
                          << std::hex << load_addr << std::dec << "\n";

            for (size_t j = 0; j < n; j++)
                bus.write(static_cast<uint16_t>(load_addr + j), buf[i + j]);
            i += n + 1;
            n_blocks++;

        } else if (marker == 0x78) {
            if (i + 2 > buf.size()) {
                std::cerr << "[SYSTEM] Truncated EOF block\n";
                return false;
            }
            exec_addr  = buf[i] | (buf[i + 1] << 8);
            i += 2;
            found_exec = true;
            break;

        } else {
            std::cerr << "[SYSTEM] Unknown block marker 0x"
                      << std::hex << (int)marker << std::dec << "\n";
            return false;
        }
    }

    if (!found_exec) {
        std::cerr << "[SYSTEM] No EOF block (0x78) in: " << path << "\n";
        return false;
    }
    cpu.set_pc(exec_addr);
    std::cout << "[SYSTEM] Loaded '" << cas_name << "' from " << path
              << " (" << n_blocks << " blocks), exec 0x"
              << std::hex << exec_addr << std::dec << "\n";
    return true;
}

std::string SoftwareLoader::extract_filename(const Bus& bus) {
    uint16_t ptr = bus.peek(ROM_FILENAME_PTR) |
                   (bus.peek(ROM_FILENAME_PTR + 1) << 8);
    if (bus.peek(ptr) == '"') ptr++;
    std::string result;
    for (int i = 0; i < 6; i++) {
        uint8_t ch = bus.peek(ptr + i);
        if (ch == 0x00 || ch == '"' || ch < 0x20 || ch > 0x7E) break;
        result += static_cast<char>(ch);
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

// ============================================================================
// Public interface
// ============================================================================

void SoftwareLoader::setup_from_cli(const std::string& name,
                                    KeyInjector& injector) {
    std::string path = find_cas_file(name, "LOAD");
    if (path.empty()) {
        std::cerr << "[LOAD] No file found matching: " << name << "\n";
        return;
    }
    namespace fs = std::filesystem;
    std::string ext  = file_ext(path);
    std::string stem = fs::path(path).stem().string();

    if (ext == ".cas") {
        if (is_system_cas(path)) {
            // Type SYSTEM\n<name>\n — leading \n answers the cold-boot MEMORY SIZE? prompt
            injector.enqueue("\nSYSTEM\n" + stem + "\n");
        } else {
            cli_autoload_path_ = path;
            injector.enqueue("CLOAD\n");
            cli_autorun_ = true;
        }
    } else if (ext == ".bas") {
        injector.enqueue("\n");   // answer cold-boot MEMORY SIZE? prompt
        injector.load_bas(path);
        injector.enqueue("RUN\n");
    }
}

void SoftwareLoader::on_system_entry(uint16_t pc, Z80& cpu, Bus& bus) {
    if (pc != ROM_SYSTEM_ENTRY) return;
    fprintf(stderr, "[DBG] on_system_entry fired PC=0x%04X\n", pc);

    system_active_ = true;
    std::string fname = extract_filename(bus);
    std::string path  = find_cas_file(fname, "SYSTEM");
    if (!path.empty() && load_system_cas(path, bus, cpu))
        system_active_ = false;  // success: suppress the upcoming CSRDON intercept
    // on failure: system_active_ stays true so CLOAD is skipped for this file
}

void SoftwareLoader::on_cload_entry(uint16_t pc, Z80& cpu, Bus& bus,
                                    KeyInjector& injector) {
    if (pc != ROM_SYNC_SEARCH) return;
    if (bus.get_cassette_state() != CassetteState::IDLE) return;
    fprintf(stderr, "[DBG] on_cload_entry fired PC=0x%04X\n", pc);

    if (system_active_) {
        // CSRDON reached after a failed SYSTEM fast-load — skip, don't CLOAD
        system_active_ = false;
        return;
    }

    std::string path;
    std::string fname;
    if (!cli_autoload_path_.empty()) {
        path = cli_autoload_path_;
        cli_autoload_path_.clear();
        std::cout << "[CLOAD] Using CLI autoload: " << path << "\n";
    } else {
        fname = extract_filename(bus);
        path  = find_cas_file(fname);
    }

    if (path.empty()) {
        std::cout << "CLOAD: no file found\n";
        return;
    }

    std::string ext = file_ext(path);
    if (ext == ".bas") {
        injector.load_bas(path);
        cpu.set_pc(ROM_BASIC_READY);
    } else {
        if (bus.load_cas_file(path)) {
            bus.set_cas_filename(fname.empty() ? "(auto)" : fname);
            bus.start_playback();
            const auto& cas = bus.get_cas_data();
            cload_active_     = true;
            cload_realigned_  = false;
            cload_byte_count_ = 0;
            cload_sync_pos_   = 0;
            for (size_t i = 0; i < cas.size(); i++)
                if (cas[i] == 0xA5) { cload_sync_pos_ = i; break; }
            size_t data_bytes = cas.size() - cload_sync_pos_ - 1;
            std::cout << "CLOAD: " << path << " (" << data_bytes << " bytes)\n";
        }
    }
}

void SoftwareLoader::on_cload_tracking(uint16_t pc, Z80& cpu, Bus& bus,
                                       KeyInjector& injector) {
    if (!cload_active_) return;

    if (bus.get_cassette_state() == CassetteState::PLAYING) {
        if (pc == ROM_CASIN_FIRST && !cload_realigned_) {
            bus.realign_cas_clock();
            cload_realigned_ = true;
        }
        if (pc == ROM_CASIN_RET) {
            uint8_t     actual   = cpu.get_a();
            const auto& cas      = bus.get_cas_data();
            size_t      exp_idx  = cload_sync_pos_ + 1 + cload_byte_count_;
            uint8_t     expected = (exp_idx < cas.size()) ? cas[exp_idx] : 0xFF;
            size_t      total    = cas.size() - cload_sync_pos_ - 1;

            if (actual != expected)
                fprintf(stderr,
                    "[CLOAD] MISMATCH byte %d/%zu: got 0x%02X expected 0x%02X\n",
                    cload_byte_count_, total, actual, expected);
            if (cload_byte_count_ % 512 == 0)
                fprintf(stderr, "[CLOAD] Progress: %d / %zu bytes (%.0f%%)\n",
                    cload_byte_count_, total,
                    100.0 * cload_byte_count_ / total);
            cload_byte_count_++;
        }
    }

    if (bus.get_cassette_state() == CassetteState::IDLE) {
        fprintf(stderr, "[CLOAD] Complete: %d bytes read\n", cload_byte_count_);
        cload_active_ = false;
        if (cli_autorun_) {
            injector.enqueue("RUN\n");
            cli_autorun_ = false;
        }
    }
}

void SoftwareLoader::on_csave_entry(uint16_t pc, Bus& bus) {
    if (pc != ROM_WRITE_LEADER) return;
    if (bus.get_cassette_state() != CassetteState::IDLE) return;

    std::string fname = extract_filename(bus);
    bus.set_cas_filename(fname);
    bus.start_recording();
    std::cout << "CSAVE: recording"
              << (fname.empty() ? "" : " \"" + fname + "\"") << "\n";
}

// ============================================================================
// CMD file loading (Phase 1 — init-time host-side loader)
// ============================================================================

// Lower-case a string in place and return it.
static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Return the lower-cased extension of path (including the dot), e.g. ".cmd".
static std::string ext_lower(const std::string& path) {
    namespace fs = std::filesystem;
    return to_lower(fs::path(path).extension().string());
}

// Case-insensitive filename comparison (base name only, no extension).
// (Reserved for future use.)
// static bool stem_iequal(...) — removed to avoid unused-function warning

// ─────────────────────────────────────────────────────────────────────────────
// resolve_cmd_source: turn a CLI argument into a CmdSource.
//
// Resolution order:
//  1. arg is an existing file on disk → direct path
//  2. Walk the path from right-to-left stripping one component at a time;
//     if <parent>.zip exists and contains the right entry → zip source
//  3. Bare-name search: look in software/ for *.cmd prefix match (shortest)
//  4. Bare-name search inside software/*.zip files
// ─────────────────────────────────────────────────────────────────────────────
CmdSource SoftwareLoader::resolve_cmd_source(const std::string& arg) {
    namespace fs = std::filesystem;

    // 1. Direct path
    if (fs::exists(arg) && fs::is_regular_file(arg))
        return { "", arg, fs::path(arg).parent_path().string() };

    // 2. Walk path components: check if <prefix>.zip exists and contains <suffix>
    fs::path p(arg);
    fs::path suffix = p.filename();
    fs::path parent = p.parent_path();
    while (!parent.empty() && parent != parent.parent_path()) {
        fs::path zip_candidate = fs::path(parent.string() + ".zip");
        if (fs::exists(zip_candidate)) {
            // Check if this zip contains the suffix entry (case-insensitive)
            mz_zip_archive zip{};
            if (mz_zip_reader_init_file(&zip, zip_candidate.string().c_str(), 0)) {
                mz_uint n = mz_zip_reader_get_num_files(&zip);
                std::string want = to_lower(suffix.string());
                for (mz_uint i = 0; i < n; i++) {
                    mz_zip_archive_file_stat st;
                    if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
                    if (to_lower(st.m_filename) == want) {
                        mz_zip_reader_end(&zip);
                        return { zip_candidate.string(), st.m_filename,
                                 zip_candidate.parent_path().string() };
                    }
                }
                mz_zip_reader_end(&zip);
            }
        }
        suffix = fs::path(parent.filename()) / suffix;
        parent = parent.parent_path();
    }

    // 3. Bare name: search software/ for *.cmd prefix match
    if (fs::exists("software")) {
        std::string lower_arg = to_lower(arg);
        std::vector<std::string> matches;
        for (auto& e : fs::directory_iterator("software")) {
            if (!e.is_regular_file()) continue;
            if (ext_lower(e.path().string()) != ".cmd") continue;
            std::string sl = to_lower(e.path().stem().string());
            if (sl.find(lower_arg) == 0)
                matches.push_back(e.path().string());
        }
        if (!matches.empty()) {
            std::sort(matches.begin(), matches.end());
            const std::string& best = matches.front();
            return { "", best, fs::path(best).parent_path().string() };
        }

        // 4. Search inside software/*.zip
        for (auto& e : fs::directory_iterator("software")) {
            if (!e.is_regular_file()) continue;
            if (ext_lower(e.path().string()) != ".zip") continue;
            mz_zip_archive zip{};
            if (!mz_zip_reader_init_file(&zip, e.path().string().c_str(), 0))
                continue;
            mz_uint n = mz_zip_reader_get_num_files(&zip);
            std::vector<std::pair<std::string,std::string>> hits; // {filename, rawname}
            for (mz_uint i = 0; i < n; i++) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
                std::string fn = st.m_filename;
                if (ext_lower(fn) != ".cmd") continue;
                std::string sl = to_lower(fs::path(fn).stem().string());
                if (sl.find(lower_arg) == 0)
                    hits.push_back({ fn, fn });
            }
            mz_zip_reader_end(&zip);
            if (!hits.empty()) {
                std::sort(hits.begin(), hits.end());
                return { e.path().string(), hits.front().first,
                         e.path().parent_path().string() };
            }
        }
    }

    return {};  // not found
}

// ─────────────────────────────────────────────────────────────────────────────
// read_source_bytes: read raw bytes from a CmdSource (disk or zip).
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint8_t> SoftwareLoader::read_source_bytes(const CmdSource& src) {
    if (!src.from_zip()) {
        std::ifstream f(src.entry, std::ios::binary);
        if (!f.is_open()) return {};
        return std::vector<uint8_t>(
            (std::istreambuf_iterator<char>(f)), {});
    }

    // Extract from zip
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, src.zip_path.c_str(), 0)) {
        std::cerr << "[CMD] Cannot open zip: " << src.zip_path << "\n";
        return {};
    }
    // locate the entry (case-insensitive)
    std::string want = to_lower(src.entry);
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    int idx = -1;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (to_lower(st.m_filename) == want) { idx = (int)i; break; }
    }
    if (idx < 0) {
        std::cerr << "[CMD] Entry '" << src.entry
                  << "' not found in " << src.zip_path << "\n";
        mz_zip_reader_end(&zip);
        return {};
    }
    size_t sz = 0;
    void* raw = mz_zip_reader_extract_to_heap(&zip, (mz_uint)idx, &sz, 0);
    mz_zip_reader_end(&zip);
    if (!raw) {
        std::cerr << "[CMD] Extraction failed for: " << src.entry << "\n";
        return {};
    }
    std::vector<uint8_t> buf(static_cast<uint8_t*>(raw),
                              static_cast<uint8_t*>(raw) + sz);
    mz_free(raw);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_and_load_cmd: parse CMD record stream, write into bus RAM.
// Returns true if a transfer address (0x02) was found and PC was set.
// ─────────────────────────────────────────────────────────────────────────────
bool SoftwareLoader::parse_and_load_cmd(const std::vector<uint8_t>& buf,
                                        Bus& bus, Z80& cpu,
                                        const std::string& label) {
    if (buf.empty()) {
        std::cerr << "[CMD] Empty file: " << label << "\n";
        return false;
    }

    // Valid CMD starts with 0x01 (load block) or 0x05 (module header).
    if (buf[0] != 0x01 && buf[0] != 0x05) {
        std::cerr << "[CMD] Not a CMD file (first byte 0x"
                  << std::hex << (int)buf[0] << std::dec
                  << "): " << label << "\n";
        return false;
    }

    size_t   i          = 0;
    uint16_t exec_addr  = 0;
    bool     has_exec   = false;
    uint16_t lowest     = 0xFFFF;
    int      n_blocks   = 0;
    std::string module_name;

    while (i < buf.size()) {
        uint8_t type  = buf[i++];
        if (i >= buf.size()) break;
        uint8_t count = buf[i++];

        // count=0 means 256 bytes follow in the frame.
        // For load blocks (0x01), the frame includes 2 address bytes,
        // so actual data = (count==0 ? 254 : count-2), except count==2 wraps.
        size_t frame = (count == 0) ? 256 : count;

        switch (type) {

        case 0x01: {  // Load block: [addr_lo] [addr_hi] [data...]
            if (i + 2 > buf.size()) {
                if (has_exec) break;  // trailing garbage after exec — stop gracefully
                std::cerr << "[CMD] Truncated load block in: " << label << "\n";
                return false;
            }
            uint8_t  lo   = buf[i++];
            uint8_t  hi   = buf[i++];
            uint16_t addr = static_cast<uint16_t>(lo | (hi << 8));
            // data_count = frame - 2 (address bytes), but if result ≤ 0 → 256
            int dc = static_cast<int>(frame) - 2;
            if (dc <= 0) dc = 256;  // count==2 or count==0 wraps
            if (i + (size_t)dc > buf.size()) {
                if (has_exec) break;  // trailing garbage after exec — stop gracefully
                std::cerr << "[CMD] Truncated data in load block at 0x"
                          << std::hex << addr << std::dec << ": " << label << "\n";
                return false;
            }
            for (int j = 0; j < dc; j++)
                bus.write(static_cast<uint16_t>(addr + j), buf[i + j]);
            i += (size_t)dc;
            if (addr < lowest) lowest = addr;
            n_blocks++;
            break;
        }

        case 0x02: {  // Transfer address (last one wins)
            if (i + 2 > buf.size()) {
                std::cerr << "[CMD] Truncated transfer address in: " << label << "\n";
                return false;
            }
            uint8_t lo = buf[i++];
            uint8_t hi = buf[i++];
            exec_addr  = static_cast<uint16_t>(lo | (hi << 8));
            has_exec   = true;
            // Stop here — exec record found; trailing data is not meaningful.
            goto done_parsing;
            break;
        }

        case 0x03:  // EOF, no transfer address — stop loading
            std::cout << "[CMD] " << label << ": no transfer address (overlay); "
                      << n_blocks << " blocks loaded, lowest addr 0x"
                      << std::hex << lowest << std::dec << "\n";
            return false;

        case 0x05: {  // Module header — ASCII name, informational
            size_t name_end = std::min(i + frame, buf.size());
            module_name = std::string(buf.begin() + (long)i,
                                      buf.begin() + (long)name_end);
            i += frame;
            break;
        }

        case 0x10: {  // Yanked load block — same layout as 0x01 but NOT stored
            if (i + 2 > buf.size()) { i = buf.size(); break; }
            i += 2;  // skip address bytes
            int dc = static_cast<int>(frame) - 2;
            if (dc <= 0) dc = 256;
            if (i + (size_t)dc <= buf.size()) i += (size_t)dc;
            break;
        }

        case 0x1f: {  // Copyright block — ASCII text, skip
            if (i + frame <= buf.size()) i += frame;
            break;
        }

        default:
            // Unknown record — skip the declared frame and warn once.
            fprintf(stderr, "[CMD] Unknown record type 0x%02X, skipping %zu bytes"
                    " in: %s\n", type, frame, label.c_str());
            if (i + frame <= buf.size()) i += frame;
            break;
        }
    }
    done_parsing:

    if (!has_exec) {
        std::cerr << "[CMD] No transfer address in: " << label
                  << " (lowest load addr was 0x" << std::hex << lowest
                  << std::dec << ")\n";
        return false;
    }

    cpu.set_pc(exec_addr);
    std::cout << "[CMD] Loaded ";
    if (!module_name.empty()) std::cout << "'" << module_name << "' ";
    std::cout << "from " << label << " (" << n_blocks << " blocks)"
              << ", exec 0x" << std::hex << exec_addr << std::dec << "\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// find_sibling_bytes: locate a named file relative to cmd_source_.
// Searches: the same zip, or the same directory on disk.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint8_t> SoftwareLoader::find_sibling_bytes(const std::string& name) const {
    namespace fs = std::filesystem;
    std::string lower_name = to_lower(name);

    if (cmd_source_.from_zip()) {
        // Search zip for any entry whose filename (base) matches case-insensitively
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, cmd_source_.zip_path.c_str(), 0))
            return {};
        mz_uint n = mz_zip_reader_get_num_files(&zip);
        int idx = -1;
        for (mz_uint i = 0; i < n; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
            std::string fn = to_lower(fs::path(st.m_filename).filename().string());
            if (fn == lower_name) { idx = (int)i; break; }
        }
        if (idx < 0) { mz_zip_reader_end(&zip); return {}; }
        size_t sz = 0;
        void* raw = mz_zip_reader_extract_to_heap(&zip, (mz_uint)idx, &sz, 0);
        mz_zip_reader_end(&zip);
        if (!raw) return {};
        std::vector<uint8_t> buf(static_cast<uint8_t*>(raw),
                                  static_cast<uint8_t*>(raw) + sz);
        mz_free(raw);
        return buf;
    }

    // Filesystem: search the same directory as the CMD file
    std::string dir = cmd_source_.dir.empty() ? "." : cmd_source_.dir;
    if (!fs::exists(dir)) return {};
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        if (to_lower(e.path().filename().string()) == lower_name) {
            std::ifstream f(e.path().string(), std::ios::binary);
            if (!f.is_open()) return {};
            return std::vector<uint8_t>(
                (std::istreambuf_iterator<char>(f)), {});
        }
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// load_cmd_file (public): main entry point called from Emulator::init().
// ─────────────────────────────────────────────────────────────────────────────
bool SoftwareLoader::load_cmd_file(const std::string& arg, Bus& bus, Z80& cpu) {
    CmdSource src = resolve_cmd_source(arg);
    if (src.entry.empty()) {
        std::cerr << "[CMD] No .cmd file found for: " << arg << "\n";
        return false;
    }

    std::string label = src.from_zip()
        ? (src.zip_path + "!" + src.entry)
        : src.entry;
    std::cout << "[CMD] Loading: " << label << "\n";

    std::vector<uint8_t> buf = read_source_bytes(src);
    if (buf.empty()) {
        std::cerr << "[CMD] Could not read: " << label << "\n";
        return false;
    }

    bool ok = parse_and_load_cmd(buf, bus, cpu, label);
    if (ok) {
        cmd_loaded_ = true;
        cmd_source_ = src;
    }
    return ok;
}

// ============================================================================
// Phase 2 — RST 28h SVC intercept for runtime overlay loading
// ============================================================================
//
// LDOS SVC ABI (public interface, from LDOS 5 manual):
//   RST  28h        ; push PC+1, jump to 0x0028 → patched by LDOS to 0x400C → 0x4BCD
//   DEFB svc#       ; SVC number follows immediately (caller's "return address" points here)
//
// Registers used by file SVCs:
//   @OPEN  (0x1C): HL = FCB address; FCB+0..7 = filename, FCB+9..10 = ext
//                  DE = password; B = drive#; A = access mode; returns A=0 ok
//   @CLOSE (0x1A): HL = FCB address; returns A=0 ok
//   @READ  (0x21): HL = FCB; DE = buffer address; BC = byte count; returns A=0 ok
//   @LOAD  (0x26): HL = FCB (name of CMD file to load); returns A=0/NZ=error
//
// Without LDOS present, 0x0028 contains ROM code we must NOT execute.
// We intercept PC==0x0028 before cpu_.step() in step_frame.

static constexpr uint8_t SVC_CLOSE = 0x1A;
static constexpr uint8_t SVC_OPEN  = 0x1C;
static constexpr uint8_t SVC_READ  = 0x21;
static constexpr uint8_t SVC_LOAD  = 0x26;

// Fake a successful SVC return: consume the DEFB byte (advance stacked return
// addr past it) and do a Z80 RET from the RST.
static void svc_return_ok(Z80& cpu, Bus& bus, uint8_t result_a = 0) {
    // Stacked return address points at the DEFB byte.
    // Advance it by 1 so the program resumes after the DEFB.
    uint16_t sp  = cpu.get_sp();
    uint16_t ret = static_cast<uint16_t>(bus.peek(sp) | (bus.peek(sp + 1) << 8));
    ret++;
    bus.write(sp,     ret & 0xFF);
    bus.write(sp + 1, ret >> 8);
    // Pop the return address into PC (fake RET).
    cpu.set_sp(sp + 2);
    cpu.set_pc(ret);
    cpu.set_a(result_a);
    // Note: LDOS programs check A=0 for success; we don't set carry here
    // since Z80 flag setters are internal.  Add set_f() to z80.hpp if needed.
}

// Read an 8-character LDOS filename from HL (FCB offset 0..7) + extension
// (FCB offset 9..10) and return "NAME/EXT" trimmed.
static std::string read_fcb_name(uint16_t hl, const Bus& bus) {
    char name[9] = {}, ext[4] = {};
    for (int i = 0; i < 8; i++) {
        char c = (char)bus.peek(hl + (uint16_t)i);
        if (c == ' ' || c == 0) break;
        name[i] = c;
    }
    for (int i = 0; i < 3; i++) {
        char c = (char)bus.peek(hl + 9 + (uint16_t)i);
        if (c == ' ' || c == 0) break;
        ext[i] = c;
    }
    std::string s(name);
    if (ext[0]) s = s + "/" + ext;
    return s;
}

void SoftwareLoader::on_svc_entry(Z80& cpu, Bus& bus) {
    // The stacked return address points at the DEFB immediately after RST 28h.
    uint16_t sp    = cpu.get_sp();
    uint16_t retpc = static_cast<uint16_t>(bus.peek(sp) | (bus.peek(sp + 1) << 8));
    uint8_t  svc   = bus.peek(retpc);

    switch (svc) {

    case SVC_OPEN: {
        // Find a free VFile slot.
        int slot = -1;
        for (int i = 0; i < 4; i++) if (!vfiles_[i]) { slot = i; break; }
        if (slot < 0) {
            std::cerr << "[SVC] @OPEN: no free handle slots\n";
            svc_return_ok(cpu, bus, 0xFE);  // error
            return;
        }
        uint16_t hl  = cpu.get_hl();
        std::string name = read_fcb_name(hl, bus);
        std::vector<uint8_t> data = find_sibling_bytes(name);
        if (data.empty()) {
            // Try with /CMD extension appended
            data = find_sibling_bytes(name + "/CMD");
            if (data.empty()) {
                std::cerr << "[SVC] @OPEN: not found: " << name << "\n";
                svc_return_ok(cpu, bus, 0xFE);
                return;
            }
        }
        vfiles_[slot] = VFile{ name, std::move(data), 0 };
        std::cout << "[SVC] @OPEN '" << name << "' → handle " << slot << "\n";
        // Store handle index into FCB+0xB (attribute byte) for @READ/@CLOSE
        bus.write(hl + 0x0B, (uint8_t)slot);
        svc_return_ok(cpu, bus, 0);
        break;
    }

    case SVC_CLOSE: {
        uint16_t hl   = cpu.get_hl();
        uint8_t  slot = bus.peek(hl + 0x0B) & 0x03;
        if (vfiles_[slot]) {
            std::cout << "[SVC] @CLOSE handle " << (int)slot
                      << " ('" << vfiles_[slot]->name << "')\n";
            vfiles_[slot].reset();
        }
        svc_return_ok(cpu, bus, 0);
        break;
    }

    case SVC_READ: {
        uint16_t hl   = cpu.get_hl();
        uint8_t  slot = bus.peek(hl + 0x0B) & 0x03;
        uint16_t dest = cpu.get_de();
        uint16_t bc   = cpu.get_bc();
        if (!vfiles_[slot]) {
            std::cerr << "[SVC] @READ: invalid handle " << (int)slot << "\n";
            svc_return_ok(cpu, bus, 0xFE);
            return;
        }
        VFile& vf = *vfiles_[slot];
        size_t avail = vf.data.size() - vf.pos;
        size_t n     = std::min((size_t)bc, avail);
        for (size_t j = 0; j < n; j++)
            bus.write(static_cast<uint16_t>(dest + j), vf.data[vf.pos + j]);
        vf.pos += n;
        // BC = bytes not transferred (0 = all read); we can't set BC directly
        // so we rely on the program checking A=0 for success.
        svc_return_ok(cpu, bus, 0);
        break;
    }

    case SVC_LOAD: {
        // Load and execute a CMD overlay.
        uint16_t hl  = cpu.get_hl();
        std::string name = read_fcb_name(hl, bus);
        // Ensure it has a /CMD extension for lookup
        if (to_lower(name).find("/cmd") == std::string::npos)
            name += "/CMD";
        std::vector<uint8_t> data = find_sibling_bytes(name);
        if (data.empty()) {
            std::cerr << "[SVC] @LOAD: not found: " << name << "\n";
            svc_return_ok(cpu, bus, 0xFE);
            return;
        }
        std::cout << "[SVC] @LOAD '" << name << "'\n";
        // Parse and load — if it has a transfer address the Z80 PC will be
        // redirected; if not (0x03 overlay), we just load and return normally.
        bool ok = parse_and_load_cmd(data, bus, cpu, name);
        if (ok) {
            // Transfer address was set — the loaded module will run directly.
            // Pop the RST 28h frame off the stack so we don't double-return.
            cpu.set_sp(cpu.get_sp() + 2);
        } else {
            // Overlay (no exec addr) — return to caller after loading.
            svc_return_ok(cpu, bus, 0);
        }
        break;
    }

    default: {
        // Unhandled SVC — fake success and log once.
        if (svc < 32 && !(svc_unhandled_logged_ & (1u << svc))) {
            svc_unhandled_logged_ |= (1u << svc);
            fprintf(stderr, "[SVC] Unhandled SVC 0x%02X — faking success\n", svc);
        } else if (svc >= 32) {
            fprintf(stderr, "[SVC] Unhandled SVC 0x%02X — faking success\n", svc);
        }
        svc_return_ok(cpu, bus, 0);
        break;
    }
    }
}


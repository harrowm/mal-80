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

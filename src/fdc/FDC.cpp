// src/fdc/FDC.cpp
// FD1771 Floppy Disk Controller — see FDC.hpp for architecture notes.
#include "FDC.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

// ============================================================================
// STATUS REGISTER BIT MASKS
// ============================================================================
static constexpr uint8_t ST_BUSY     = 0x01;  // Command in progress
static constexpr uint8_t ST_DRQ      = 0x02;  // Data request (type II/III) / Index (type I)
static constexpr uint8_t ST_TRACK0   = 0x04;  // Head on track 0 (type I)
static constexpr uint8_t ST_RNF      = 0x10;  // Record not found (type II/III)
static constexpr uint8_t ST_RECTYPE  = 0x20;  // Record type: deleted data mark (type II/III)
static constexpr uint8_t ST_NOTREADY = 0x80;  // No disk in drive

// ============================================================================
// DISK IMAGE LOADING
// ============================================================================
bool FDC::load_disk(int drive, const std::string& path) {
    if (drive < 0 || drive >= DRIVES) {
        std::cerr << "[FDC] Invalid drive index: " << drive << "\n";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "[FDC] Cannot open disk image: " << path << "\n";
        return false;
    }
    f.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    drives_[drive].image.resize(size);
    f.read(reinterpret_cast<char*>(drives_[drive].image.data()),
           static_cast<std::streamsize>(size));
    drives_[drive].loaded     = true;
    disk_names_[drive]        = path;
    drives_[drive].head_track = 0;
    drives_[drive].tracks     = static_cast<int>(size / (SECTORS_PER_TRACK * BYTES_PER_SECTOR));
    // FD1771 power-on state: head on track 0, drive ready.
    // The Level II ROM checks: LD A,(0x37EC); INC A; CP 2; JP C, no_disk
    // Status 0x00 is treated same as 0xFF (no FDC). Must be >= 0x01.
    // A real FD1771 after reset with disk shows TRACK0 (bit 2) = 0x04.
    status_ = ST_TRACK0;
    std::cout << "[FDC] Drive " << drive << ": loaded " << path
              << " (" << size << " bytes, "
              << drives_[drive].tracks << " tracks)\n";
    return true;
}

// ============================================================================
// PRESENCE DETECTION
// ============================================================================
bool FDC::is_present() const {
    for (const auto& d : drives_)
        if (d.loaded) return true;
    return false;
}

// ============================================================================
// DRIVE SELECTION
// ============================================================================
int FDC::current_drive() const {
    // Bits 0-2 select drives 0-2; bit 3 = side select (not a drive number).
    // If no drive bits are set (e.g. after a motor-off deselect write),
    // fall back to the last explicitly-selected drive so that FDC commands
    // continue to work — the real motor keeps spinning after deselect.
    for (int i = 0; i < 3; i++)
        if (drive_sel_ & (1 << i)) return i;
    return last_drive_;
}

FDC::Drive* FDC::active_drive() {
    int idx = current_drive();
    if (idx < 0 || !drives_[idx].loaded) return nullptr;
    return &drives_[idx];
}

// ============================================================================
// JV1 SECTOR ACCESS
// ============================================================================
std::vector<uint8_t> FDC::Drive::read_sector(int track, int sector) const {
    std::vector<uint8_t> out(BYTES_PER_SECTOR, 0x00);
    size_t offset = static_cast<size_t>((track * SECTORS_PER_TRACK + sector)
                                        * BYTES_PER_SECTOR);
    if (offset + BYTES_PER_SECTOR <= image.size())
        std::copy(image.begin() + static_cast<ptrdiff_t>(offset),
                  image.begin() + static_cast<ptrdiff_t>(offset + BYTES_PER_SECTOR),
                  out.begin());
    return out;
}

bool FDC::Drive::write_sector(int track, int sector,
                               const std::array<uint8_t, BYTES_PER_SECTOR>& data) {
    size_t offset = static_cast<size_t>((track * SECTORS_PER_TRACK + sector)
                                        * BYTES_PER_SECTOR);
    if (offset + BYTES_PER_SECTOR > image.size()) {
        // Extend image if needed (e.g. formatting a larger disk)
        image.resize(offset + BYTES_PER_SECTOR, 0x00);
    }
    std::copy(data.begin(), data.end(),
              image.begin() + static_cast<ptrdiff_t>(offset));
    return true;
}

// ============================================================================
// REGISTER READ
// ============================================================================
uint8_t FDC::read(uint16_t addr) {
    switch (addr) {

    case 0x37EC:   // Status register — reading clears INTRQ
        intrq_ = false;
        return status_;

    case 0x37ED:
        return track_;

    case 0x37EE:
        return sector_;

    case 0x37EF: {  // Data register — drives the byte-by-byte transfer
        if (buf_len_ > 0 && buf_pos_ < buf_len_) {
            data_ = buf_[buf_pos_++];
            if (buf_pos_ == 1) sector_write_flag_ = true;  // first byte consumed — next RAM write is the sector destination
            if (buf_pos_ >= buf_len_) {
                // All bytes delivered — command complete
                buf_len_  = 0;
                status_  &= static_cast<uint8_t>(~(ST_BUSY | ST_DRQ));
                intrq_    = true;
            }
            return data_;
        }
        return data_;
    }

    default:
        return 0xFF;
    }
}

// ============================================================================
// REGISTER WRITE
// ============================================================================
void FDC::write(uint16_t addr, uint8_t val) {
    switch (addr) {

    case 0x37E0: case 0x37E1: case 0x37E2: case 0x37E3:
        // Drive select latch (Bus also calls select_drive(), but handle here too)
        select_drive(val);
        break;

    case 0x37EC:   // Command register
        execute_command(val);
        break;

    case 0x37ED:
        track_ = val;
        break;

    case 0x37EE:
        sector_ = val;
        break;

    case 0x37EF:   // Data register write (Write Sector accumulation)
        data_ = val;
        if (write_pending_ && buf_len_ > 0 && buf_pos_ < buf_len_) {
            buf_[static_cast<size_t>(buf_pos_++)] = val;
            if (buf_pos_ >= buf_len_) {
                // All bytes received — commit sector to disk image
                Drive* d = active_drive();
                if (d) d->write_sector(write_track_, write_sector_, buf_);
                buf_len_       = 0;
                write_pending_ = false;
                status_       &= static_cast<uint8_t>(~(ST_BUSY | ST_DRQ));
                intrq_         = true;
            }
        }
        break;

    default:
        break;
    }
}

// ============================================================================
// COMMAND DISPATCH
// ============================================================================
void FDC::execute_command(uint8_t cmd) {
    // Cancel any in-progress transfer
    buf_len_       = 0;
    buf_pos_       = 0;
    write_pending_ = false;
    intrq_         = false;

    static const char* cmd_names[] = {
        "Restore","Seek","Step","StepU","StepIn","StepInU","StepOut","StepOutU",
        "ReadSec","ReadSecM","WriteSec","WriteSecM","ReadAddr","ForceInt","ReadTrk","WriteTrk"
    };
    fprintf(stderr, "[FDC] cmd=0x%02X (%s) T%02d/S%d drv=%d\n",
            cmd, cmd_names[cmd >> 4], track_, sector_, current_drive());

    uint8_t type = cmd >> 4;
    switch (type) {
    case 0x0:                          cmd_restore(cmd);              break;
    case 0x1:                          cmd_seek(cmd);                 break;
    case 0x2:  cmd_step(cmd, last_dir_, false);                       break;
    case 0x3:  cmd_step(cmd, last_dir_, true);                        break;
    case 0x4:  cmd_step(cmd, +1, false);                              break;
    case 0x5:  cmd_step(cmd, +1, true);                               break;
    case 0x6:  cmd_step(cmd, -1, false);                              break;
    case 0x7:  cmd_step(cmd, -1, true);                               break;
    case 0x8:
    case 0x9:                          cmd_read_sector(cmd);          break;
    case 0xA:
    case 0xB:                          cmd_write_sector(cmd);         break;
    case 0xC:                          cmd_read_address(cmd);         break;
    case 0xD:                          cmd_force_interrupt(cmd);      break;
    // 0xE = Read Track, 0xF = Write Track — not needed for boot/run
    default:                           cmd_force_interrupt(0xD0);     break;
    }
    // (result logged per-command for interesting cases only)
}

// ============================================================================
// TYPE I COMMANDS — HEAD POSITIONING
// ============================================================================

void FDC::cmd_restore(uint8_t /*cmd*/) {
    Drive* d = active_drive();
    if (!d) { status_ = ST_NOTREADY; intrq_ = true; return; }

    d->head_track = 0;
    track_        = 0;
    status_       = ST_TRACK0;
    intrq_        = true;
}

void FDC::cmd_seek(uint8_t /*cmd*/) {
    Drive* d = active_drive();
    if (!d) { status_ = ST_NOTREADY; intrq_ = true; return; }

    int target = data_;
    if (target < 0)           target = 0;
    if (target >= d->tracks)  target = d->tracks - 1;

    last_dir_     = (target > d->head_track) ? +1 : -1;
    d->head_track = target;
    track_        = static_cast<uint8_t>(target);
    status_       = (track_ == 0) ? ST_TRACK0 : 0x00;
    intrq_        = true;
}

void FDC::cmd_step(uint8_t /*cmd*/, int dir, bool update_track) {
    Drive* d = active_drive();
    if (!d) { status_ = ST_NOTREADY; intrq_ = true; return; }

    last_dir_ = dir;
    int next  = d->head_track + dir;
    if (next < 0)          next = 0;
    if (next >= d->tracks) next = d->tracks - 1;

    d->head_track = next;
    if (update_track) track_ = static_cast<uint8_t>(next);

    status_ = (d->head_track == 0) ? ST_TRACK0 : 0x00;
    intrq_  = true;
}

// ============================================================================
// TYPE II COMMANDS — SECTOR READ / WRITE
// ============================================================================

void FDC::cmd_read_sector(uint8_t /*cmd*/) {
    Drive* d = active_drive();
    if (!d) { status_ = ST_NOTREADY; intrq_ = true; return; }

    int t = d->head_track;
    int s = sector_;

    if (s >= SECTORS_PER_TRACK || t >= d->tracks) {
        fprintf(stderr, "[FDC] RNF! head_track=%d sector_=%d (sectors_per_track=%d tracks=%d)\n",
                t, s, SECTORS_PER_TRACK, d->tracks);
        status_ = ST_RNF;
        intrq_  = true;
        return;
    }

    auto bytes = d->read_sector(t, s);
    std::copy(bytes.begin(), bytes.end(), buf_.begin());
    buf_pos_ = 0;
    buf_len_ = BYTES_PER_SECTOR;

    last_read_track_  = t;
    last_read_sector_ = s;

    // Track 17 on Model I TRSDOS/LDOS: directory entry sectors (2+) use a
    // deleted data address mark; GAT (sector 0) and HIT (sector 1) use a normal mark.
    // On data tracks: LDOS SYS module *header* sectors begin with 0x1F (the copyright
    // block length-prefix byte) and were formatted with deleted data marks on real
    // hardware.  The FD1771 reports the mark type in bit 5 (RECTYPE=1 for deleted).
    // LDOS uses RECTYPE to distinguish header sectors (parse module descriptor) from
    // code sectors (copy raw bytes to load address).  JV1 images have no per-sector
    // metadata, so we infer it from the content.
    // Note: Track 0 sectors (boot loader, secondary boot) use NORMAL data marks —
    // the second-stage at 0x4777 uses RECTYPE=0 to indicate "module code to copy".
    bool deleted = ((t == 17) && (s >= 2))
                || ((t != 17) && (bytes[0] == 0x1F));
    status_ = ST_BUSY | ST_DRQ | (deleted ? ST_RECTYPE : 0x00);

    // Log sector type and inferred load address for every sector read.
    if (t == 17) {
        fprintf(stderr, "[SEC] T%02d/S%d DIR    bytes[0..3]=0x%02X 0x%02X 0x%02X 0x%02X\n",
                t, s, bytes[0], bytes[1], bytes[2], bytes[3]);
    } else if (bytes[0] == 0x1F) {
        // Module header: 0x1F, len, name[8], load_hi, load_lo, ...
        // LDOS SYS header layout: [0]=0x1F [1]=name_len [2..9]=name [10]=load_hi [11]=load_lo
        char name[9] = {};
        for (int i = 0; i < 8; i++) {
            uint8_t c = bytes[i + 2];
            name[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        uint16_t load = (uint16_t)(bytes[11] | (bytes[10] << 8));
        fprintf(stderr, "[SEC] T%02d/S%d HEADER  name=\"%.8s\"  load=0x%04X  len=0x%02X  RECTYPE=%d\n",
                t, s, name, load, bytes[1], deleted ? 1 : 0);
    } else {
        // Code sector: bytes[0..1] = LE load address
        uint16_t load = (uint16_t)(bytes[0] | (bytes[1] << 8));
        const char* region = (load > 0x7FFF) ? "HIGH" : "low ";
        fprintf(stderr, "[SEC] T%02d/S%d CODE    load=0x%04X [%s]  data[0..3]=0x%02X 0x%02X 0x%02X 0x%02X\n",
                t, s, load, region, bytes[2], bytes[3], bytes[4], bytes[5]);
    }
}

void FDC::cmd_write_sector(uint8_t /*cmd*/) {
    Drive* d = active_drive();
    if (!d) { status_ = ST_NOTREADY; intrq_ = true; return; }

    int t = d->head_track;
    int s = sector_;

    if (s >= SECTORS_PER_TRACK || t >= d->tracks) {
        fprintf(stderr, "[FDC] WriteSec RNF! head_track=%d sector_=%d\n", t, s);
        status_ = ST_RNF;
        intrq_  = true;
        return;
    }
    fprintf(stderr, "[FDC] WriteSec T%02d/S%d starting...\n", t, s);

    write_pending_  = true;
    write_track_    = t;
    write_sector_   = s;
    buf_pos_        = 0;
    buf_len_        = BYTES_PER_SECTOR;
    status_         = ST_BUSY | ST_DRQ;
}

// ============================================================================
// TYPE III — READ ADDRESS
// ============================================================================
// Returns the 6-byte sector ID for the next encountered sector header.
// LDOS uses this to verify head position after a seek.
void FDC::cmd_read_address(uint8_t /*cmd*/) {
    Drive* d = active_drive();
    if (!d) { status_ = ST_NOTREADY; intrq_ = true; return; }

    // Synthesise an ID field for the current head position.
    // The FD1771 also updates the sector register with the sector number found.
    uint8_t trk = static_cast<uint8_t>(d->head_track);
    uint8_t sec = sector_;

    buf_[0] = trk;    // Track
    buf_[1] = 0x00;   // Side 0
    buf_[2] = sec;    // Sector
    buf_[3] = 0x01;   // Length code 1 = 256 bytes
    buf_[4] = 0x00;   // CRC high (fake)
    buf_[5] = 0x00;   // CRC low (fake)
    buf_pos_ = 0;
    buf_len_ = 6;

    // FD1771 behaviour: track register is loaded with the track field from the ID
    track_  = trk;
    status_ = ST_BUSY | ST_DRQ;
}

// ============================================================================
// TYPE IV — FORCE INTERRUPT
// ============================================================================
void FDC::cmd_force_interrupt(uint8_t cmd) {
    // Abort any in-progress operation (already done in execute_command)
    status_ &= static_cast<uint8_t>(~(ST_BUSY | ST_DRQ));

    // Bit 3 set: generate INTRQ immediately
    if (cmd & 0x08) {
        intrq_ = true;
    }
    // Bits 0-2 relate to index pulses / ready transitions — not needed
}

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
    // FD1771 power-on state: head on track 0, motor not yet running.
    // The Level II ROM checks: LD A,(0x37EC); INC A; CP 2; JP C, no_disk
    // Status 0x00 is treated same as 0xFF (no FDC). Must be >= 0x01.
    // xtrs initialises to TRSDISK_NOTRDY|TRSDISK_TRKZERO = 0x84:
    // NOTREADY (bit 7) reflects motor not yet up to speed; TRACK0 (bit 2)
    // reflects head position.  NOTREADY is cleared by the first successful
    // Type I command (Restore/Seek/Step) once the motor has started.
    status_ = ST_NOTREADY | ST_TRACK0;
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
    // Log first 32 bytes of write for debugging
    fprintf(stderr, "[FDC] WriteSec T%02d/S%d data:", track, sector);
    for (int i = 0; i < 32; i++) fprintf(stderr, " %02X", data[i]);
    fprintf(stderr, "\n");
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

    uint8_t type = cmd >> 4;
    {
        Drive* tmp = active_drive();
        int t = tmp ? tmp->head_track : -1;
        switch (type) {
        case 0x0: fprintf(stderr, "[FDC] Restore\n"); break;
        case 0x1: fprintf(stderr, "[FDC] Seek -> T%02d\n", (int)data_); break;
        case 0x2: case 0x3: fprintf(stderr, "[FDC] Step(T%02d)\n", t); break;
        case 0x4: case 0x5: fprintf(stderr, "[FDC] StepIn(T%02d)\n", t); break;
        case 0x6: case 0x7: fprintf(stderr, "[FDC] StepOut(T%02d)\n", t); break;
        case 0x8: case 0x9: /* logged in cmd_read_sector with sector data */ break;
        case 0xA: case 0xB: fprintf(stderr, "[FDC] WriteSec T%02d/S%d\n", t, (int)sector_); break;
        case 0xC: fprintf(stderr, "[FDC] ReadAddr T%02d (sector_=%d)\n", t, (int)sector_); break;
        case 0xD: fprintf(stderr, "[FDC] ForceInt 0x%02X\n", cmd); break;
        default:  fprintf(stderr, "[FDC] Unknown cmd 0x%02X\n", cmd); break;
        }
    }
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
    // Motor is now running — clear NOTREADY.  Head is at track 0 → TRACK0.
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
    // Motor is now running — clear NOTREADY.
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

    // Motor is now running — clear NOTREADY.
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
        status_ = ST_RNF;
        intrq_  = true;
        return;
    }

    auto bytes = d->read_sector(t, s);
    std::copy(bytes.begin(), bytes.end(), buf_.begin());
    buf_pos_ = 0;
    buf_len_ = BYTES_PER_SECTOR;

    // Log in xtrs-compatible format for comparison
    bool deleted = (t == 17);
    fprintf(stderr, "[FDC-mal80] ReadSector drv=%d trk=%d sec=%d rectype=%d pc=0x%04X bytes:",
            current_drive(), t, s, (int)deleted, (unsigned)last_pc_);
    for (int i = 0; i < 16; i++) fprintf(stderr, " %02x", bytes[i]);
    fprintf(stderr, "\n");

    last_read_track_  = t;
    last_read_sector_ = s;

    // JV1 format: track 17 (directory track) is formatted with FA data address
    // marks on ALL sectors (S0 GAT through S9).  All other tracks use FB (normal).
    // This matches xtrs behaviour and is required by LDOS's module loader, which
    // checks RECTYPE to distinguish directory-track sectors from data sectors.
    status_ = ST_BUSY | ST_DRQ | (deleted ? ST_RECTYPE : 0x00);

}

void FDC::cmd_write_sector(uint8_t /*cmd*/) {
    Drive* d = active_drive();
    if (!d) { status_ = ST_NOTREADY; intrq_ = true; return; }

    int t = d->head_track;
    int s = sector_;

    if (s >= SECTORS_PER_TRACK || t >= d->tracks) {
        status_ = ST_RNF;
        intrq_  = true;
        return;
    }

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

    // Synthesise an ID field for the "next" sector at the current head position.
    // JV1 sectors are physically laid out using this interleave on a real disk.
    // LDOS uses Read Address to verify track position after a seek; it reads
    // back the track byte and ignores rotation-dependent sector ordering.
    static constexpr uint8_t JV1_INTERLEAVE[10] = {0,5,1,6,2,7,3,8,4,9};
    uint8_t trk = static_cast<uint8_t>(d->head_track);
    // Return the logically-next interleaved sector.  We use sector_ as the
    // index into the interleave table so successive Read Address calls cycle
    // through realistic sector IDs without needing rotation simulation.
    uint8_t sec = JV1_INTERLEAVE[sector_ % SECTORS_PER_TRACK];

    buf_[0] = trk;    // Track
    buf_[1] = 0x00;   // Side 0
    buf_[2] = sec;    // Sector
    buf_[3] = 0x01;   // Length code 1 = 256 bytes
    buf_[4] = 0x00;   // CRC high (fake)
    buf_[5] = 0x00;   // CRC low (fake)
    buf_pos_ = 0;
    buf_len_ = 6;

    // FD1771 (P1771 / Model I): track register ← track field from ID.
    // Sector register ← sector field from ID (P1771 datasheet, byte 3 of 6).
    track_  = trk;
    sector_ = sec;
    status_ = ST_BUSY | ST_DRQ;
}

// ============================================================================
// TYPE IV — FORCE INTERRUPT
// ============================================================================
void FDC::cmd_force_interrupt(uint8_t cmd) {
    // Abort any in-progress operation (already done in execute_command).
    // After Force Interrupt the FD1771 presents Type I status: reflect actual
    // head position (TRACK0 bit) and drive ready state.
    Drive* d = active_drive();
    if (!d) {
        status_ = ST_NOTREADY;
    } else {
        status_ = (d->head_track == 0) ? ST_TRACK0 : 0x00;
    }

    // Bit 3: generate INTRQ immediately
    if (cmd & 0x08)
        intrq_ = true;
    // Bits 0-2 relate to index pulses / ready transitions — not needed
}

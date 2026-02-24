// src/fdc/FDC.hpp
// FD1771 Floppy Disk Controller emulation for TRS-80 Model I
//
// Registers are memory-mapped (not I/O ports):
//   0x37E0-0x37E3  Drive select latch (write) / IRQ status (read, handled by Bus)
//   0x37EC         Command (write) / Status (read)   — read clears INTRQ
//   0x37ED         Track register
//   0x37EE         Sector register
//   0x37EF         Data register
//
// Interrupt routing: INTRQ → Z80 /INT (IM1, RST 38h), same line as 60Hz timer.
// Bus::interrupt_pending() combines both sources; reading 0x37E0 exposes which
// fired (bit7=timer, bit6=FDC). DRQ is polled by software; no /WAIT hardware.
//
// JV1 disk image format: flat array of 256-byte sectors in track-major order.
//   offset = (track × SECTORS_PER_TRACK + sector) × BYTES_PER_SECTOR
// Supports up to 4 drives. TRSDOS/LDOS standard: 35 tracks, 10 sectors, SS/SD.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class FDC {
public:
    static constexpr int DRIVES          = 4;
    static constexpr int SECTORS_PER_TRACK = 10;
    static constexpr int BYTES_PER_SECTOR  = 256;
    static constexpr int MAX_TRACKS        = 35;   // standard TRSDOS disk

    // Load a JV1 .dsk image into drive slot 0-3.  Returns false on error.
    bool load_disk(int drive, const std::string& path);

    // Memory-mapped register access called from Bus::read()/write().
    // addr range: 0x37EC-0x37EF  (drive select 0x37E0-0x37E3 handled by Bus)
    uint8_t read(uint16_t addr);
    void    write(uint16_t addr, uint8_t val);

    // Drive select latch write (0x37E0-0x37E3): bits 0-2 select drives 0-2.
    // Bit 3 = side select (ignored for single-sided JV1).
    void select_drive(uint8_t val) {
        drive_sel_ = val;
        // Remember the last explicitly-selected drive so FDC commands continue
        // working after the host deselects drives for motor control (bits 0-2 = 0).
        // On real hardware the motor keeps spinning and the FDC stays responsive.
        for (int i = 0; i < DRIVES; i++)
            if (val & (1 << i)) { last_drive_ = i; break; }
    }

    // INTRQ flag — combined into Bus::interrupt_pending().
    // Set when a command completes; cleared when FDC status (0x37EC) is read.
    bool intrq_pending() const { return intrq_; }

    // True if any drive has a disk loaded (used for expansion-interface detection).
    bool is_present() const;

private:
    // =========================================================================
    // DRIVE STATE
    // =========================================================================
    struct Drive {
        std::vector<uint8_t> image;
        int  head_track = 0;
        bool loaded     = false;

        std::vector<uint8_t> read_sector(int track, int sector) const;
        bool write_sector(int track, int sector,
                          const std::array<uint8_t, BYTES_PER_SECTOR>& data);
    };
    std::array<Drive, DRIVES> drives_;

    // =========================================================================
    // FD1771 REGISTERS
    // =========================================================================
    uint8_t status_  = 0;   // Status register (read-only externally)
    uint8_t track_   = 0;   // Track register
    uint8_t sector_  = 0;   // Sector register
    uint8_t data_    = 0;   // Data register

    // =========================================================================
    // CONTROLLER STATE
    // =========================================================================
    uint8_t drive_sel_ = 0;   // Drive select latch
    int     last_drive_ = 0;  // Last explicitly-selected drive (sticky after deselect)

    // Sector transfer buffer — shared by Read Sector, Write Sector, Read Address
    std::array<uint8_t, BYTES_PER_SECTOR> buf_{};
    int  buf_pos_  = 0;
    int  buf_len_  = 0;

    // Write Sector commit target (set when Write Sector command is issued)
    bool write_pending_  = false;
    int  write_track_    = 0;
    int  write_sector_   = 0;

    bool intrq_ = false;   // Interrupt request pending

    int last_dir_ = 1;     // Last step direction (+1 = in, -1 = out)

    // =========================================================================
    // HELPERS
    // =========================================================================
    int    current_drive() const;   // Index of selected drive, or -1
    Drive* active_drive();          // Pointer to selected drive, or nullptr

    // =========================================================================
    // COMMAND HANDLERS
    // =========================================================================
    void execute_command(uint8_t cmd);

    // Type I — head positioning
    void cmd_restore(uint8_t cmd);
    void cmd_seek(uint8_t cmd);
    void cmd_step(uint8_t cmd, int dir, bool update_track);

    // Type II — sector read/write
    void cmd_read_sector(uint8_t cmd);
    void cmd_write_sector(uint8_t cmd);

    // Type III — address/track
    void cmd_read_address(uint8_t cmd);

    // Type IV — force interrupt
    void cmd_force_interrupt(uint8_t cmd);
};

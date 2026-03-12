// src/video/Display.hpp
#pragma once
#include <SDL.h>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

// Forward declaration
class Bus;

// ============================================================================
// TRS-80 VIDEO CONSTANTS
// ============================================================================
constexpr uint16_t TRS80_CHARS_PER_LINE    = 64;
constexpr uint16_t TRS80_CHAR_LINES        = 16;
constexpr uint16_t TRS80_VRAM_SIZE         = 1024;  // 64 × 16

constexpr uint16_t CHAR_CELL_W             = 6;   // pixels wide per cell
constexpr uint16_t CHAR_CELL_H             = 12;  // pixels tall per cell

constexpr uint16_t TRS80_WIDTH             = TRS80_CHARS_PER_LINE * CHAR_CELL_W;  // 384
constexpr uint16_t TRS80_HEIGHT            = TRS80_CHAR_LINES * CHAR_CELL_H;      // 192

constexpr uint16_t WINDOW_SCALE            = 3;
constexpr uint16_t WINDOW_WIDTH            = TRS80_WIDTH  * WINDOW_SCALE;  // 1152
constexpr uint16_t WINDOW_HEIGHT           = TRS80_HEIGHT * WINDOW_SCALE;  // 576

constexpr uint16_t CHAR_GEN_CHARS          = 128;
constexpr uint16_t CHAR_GEN_BYTES_PER_CHAR = 8;

// ============================================================================
// PHOSPHOR COLOUR PRESETS  (ARGB8888)
//   0 = white phosphor  1 = amber (P3)  2 = green (P1)
// ============================================================================
inline constexpr uint32_t PHOSPHOR_FG[3] = { 0xFFFFFFFF, 0xFFFFB000, 0xFF33FF00 };
inline constexpr uint32_t PHOSPHOR_BG[3] = { 0xFF000000, 0xFF0A0500, 0xFF001400 };

// Legacy named colour constants (kept for source compatibility)
constexpr uint32_t COLOR_BLACK = 0xFF000000;
constexpr uint32_t COLOR_GREEN = 0xFF33FF00;
constexpr uint32_t COLOR_AMBER = 0xFFFFBF00;

// ============================================================================
// PENDING ACTIONS
// Actions that need Bus/CPU access are reported back to the Emulator.
// ============================================================================
enum class DisplayAction {
    NONE,
    SOFT_RESET,       // F10
    HARD_RESET,       // Shift+F10
    MOUNT_DISK,       // Ctrl+0..3  (drive index in pop_action drive_out)
    PASTE_CLIPBOARD,  // Ctrl+V
    DUMP_RAM,         // F11  — dump full 64KB memory map to memdump.bin
};

// ============================================================================
// DISPLAY CLASS
// ============================================================================
class Display {
public:
    Display();
    ~Display();

    bool init(const std::string& title);
    void cleanup();

    // Render a complete frame from VRAM (skips if overlay is active)
    void render_frame(const Bus& bus);

    // Render a single scanline (cycle-accurate path — optional)
    void render_scanline(const Bus& bus, uint16_t scanline);

    // Poll SDL events.  Returns false when the user requests quit.
    bool handle_events(uint8_t* keyboard_matrix);

    void clear_screen();
    void set_title(const std::string& title);
    bool is_running() const { return running; }

    // Consume the oldest pending action.  drive_out is set for MOUNT_DISK.
    DisplayAction pop_action(int& drive_out);

    // Release all active key presses (call on emulator reset).
    void release_all_keys(uint8_t* keyboard_matrix);

    // Character pattern lookup (public so Bus/Loader can use it)
    uint8_t get_char_pattern(uint8_t char_code, uint8_t row) const;

private:
    // =========================================================================
    // SDL STATE
    // =========================================================================
    SDL_Window*   window         = nullptr;
    SDL_Renderer* renderer       = nullptr;
    SDL_Texture*  screen_texture = nullptr;
    bool running = true;

    // =========================================================================
    // FRAMEBUFFER  (384×192, 32-bit ARGB8888)
    // =========================================================================
    std::array<uint32_t, TRS80_WIDTH * TRS80_HEIGHT> framebuffer{};

    // =========================================================================
    // CRT POST-PROCESSING  (operates at window resolution 1152×576)
    // post_buffer_ holds the upscaled+processed frame sent to SDL.
    // crt_mask_    is pre-baked per-pixel multiplier (0-255): scanlines+vignette.
    // =========================================================================
    static constexpr int POST_W = WINDOW_WIDTH;   // 1152
    static constexpr int POST_H = WINDOW_HEIGHT;  // 576

    std::array<uint32_t, POST_W * POST_H> post_buffer_{};
    std::array<uint8_t,  POST_W * POST_H> crt_mask_{};
    bool crt_enabled_ = true;

    void build_crt_mask();   // call once at init (mask is geometry-only)

    // =========================================================================
    // CHARACTER GENERATOR ROM
    // =========================================================================
    std::array<uint8_t, CHAR_GEN_CHARS * CHAR_GEN_BYTES_PER_CHAR> char_generator{};
    void init_char_generator();

    // =========================================================================
    // PHOSPHOR STATE
    // =========================================================================
    int      phosphor_mode_ = 0;
    uint32_t pixel_fg_      = PHOSPHOR_FG[0];  // "on" pixel colour
    uint32_t pixel_bg_      = PHOSPHOR_BG[0];  // "off" pixel colour

    // =========================================================================
    // IN-WINDOW OVERLAY  (help / about)
    // =========================================================================
    std::array<uint32_t, TRS80_WIDTH * TRS80_HEIGHT> overlay_saved_{};
    bool overlay_active_ = false;
    int  overlay_kind_   = 0;  // 1 = help, 2 = about

    void show_overlay(int kind);
    void hide_overlay();

    // =========================================================================
    // PENDING ACTION  (set by hotkey, consumed by Emulator::run)
    // =========================================================================
    DisplayAction pending_action_ = DisplayAction::NONE;
    int           pending_drive_  = -1;

    // =========================================================================
    // RENDERING HELPERS
    // =========================================================================
    void draw_pixel(uint16_t x, uint16_t y, bool on);
    void draw_character(uint16_t char_x, uint16_t char_y, uint8_t char_code);
    void update_texture();

    // Overlay pixel helpers
    void fill_rect_fb(int x, int y, int w, int h, uint32_t color);
    void draw_char_pixels(int px, int py, uint8_t c, uint32_t fg, uint32_t bg);
    void draw_string_pixels(int px, int py, const char* s, uint32_t fg, uint32_t bg);

    // =========================================================================
    // KEYBOARD MATRIX
    // =========================================================================
    uint8_t keyboard_matrix[8]{};
    void init_keyboard_matrix();

    bool physical_shift_held   = false;
    int  synthetic_shift_count = 0;

    struct TRS80KeyMapping {
        uint8_t row;
        uint8_t col;
        int     shift_override;  // 0=none, 1=force on, -1=force off
    };
    std::unordered_map<int, TRS80KeyMapping> active_keys;
};

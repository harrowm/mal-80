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
// The TRS-80 Model I displays 64 characters × 16 lines
// Each character cell is 6 pixels wide × 12 pixels tall
//   (6-wide dot matrix from char ROM × 8 rows, plus 4 rows inter-line gap)
// ============================================================================

constexpr uint16_t TRS80_CHARS_PER_LINE = 64;
constexpr uint16_t TRS80_CHAR_LINES = 16;
constexpr uint16_t TRS80_VRAM_SIZE = 1024;  // 64 × 16 = 1024 bytes

// Character cell dimensions
constexpr uint16_t CHAR_CELL_W = 6;   // 6 pixels wide (bits 5..0 of ROM byte)
constexpr uint16_t CHAR_CELL_H = 12;  // 12 pixels tall (8 ROM rows + 4 blank)

// Logical resolution (what the TRS-80 actually outputs)
constexpr uint16_t TRS80_WIDTH  = TRS80_CHARS_PER_LINE * CHAR_CELL_W;  // 384
constexpr uint16_t TRS80_HEIGHT = TRS80_CHAR_LINES * CHAR_CELL_H;      // 192

// Window resolution (scaled up for modern displays)
constexpr uint16_t WINDOW_SCALE = 3;
constexpr uint16_t WINDOW_WIDTH  = TRS80_WIDTH  * WINDOW_SCALE;  // 1152
constexpr uint16_t WINDOW_HEIGHT = TRS80_HEIGHT * WINDOW_SCALE;  // 576

// Character generator (6-wide dot matrix, stored as 8 bytes per char)
constexpr uint16_t CHAR_GEN_CHARS = 128;
constexpr uint16_t CHAR_GEN_BYTES_PER_CHAR = 8;

// Colors (TRS-80 is monochrome - green/amber on black)
constexpr uint32_t COLOR_BLACK = 0x000000FF;
constexpr uint32_t COLOR_GREEN = 0x00FF00FF;   // RGBA
constexpr uint32_t COLOR_AMBER = 0xFFBF00FF;   // Alternative

class Display {
public:
    Display();
    ~Display();

    // Initialize SDL window and renderer
    bool init(const std::string& title);
    void cleanup();

    // Render a complete frame (called once per 60Hz frame)
    void render_frame(const Bus& bus);

    // Render a single scanline (for cycle-accurate rendering - optional)
    void render_scanline(const Bus& bus, uint16_t scanline);

    // Handle SDL events (keyboard, quit, etc.)
    bool handle_events(uint8_t* keyboard_matrix);

    // Screen control
    void clear_screen();
    void set_title(const std::string& title);
    bool is_running() const { return running; }

    // Get character pattern from built-in character generator
    uint8_t get_char_pattern(uint8_t char_code, uint8_t row) const;

private:
    // =========================================================================
    // SDL STATE
    // =========================================================================
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* screen_texture = nullptr;
    bool running = true;

    // =========================================================================
    // FRAMEBUFFER (384×192 pixels, 32-bit color)
    // =========================================================================
    std::array<uint32_t, TRS80_WIDTH * TRS80_HEIGHT> framebuffer;

    // =========================================================================
    // CHARACTER GENERATOR ROM (128 characters × 8 bytes each)
    // This is the built-in TRS-80 character pattern
    // =========================================================================
    std::array<uint8_t, CHAR_GEN_CHARS * CHAR_GEN_BYTES_PER_CHAR> char_generator;

    // =========================================================================
    // RENDERING HELPERS
    // =========================================================================
    void init_char_generator();
    void draw_pixel(uint16_t x, uint16_t y, bool on);
    void draw_character(uint16_t char_x, uint16_t char_y, uint8_t char_code);
    void update_texture();

    // =========================================================================
    // KEYBOARD MATRIX (64 keys, 8×8 matrix)
    // =========================================================================
    uint8_t keyboard_matrix[8];
    void init_keyboard_matrix();
    
    // Mac-to-TRS-80 shift remapping state
    bool physical_shift_held = false;
    int synthetic_shift_count = 0;
    
    // Track active key mappings so key-up undoes exactly what key-down did
    struct TRS80KeyMapping {
        uint8_t row;
        uint8_t col;
        int shift_override;  // 0=none, 1=forced on, -1=forced off
    };
    std::unordered_map<int, TRS80KeyMapping> active_keys;  // keyed by SDL scancode
};
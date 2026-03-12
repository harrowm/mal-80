// src/video/Display.cpp
#include "Display.hpp"
#include "CharRom.hpp"
#include "../system/Bus.hpp"
#include <iostream>
#include <cstring>

// ============================================================================
// CONSTRUCTION / DESTRUCTION
// ============================================================================

Display::Display() {
    std::memset(keyboard_matrix, 0x00, sizeof(keyboard_matrix));
    init_char_generator();
}

Display::~Display() {
    cleanup();
}

// ============================================================================
// CHARACTER GENERATOR
// ============================================================================

void Display::init_char_generator() {
    static_assert(sizeof(TRS80_CHAR_GEN) == CHAR_GEN_CHARS * CHAR_GEN_BYTES_PER_CHAR,
                  "CharRom size mismatch");
    std::memcpy(char_generator.data(), TRS80_CHAR_GEN, TRS80_CHAR_GEN_SIZE);
}

uint8_t Display::get_char_pattern(uint8_t char_code, uint8_t row) const {
    if (row >= 8) return 0x00;
    // 7-bit lookup.  Chars 0x00-0x1F alias to 0x40-0x5F (control → uppercase).
    // Chars 0x60-0x7F (lowercase) map to uppercase via CharRom.hpp arrangement.
    uint8_t rom_addr  = char_code & 0x7F;
    uint8_t ascii_idx = (rom_addr < 0x20) ? (rom_addr + 0x40) : rom_addr;
    return char_generator[ascii_idx * 8 + row];
}

// ============================================================================
// SDL INITIALISATION
// ============================================================================

bool Display::init(const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL Video Init Failed: " << SDL_GetError() << "\n";
        return false;
    }

    window = SDL_CreateWindow(title.c_str(),
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WINDOW_WIDTH, WINDOW_HEIGHT,
                              SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "SDL Window Creation Failed: " << SDL_GetError() << "\n";
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "SDL Renderer Creation Failed: " << SDL_GetError() << "\n";
        return false;
    }

    screen_texture = SDL_CreateTexture(renderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       POST_W, POST_H);
    if (!screen_texture) {
        std::cerr << "SDL Texture Creation Failed: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    build_crt_mask();
    clear_screen();
    init_keyboard_matrix();

    std::cout << "Display initialized: " << WINDOW_WIDTH << "×" << WINDOW_HEIGHT << "\n";
    return true;
}

void Display::cleanup() {
    if (!window && !renderer && !screen_texture) return;
    if (screen_texture) { SDL_DestroyTexture(screen_texture); screen_texture = nullptr; }
    if (renderer)       { SDL_DestroyRenderer(renderer);      renderer       = nullptr; }
    if (window)         { SDL_DestroyWindow(window);          window         = nullptr; }
    SDL_Quit();
}

// ============================================================================
// FRAMEBUFFER PRIMITIVES
// ============================================================================

void Display::clear_screen() {
    framebuffer.fill(pixel_bg_);
}

void Display::draw_pixel(uint16_t x, uint16_t y, bool on) {
    if (x >= TRS80_WIDTH || y >= TRS80_HEIGHT) return;
    framebuffer[y * TRS80_WIDTH + x] = on ? pixel_fg_ : pixel_bg_;
}

void Display::fill_rect_fb(int x, int y, int w, int h, uint32_t color) {
    for (int py = y; py < y + h; py++) {
        if (py < 0 || py >= TRS80_HEIGHT) continue;
        for (int px = x; px < x + w; px++) {
            if (px < 0 || px >= TRS80_WIDTH) continue;
            framebuffer[py * TRS80_WIDTH + px] = color;
        }
    }
}

// Draw one character cell at pixel position (px, py) with explicit colours.
void Display::draw_char_pixels(int px, int py, uint8_t c, uint32_t fg, uint32_t bg) {
    fill_rect_fb(px, py, CHAR_CELL_W, CHAR_CELL_H, bg);
    for (int row = 0; row < 8; row++) {
        uint8_t pattern = get_char_pattern(c, row);
        for (int col = 0; col < CHAR_CELL_W; col++) {
            bool on  = (pattern >> (5 - col)) & 0x01;
            int  fpx = px + col;
            int  fpy = py + row;
            if (fpx >= 0 && fpx < TRS80_WIDTH && fpy >= 0 && fpy < TRS80_HEIGHT)
                framebuffer[fpy * TRS80_WIDTH + fpx] = on ? fg : bg;
        }
    }
}

void Display::draw_string_pixels(int px, int py, const char* s, uint32_t fg, uint32_t bg) {
    while (*s) {
        draw_char_pixels(px, py, (uint8_t)*s, fg, bg);
        px += CHAR_CELL_W;
        ++s;
    }
}

// ============================================================================
// IN-WINDOW OVERLAY  (help / about)
// Overlay occupies row 1 onward, cols 4-59 (56 chars) of the char grid.
// Height is computed dynamically: title row + nlines content rows + footer row.
// ============================================================================

static constexpr int OVL_COL  = 4;   // left column (char grid)
static constexpr int OVL_ROW  = 1;   // top row (row 1 gives a 1-row top margin)
static constexpr int OVL_COLS = 56;  // width in characters

// Return a string of exactly `width` chars, centred (space-padded).
static std::string center_str(const char* s, int width) {
    int len = (int)std::strlen(s);
    if (len >= width) return std::string(s, (size_t)width);
    int lpad = (width - len) / 2;
    return std::string((size_t)lpad, ' ') + s +
           std::string((size_t)(width - lpad - len), ' ');
}

void Display::show_overlay(int kind) {
    overlay_saved_ = framebuffer;  // snapshot current screen

    int bx = OVL_COL * CHAR_CELL_W;
    int by = OVL_ROW * CHAR_CELL_H;
    // Colour scheme: use the *next* phosphor preset so the overlay is visually
    // distinct from the emulator screen (white screen → amber overlay, etc.)
    int      next_mode = (phosphor_mode_ + 1) % 3;
    uint32_t next_fg   = PHOSPHOR_FG[next_mode];
    uint32_t next_bg   = PHOSPHOR_BG[next_mode];
    uint32_t bar_fg = next_bg;   // dark text on bright bar
    uint32_t bar_bg = next_fg;   // bright bar background
    uint32_t txt_fg = next_fg;   // bright text
    uint32_t txt_bg = next_bg;   // dark body background

    const char*  title;
    const char** lines;
    int          nlines;

    static const char* help_lines[] = {
        "Home         TRS-80 CLEAR  (Ctrl+Left on Mac)",
        "F5           @ key  (always unshifted)",
        "F6           0 key  (always unshifted)",
        "F7           Dump RAM to memdump.bin",
        "F8           Quit",
        "F9           Toggle CRT effects on/off",
        "Shift+F9     Cycle colour  (white/amber/green)",
        "F10          Warm boot  (keeps program in RAM)",
        "Shift+F10    Hard reset  (clears RAM)",
        "Shift+F11    This help",
        "F12          About",
        "Ctrl+V       Paste clipboard",
        "Ctrl+0..3    Mount disk image on drive 0-3",
    };

    static const char* about_lines[] = {
        "",
        "Mal-80: TRS-80 Model I Emulator",
        "",
        "SDL2 / macOS M4 arm64",
        "",
        "Z80 passes all 67 ZEXALL tests",
    };

    if (kind == 1) {
        title  = " Mal-80 Hotkeys ";
        lines  = help_lines;
        nlines = 13;
    } else {
        title  = " About Mal-80 ";
        lines  = about_lines;
        nlines = 6;
    }

    int bw = OVL_COLS * CHAR_CELL_W;
    int bh = (nlines + 2) * CHAR_CELL_H;  // title row + content rows + footer row

    // Fill box background
    fill_rect_fb(bx, by, bw, bh, txt_bg);

    // Title bar (top row of box)
    draw_string_pixels(bx, by,
                       center_str(title, OVL_COLS).c_str(),
                       bar_fg, bar_bg);

    // Content rows  (rows 1 .. nlines within box)
    for (int i = 0; i < nlines; i++) {
        int ty = by + (i + 1) * CHAR_CELL_H;
        int tx = (OVL_COL + 1) * CHAR_CELL_W;  // one char indent from left edge
        draw_string_pixels(tx, ty, lines[i], txt_fg, txt_bg);
    }

    // Footer bar (bottom row of box)
    int fy = by + (nlines + 1) * CHAR_CELL_H;
    draw_string_pixels(bx, fy,
                       center_str("any key to close", OVL_COLS).c_str(),
                       bar_fg, bar_bg);

    overlay_active_ = true;
    overlay_kind_   = kind;
    update_texture();
}

void Display::hide_overlay() {
    if (!overlay_active_) return;
    framebuffer    = overlay_saved_;
    overlay_active_ = false;
    update_texture();
}

// ============================================================================
// PENDING ACTION
// ============================================================================

DisplayAction Display::pop_action(int& drive_out) {
    DisplayAction a = pending_action_;
    drive_out        = pending_drive_;
    pending_action_  = DisplayAction::NONE;
    pending_drive_   = -1;
    return a;
}

// ============================================================================
// CHARACTER / FRAME RENDERING
// ============================================================================

void Display::draw_character(uint16_t char_x, uint16_t char_y, uint8_t char_code) {
    uint16_t pixel_x = char_x * CHAR_CELL_W;
    uint16_t pixel_y = char_y * CHAR_CELL_H;

    if (char_code & 0x80) {
        // 2×3 semigraphic block: bits 0-5 control 6 sub-cells
        for (int block_row = 0; block_row < 3; block_row++) {
            for (int block_col = 0; block_col < 2; block_col++) {
                bool on = (char_code >> (block_row * 2 + block_col)) & 0x01;
                for (int py = 0; py < 4; py++)
                    for (int px = 0; px < 3; px++)
                        draw_pixel(pixel_x + block_col * 3 + px,
                                   pixel_y + block_row * 4 + py, on);
            }
        }
        return;
    }

    // Normal ROM character (rows 0-7; rows 8-11 remain bg from clear_screen)
    for (int row = 0; row < 8; row++) {
        uint8_t pattern = get_char_pattern(char_code, row);
        for (int col = 0; col < CHAR_CELL_W; col++)
            draw_pixel(pixel_x + col, pixel_y + row, (pattern >> (5 - col)) & 0x01);
    }
}

// ============================================================================
// CRT POST-PROCESSING
// ============================================================================

// Pre-bake a per-pixel luminance multiplier (0-255) combining:
//   - Scanlines: every 3rd row (the "gap" between 3× scaled logical rows)
//     is darkened to 65% — a subtle single-pixel dark line per character row.
//   - Vignette: gentle radial falloff (25% darkening at corners).
// This is geometry-only so it never needs rebuilding (colour is applied later).
void Display::build_crt_mask() {
    const float cx = POST_W * 0.5f;
    const float cy = POST_H * 0.5f;
    for (int py = 0; py < POST_H; py++) {
        float sl = (py % WINDOW_SCALE == WINDOW_SCALE - 1) ? 0.65f : 1.0f;
        float dy = (py - cy) / cy;
        for (int px = 0; px < POST_W; px++) {
            float dx  = (px - cx) / cx;
            float vig = 1.0f - (dx * dx + dy * dy) * 0.25f;
            if (vig < 0.0f) vig = 0.0f;
            float m = sl * vig;
            crt_mask_[py * POST_W + px] = static_cast<uint8_t>(m * 255.0f + 0.5f);
        }
    }
}

void Display::update_texture() {
    // Nearest-neighbour 3× upscale: framebuffer (384×192) → post_buffer_ (1152×576)
    for (int py = 0; py < POST_H; py++) {
        const uint32_t* src_row = &framebuffer[(py / WINDOW_SCALE) * TRS80_WIDTH];
        uint32_t*       dst_row = &post_buffer_[py * POST_W];
        for (int px = 0; px < POST_W; px++)
            dst_row[px] = src_row[px / WINDOW_SCALE];
    }

    // Apply CRT mask (scanlines + vignette) if enabled
    if (crt_enabled_) {
        const uint8_t* mask = crt_mask_.data();
        uint32_t*      out  = post_buffer_.data();
        const int      N    = POST_W * POST_H;
        for (int i = 0; i < N; i++) {
            uint32_t p = out[i];
            uint32_t m = mask[i];
            uint32_t r = ((p >> 16) & 0xFF) * m >> 8;
            uint32_t g = ((p >>  8) & 0xFF) * m >> 8;
            uint32_t b = ( p        & 0xFF) * m >> 8;
            out[i] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
        }
    }

    SDL_UpdateTexture(screen_texture, nullptr, post_buffer_.data(),
                      POST_W * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screen_texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void Display::render_frame(const Bus& bus) {
    if (overlay_active_) {
        // Overlay is already painted into framebuffer — just re-present it.
        update_texture();
        return;
    }

    clear_screen();
    for (int line = 0; line < TRS80_CHAR_LINES; line++)
        for (int col = 0; col < TRS80_CHARS_PER_LINE; col++)
            draw_character(col, line,
                           bus.get_vram_byte(line * TRS80_CHARS_PER_LINE + col));
    update_texture();
}

void Display::render_scanline(const Bus& bus, uint16_t scanline) {
    if (scanline >= TRS80_HEIGHT) return;
    uint16_t char_line   = scanline / CHAR_CELL_H;
    uint16_t row_in_cell = scanline % CHAR_CELL_H;

    for (int col = 0; col < TRS80_CHARS_PER_LINE; col++) {
        uint8_t  char_code = bus.get_vram_byte(char_line * TRS80_CHARS_PER_LINE + col);
        uint16_t pixel_x   = col * CHAR_CELL_W;

        if (row_in_cell >= 8) {
            for (int px = 0; px < CHAR_CELL_W; px++)
                draw_pixel(pixel_x + px, scanline, false);
            continue;
        }

        if (char_code & 0x80) {
            int block_row = row_in_cell / 4;
            for (int block_col = 0; block_col < 2; block_col++) {
                bool on = (char_code >> (block_row * 2 + block_col)) & 0x01;
                for (int px = 0; px < 3; px++)
                    draw_pixel(pixel_x + block_col * 3 + px, scanline, on);
            }
        } else {
            uint8_t pattern = get_char_pattern(char_code, row_in_cell);
            for (int dot = 0; dot < CHAR_CELL_W; dot++)
                draw_pixel(pixel_x + dot, scanline, (pattern >> (5 - dot)) & 0x01);
        }
    }
}

// ============================================================================
// KEYBOARD
// ============================================================================

void Display::init_keyboard_matrix() {
    std::memset(keyboard_matrix, 0x00, sizeof(keyboard_matrix));
}

void Display::release_all_keys(uint8_t* km) {
    active_keys.clear();
    physical_shift_held   = false;
    synthetic_shift_count = 0;
    if (km) std::memset(km, 0, 8);
}

void Display::set_title(const std::string& title) {
    if (window) SDL_SetWindowTitle(window, title.c_str());
}

// ============================================================================
// EVENT HANDLING
// ============================================================================

bool Display::handle_events(uint8_t* keyboard_matrix) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
            return false;
        }

        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
            continue;

        bool         pressed = (event.type == SDL_KEYDOWN);
        SDL_Scancode sc      = event.key.keysym.scancode;
        SDL_Keycode  sym     = event.key.keysym.sym;
        bool         shifted = (event.key.keysym.mod & KMOD_SHIFT) != 0;
        bool         ctrl    = (event.key.keysym.mod & KMOD_CTRL)  != 0;

        // ── Overlay dismiss ──────────────────────────────────────────────────
        // Any non-modifier keydown dismisses the overlay without passing the
        // key to the TRS-80.
        if (overlay_active_ && pressed &&
            sc != SDL_SCANCODE_LSHIFT && sc != SDL_SCANCODE_RSHIFT &&
            sc != SDL_SCANCODE_LCTRL  && sc != SDL_SCANCODE_RCTRL  &&
            sc != SDL_SCANCODE_LALT   && sc != SDL_SCANCODE_RALT) {
            hide_overlay();
            continue;
        }

        // ── Emulator hotkeys (key-down, overlay not active) ─────────────────
        if (pressed && !overlay_active_) {

            // Ctrl+V: paste host clipboard text
            if (ctrl && sym == SDLK_v) {
                pending_action_ = DisplayAction::PASTE_CLIPBOARD;
                continue;
            }

            // Ctrl+0..3: mount disk on drive N
            if (ctrl && sym >= SDLK_0 && sym <= SDLK_3) {
                pending_action_ = DisplayAction::MOUNT_DISK;
                pending_drive_  = sym - SDLK_0;
                continue;
            }

            // F12: about overlay
            if (sym == SDLK_F12) {
                show_overlay(2);
                continue;
            }

            // Shift+F11: help overlay
            if (sym == SDLK_F11 && shifted) {
                show_overlay(1);
                continue;
            }

            // F10 / Shift+F10: soft / hard reset
            if (sym == SDLK_F10) {
                pending_action_ = shifted ? DisplayAction::HARD_RESET
                                          : DisplayAction::SOFT_RESET;
                continue;
            }

            // F9 (unshifted): toggle CRT effects on/off
            if (sym == SDLK_F9 && !shifted) {
                crt_enabled_ = !crt_enabled_;
                continue;
            }

            // Shift+F9: cycle phosphor colour  (white → amber → green → …)
            if (sym == SDLK_F9 && shifted) {
                phosphor_mode_ = (phosphor_mode_ + 1) % 3;
                pixel_fg_      = PHOSPHOR_FG[phosphor_mode_];
                pixel_bg_      = PHOSPHOR_BG[phosphor_mode_];
                continue;
            }

            // F7: dump full 64KB memory map to memdump.bin
            if (sym == SDLK_F7 && !shifted) {
                pending_action_ = DisplayAction::DUMP_RAM;
                continue;
            }

            // F8: quit
            if (sym == SDLK_F8) {
                running = false;
                return false;
            }
        }

        // ── Shift key ────────────────────────────────────────────────────────
        if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
            physical_shift_held = pressed;
            if (synthetic_shift_count == 0) {
                if (physical_shift_held) keyboard_matrix[7] |= 0x01;
                else                     keyboard_matrix[7] &= ~0x01;
            }
            continue;
        }

        // ── Key-up: undo exactly what key-down stored ────────────────────────
        if (!pressed) {
            auto it = active_keys.find((int)sc);
            if (it != active_keys.end()) {
                auto& m = it->second;
                keyboard_matrix[m.row] &= ~(1 << m.col);
                if (m.shift_override != 0) {
                    --synthetic_shift_count;
                    if (synthetic_shift_count <= 0) {
                        synthetic_shift_count = 0;
                        if (physical_shift_held) keyboard_matrix[7] |= 0x01;
                        else                     keyboard_matrix[7] &= ~0x01;
                    }
                }
                active_keys.erase(it);
            }
            continue;
        }

        // ── Key-down: compute TRS-80 matrix mapping ──────────────────────────
        uint8_t row          = 0xFF;
        uint8_t col          = 0;
        int     shift_override = 0;  // 0=pass-through, 1=force on, -1=force off

        // Mac-specific shifted symbol remappings
        if (shifted) {
            switch (sc) {
                case SDL_SCANCODE_2:          row=0; col=0; shift_override=-1; break; // @ (no shift)
                case SDL_SCANCODE_6:          row=6; col=3; shift_override=-1; break; // UP arrow
                case SDL_SCANCODE_7:          row=4; col=6; shift_override= 1; break; // &
                case SDL_SCANCODE_8:          row=5; col=2; shift_override= 1; break; // *
                case SDL_SCANCODE_9:          row=5; col=0; shift_override= 1; break; // (
                case SDL_SCANCODE_0:          row=5; col=1; shift_override= 1; break; // )
                case SDL_SCANCODE_MINUS:      break;                                   // _ — no equiv
                case SDL_SCANCODE_EQUALS:     row=5; col=3; shift_override= 1; break; // +
                case SDL_SCANCODE_SEMICOLON:  row=5; col=2; shift_override=-1; break; // : (no shift)
                case SDL_SCANCODE_APOSTROPHE: row=4; col=2; shift_override= 1; break; // "
                default: break;
            }
        } else {
            switch (sc) {
                case SDL_SCANCODE_EQUALS:     row=5; col=5; shift_override=1; break; // = → Shift+-
                case SDL_SCANCODE_APOSTROPHE: row=4; col=7; shift_override=1; break; // ' → Shift+7
                default: break;
            }
        }

        // Standard scancode → TRS-80 matrix
        if (row == 0xFF) {
            switch (sc) {
                // Row 0: @ A B C D E F G
                case SDL_SCANCODE_A: row=0; col=1; break;
                case SDL_SCANCODE_B: row=0; col=2; break;
                case SDL_SCANCODE_C: row=0; col=3; break;
                case SDL_SCANCODE_D: row=0; col=4; break;
                case SDL_SCANCODE_E: row=0; col=5; break;
                case SDL_SCANCODE_F: row=0; col=6; break;
                case SDL_SCANCODE_G: row=0; col=7; break;
                // Row 1: H I J K L M N O
                case SDL_SCANCODE_H: row=1; col=0; break;
                case SDL_SCANCODE_I: row=1; col=1; break;
                case SDL_SCANCODE_J: row=1; col=2; break;
                case SDL_SCANCODE_K: row=1; col=3; break;
                case SDL_SCANCODE_L: row=1; col=4; break;
                case SDL_SCANCODE_M: row=1; col=5; break;
                case SDL_SCANCODE_N: row=1; col=6; break;
                case SDL_SCANCODE_O: row=1; col=7; break;
                // Row 2: P Q R S T U V W
                case SDL_SCANCODE_P: row=2; col=0; break;
                case SDL_SCANCODE_Q: row=2; col=1; break;
                case SDL_SCANCODE_R: row=2; col=2; break;
                case SDL_SCANCODE_S: row=2; col=3; break;
                case SDL_SCANCODE_T: row=2; col=4; break;
                case SDL_SCANCODE_U: row=2; col=5; break;
                case SDL_SCANCODE_V: row=2; col=6; break;
                case SDL_SCANCODE_W: row=2; col=7; break;
                // Row 3: X Y Z
                case SDL_SCANCODE_X: row=3; col=0; break;
                case SDL_SCANCODE_Y: row=3; col=1; break;
                case SDL_SCANCODE_Z: row=3; col=2; break;
                // Row 4: 0 1 2 3 4 5 6 7
                case SDL_SCANCODE_0: row=4; col=0; break;
                case SDL_SCANCODE_1: row=4; col=1; break;
                case SDL_SCANCODE_2: row=4; col=2; break;
                case SDL_SCANCODE_3: row=4; col=3; break;
                case SDL_SCANCODE_4: row=4; col=4; break;
                case SDL_SCANCODE_5: row=4; col=5; break;
                case SDL_SCANCODE_6: row=4; col=6; break;
                case SDL_SCANCODE_7: row=4; col=7; break;
                // Row 5: 8 9 : ; , - . /
                case SDL_SCANCODE_8:          row=5; col=0; break;
                case SDL_SCANCODE_9:          row=5; col=1; break;
                case SDL_SCANCODE_SEMICOLON:  row=5; col=3; break;
                case SDL_SCANCODE_COMMA:      row=5; col=4; break;
                case SDL_SCANCODE_MINUS:      row=5; col=5; break;
                case SDL_SCANCODE_PERIOD:     row=5; col=6; break;
                case SDL_SCANCODE_SLASH:      row=5; col=7; break;
                // Row 6: ENTER CLEAR BREAK UP DOWN LEFT RIGHT SPACE
                case SDL_SCANCODE_RETURN:    row=6; col=0; break;
                case SDL_SCANCODE_HOME:      row=6; col=1; break; // CLEAR
                case SDL_SCANCODE_ESCAPE:    row=6; col=2; break; // BREAK
                case SDL_SCANCODE_UP:        row=6; col=3; break;
                case SDL_SCANCODE_DOWN:      row=6; col=4; break;
                case SDL_SCANCODE_BACKSPACE: row=6; col=5; break; // LEFT arrow
                case SDL_SCANCODE_LEFT:      row=6; col=5; break;
                case SDL_SCANCODE_RIGHT:     row=6; col=6; break;
                case SDL_SCANCODE_SPACE:     row=6; col=7; break;
                // F5 / F6: TRS-80 @ key and 0 key (useful shortcuts)
                case SDL_SCANCODE_F5: row=0; col=0; shift_override=-1; break; // @
                case SDL_SCANCODE_F6: row=4; col=0; shift_override=-1; break; // 0
                default: break;
            }
        }

        if (row < 8) {
            active_keys[(int)sc] = {row, col, shift_override};
            keyboard_matrix[row] |= (1 << col);
            if (shift_override == 1) {
                ++synthetic_shift_count;
                keyboard_matrix[7] |= 0x01;
            } else if (shift_override == -1) {
                ++synthetic_shift_count;
                keyboard_matrix[7] &= ~0x01;
            }
        }
    }

    return running;
}

// src/video/Display.cpp
#include "Display.hpp"
#include "CharRom.hpp"
#include "../system/Bus.hpp"
#include <iostream>
#include <cstdio>
#include <cstring>

// ============================================================================
// TRS-80 CHARACTER GENERATOR ROM
// This is the actual character pattern from the TRS-80 Model I
// Each character is 8 bytes (5×7 dot matrix, top bit unused)
// ============================================================================

Display::Display() {
    std::memset(keyboard_matrix, 0x00, sizeof(keyboard_matrix));
    init_char_generator();
}

Display::~Display() {
    cleanup();
}

// ============================================================================
// CHARACTER GENERATOR INITIALIZATION
// ============================================================================
void Display::init_char_generator() {
    // Load character patterns from the MCM6670P-compatible character ROM
    // (defined in CharRom.hpp). This is the authentic TRS-80 Model I
    // character generator data — it was a separate chip on the motherboard,
    // NOT embedded in the Level I or Level II BASIC ROMs.
    static_assert(sizeof(TRS80_CHAR_GEN) == CHAR_GEN_CHARS * CHAR_GEN_BYTES_PER_CHAR,
                  "CharRom size mismatch");
    std::memcpy(char_generator.data(), TRS80_CHAR_GEN, TRS80_CHAR_GEN_SIZE);
}

uint8_t Display::get_char_pattern(uint8_t char_code, uint8_t row) const {
    if (row >= 8) return 0x00;
    // TRS-80 MCM6670P character ROM uses 6-bit addressing (64 chars):
    //   0x00-0x1F → @, A-Z, special  (maps to ASCII 0x40-0x5F in our table)
    //   0x20-0x3F → space, digits, punctuation (same as ASCII)
    // VRAM bit 6 is ignored by character ROM hardware; bit 7 selects semigraphics
    uint8_t rom_addr = char_code & 0x3F;
    uint8_t ascii_idx = (rom_addr < 0x20) ? (rom_addr + 0x40) : rom_addr;
    return char_generator[ascii_idx * 8 + row];
}

// ============================================================================
// SDL INITIALIZATION
// ============================================================================
bool Display::init(const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL Video Init Failed: " << SDL_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "SDL Window Creation Failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED
    );

    if (!renderer) {
        std::cerr << "SDL Renderer Creation Failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create texture for the 128×48 framebuffer
    screen_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        TRS80_WIDTH,
        TRS80_HEIGHT
    );

    if (!screen_texture) {
        std::cerr << "SDL Texture Creation Failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Set scaling quality to nearest-neighbor (pixel-perfect)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    clear_screen();
    init_keyboard_matrix();

    std::cout << "Display initialized: " << WINDOW_WIDTH << "×" << WINDOW_HEIGHT << std::endl;
    return true;
}

void Display::cleanup() {
    if (screen_texture) {
        SDL_DestroyTexture(screen_texture);
        screen_texture = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

// ============================================================================
// FRAMEBUFFER OPERATIONS
// ============================================================================
void Display::clear_screen() {
    framebuffer.fill(COLOR_BLACK);
}

void Display::draw_pixel(uint16_t x, uint16_t y, bool on) {
    if (x >= TRS80_WIDTH || y >= TRS80_HEIGHT) return;
    
    uint32_t color = on ? COLOR_GREEN : COLOR_BLACK;
    framebuffer[y * TRS80_WIDTH + x] = color;
}

void Display::draw_character(uint16_t char_x, uint16_t char_y, uint8_t char_code) {
    // TRS-80 characters: 64 per line × 16 lines
    // Each cell is CHAR_CELL_W × CHAR_CELL_H pixels
    // Character ROM has 8 rows; remaining rows are blank inter-line gap

    uint16_t pixel_x = char_x * CHAR_CELL_W;
    uint16_t pixel_y = char_y * CHAR_CELL_H;

    // Handle semigraphic characters (bit 7 set) — 2×3 block graphics
    if (char_code & 0x80) {
        // 6 blocks: 2 columns × 3 rows, each block is 3×4 pixels
        // Bits: 0=TL, 1=TR, 2=ML, 3=MR, 4=BL, 5=BR
        for (int block_row = 0; block_row < 3; block_row++) {
            for (int block_col = 0; block_col < 2; block_col++) {
                int bit = block_row * 2 + block_col;
                bool on = (char_code >> bit) & 0x01;
                // Each block is 3 pixels wide × 4 pixels tall
                for (int py = 0; py < 4; py++) {
                    for (int px = 0; px < 3; px++) {
                        draw_pixel(pixel_x + block_col * 3 + px,
                                   pixel_y + block_row * 4 + py, on);
                    }
                }
            }
        }
        return;
    }

    // Normal character from ROM (bits 5..0, MSB-first)
    for (int row = 0; row < 8; row++) {
        uint8_t pattern = get_char_pattern(char_code, row);
        for (int col = 0; col < CHAR_CELL_W; col++) {
            bool on = (pattern >> (5 - col)) & 0x01;
            draw_pixel(pixel_x + col, pixel_y + row, on);
        }
    }
    // Rows 8..11 are blank (inter-line gap) — already cleared by clear_screen()
}

void Display::update_texture() {
    SDL_UpdateTexture(
        screen_texture,
        nullptr,
        framebuffer.data(),
        TRS80_WIDTH * sizeof(uint32_t)
    );

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, screen_texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

// ============================================================================
// FRAME RENDERING (Called once per 60Hz frame)
// ============================================================================
void Display::render_frame(const Bus& bus) {
    clear_screen();

    // Read VRAM from Bus and render all 1024 characters
    for (int line = 0; line < TRS80_CHAR_LINES; line++) {
        for (int col = 0; col < TRS80_CHARS_PER_LINE; col++) {
            uint16_t vram_addr = line * TRS80_CHARS_PER_LINE + col;
            uint8_t char_code = bus.get_vram_byte(vram_addr);
            draw_character(col, line, char_code);
        }
    }

    update_texture();
}

// ============================================================================
// SCANLINE RENDERING (Optional - for cycle-accurate video)
// ============================================================================
void Display::render_scanline(const Bus& bus, uint16_t scanline) {
    if (scanline >= TRS80_HEIGHT) return;

    uint16_t char_line = scanline / CHAR_CELL_H;
    uint16_t row_in_cell = scanline % CHAR_CELL_H;

    for (int col = 0; col < TRS80_CHARS_PER_LINE; col++) {
        uint16_t vram_addr = char_line * TRS80_CHARS_PER_LINE + col;
        uint8_t char_code = bus.get_vram_byte(vram_addr);
        uint16_t pixel_x = col * CHAR_CELL_W;

        if (row_in_cell >= 8) {
            // Inter-line gap — blank
            for (int px = 0; px < CHAR_CELL_W; px++)
                draw_pixel(pixel_x + px, scanline, false);
            continue;
        }

        if (char_code & 0x80) {
            // Semigraphic block
            int block_row = row_in_cell / 4;
            for (int block_col = 0; block_col < 2; block_col++) {
                int bit = block_row * 2 + block_col;
                bool on = (char_code >> bit) & 0x01;
                for (int px = 0; px < 3; px++)
                    draw_pixel(pixel_x + block_col * 3 + px, scanline, on);
            }
        } else {
            uint8_t pattern = get_char_pattern(char_code, row_in_cell);
            for (int dot = 0; dot < CHAR_CELL_W; dot++) {
                bool on = (pattern >> (5 - dot)) & 0x01;
                draw_pixel(pixel_x + dot, scanline, on);
            }
        }
    }
}

// ============================================================================
// INPUT HANDLING
// ============================================================================
bool Display::handle_events(uint8_t* keyboard_matrix) {
    SDL_Event event;
    
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
            return false;
        }
        
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            bool pressed = (event.type == SDL_KEYDOWN);
            SDL_Scancode sc = event.key.keysym.scancode;
            bool host_shifted = (event.key.keysym.mod & KMOD_SHIFT) != 0;
            
            // TRS-80 Model I keyboard matrix (active high):
            // Row 0: @ A B C D E F G
            // Row 1: H I J K L M N O
            // Row 2: P Q R S T U V W
            // Row 3: X Y Z
            // Row 4: 0 1 2 3 4 5 6 7
            // Row 5: 8 9 : ; , - . /
            // Row 6: ENTER CLEAR BREAK UP DOWN LEFT RIGHT SPACE
            // Row 7: SHIFT
            
            // Handle shift key directly
            if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
                physical_shift_held = pressed;
                if (synthetic_shift_count == 0) {
                    if (physical_shift_held)
                        keyboard_matrix[7] |= 0x01;
                    else
                        keyboard_matrix[7] &= ~0x01;
                }
                continue;
            }
            
            // On key-up, look up what we stored on key-down and undo it
            if (!pressed) {
                auto it = active_keys.find((int)sc);
                if (it != active_keys.end()) {
                    auto& m = it->second;
                    keyboard_matrix[m.row] &= ~(1 << m.col);
                    if (m.shift_override != 0) {
                        synthetic_shift_count--;
                        if (synthetic_shift_count <= 0) {
                            synthetic_shift_count = 0;
                            if (physical_shift_held)
                                keyboard_matrix[7] |= 0x01;
                            else
                                keyboard_matrix[7] &= ~0x01;
                        }
                    }
                    active_keys.erase(it);
                }
                continue;
            }
            
            // KEY-DOWN: compute TRS-80 mapping
            uint8_t row = 0xFF;
            uint8_t col = 0;
            int shift_override = 0;  // 0=pass-through, 1=force on, -1=force off
            
            // Special Mac-to-TRS-80 symbol remappings
            // These keys produce different symbols on Mac vs TRS-80 when shifted
            if (host_shifted) {
                switch (sc) {
                    case SDL_SCANCODE_2:         // Mac '@' → TRS-80 @ (row 0 col 0, no shift)
                        row = 0; col = 0; shift_override = -1; break;
                    case SDL_SCANCODE_6:         // Mac '^' → TRS-80 UP arrow
                        row = 6; col = 3; shift_override = -1; break;
                    case SDL_SCANCODE_7:         // Mac '&' → TRS-80 Shift+6
                        row = 4; col = 6; shift_override = 1; break;
                    case SDL_SCANCODE_8:         // Mac '*' → TRS-80 Shift+: (colon)
                        row = 5; col = 2; shift_override = 1; break;
                    case SDL_SCANCODE_9:         // Mac '(' → TRS-80 Shift+8
                        row = 5; col = 0; shift_override = 1; break;
                    case SDL_SCANCODE_0:         // Mac ')' → TRS-80 Shift+9
                        row = 5; col = 1; shift_override = 1; break;
                    case SDL_SCANCODE_MINUS:     // Mac '_' → no TRS-80 equiv, ignore
                        break;
                    case SDL_SCANCODE_EQUALS:    // Mac '+' → TRS-80 Shift+;
                        row = 5; col = 3; shift_override = 1; break;
                    case SDL_SCANCODE_SEMICOLON: // Mac ':' → TRS-80 : (no shift)
                        row = 5; col = 2; shift_override = -1; break;
                    case SDL_SCANCODE_APOSTROPHE:// Mac '"' → TRS-80 Shift+2
                        row = 4; col = 2; shift_override = 1; break;
                    default: break;
                }
            } else {
                switch (sc) {
                    case SDL_SCANCODE_EQUALS:    // Mac '=' → TRS-80 Shift+-
                        row = 5; col = 5; shift_override = 1; break;
                    case SDL_SCANCODE_APOSTROPHE:// Mac '\'' → TRS-80 Shift+7
                        row = 4; col = 7; shift_override = 1; break;
                    default: break;
                }
            }
            
            // Standard scancode mapping (if not already handled above)
            if (row == 0xFF) {
                switch (sc) {
                    // Row 0: @ A B C D E F G
                    case SDL_SCANCODE_A: row = 0; col = 1; break;
                    case SDL_SCANCODE_B: row = 0; col = 2; break;
                    case SDL_SCANCODE_C: row = 0; col = 3; break;
                    case SDL_SCANCODE_D: row = 0; col = 4; break;
                    case SDL_SCANCODE_E: row = 0; col = 5; break;
                    case SDL_SCANCODE_F: row = 0; col = 6; break;
                    case SDL_SCANCODE_G: row = 0; col = 7; break;
                    // Row 1: H I J K L M N O
                    case SDL_SCANCODE_H: row = 1; col = 0; break;
                    case SDL_SCANCODE_I: row = 1; col = 1; break;
                    case SDL_SCANCODE_J: row = 1; col = 2; break;
                    case SDL_SCANCODE_K: row = 1; col = 3; break;
                    case SDL_SCANCODE_L: row = 1; col = 4; break;
                    case SDL_SCANCODE_M: row = 1; col = 5; break;
                    case SDL_SCANCODE_N: row = 1; col = 6; break;
                    case SDL_SCANCODE_O: row = 1; col = 7; break;
                    // Row 2: P Q R S T U V W
                    case SDL_SCANCODE_P: row = 2; col = 0; break;
                    case SDL_SCANCODE_Q: row = 2; col = 1; break;
                    case SDL_SCANCODE_R: row = 2; col = 2; break;
                    case SDL_SCANCODE_S: row = 2; col = 3; break;
                    case SDL_SCANCODE_T: row = 2; col = 4; break;
                    case SDL_SCANCODE_U: row = 2; col = 5; break;
                    case SDL_SCANCODE_V: row = 2; col = 6; break;
                    case SDL_SCANCODE_W: row = 2; col = 7; break;
                    // Row 3: X Y Z
                    case SDL_SCANCODE_X: row = 3; col = 0; break;
                    case SDL_SCANCODE_Y: row = 3; col = 1; break;
                    case SDL_SCANCODE_Z: row = 3; col = 2; break;
                    // Row 4: 0 1 2 3 4 5 6 7
                    case SDL_SCANCODE_0: row = 4; col = 0; break;
                    case SDL_SCANCODE_1: row = 4; col = 1; break;
                    case SDL_SCANCODE_2: row = 4; col = 2; break;
                    case SDL_SCANCODE_3: row = 4; col = 3; break;
                    case SDL_SCANCODE_4: row = 4; col = 4; break;
                    case SDL_SCANCODE_5: row = 4; col = 5; break;
                    case SDL_SCANCODE_6: row = 4; col = 6; break;
                    case SDL_SCANCODE_7: row = 4; col = 7; break;
                    // Row 5: 8 9 : ; , - . /
                    case SDL_SCANCODE_8:         row = 5; col = 0; break;
                    case SDL_SCANCODE_9:         row = 5; col = 1; break;
                    case SDL_SCANCODE_SEMICOLON: row = 5; col = 3; break;
                    case SDL_SCANCODE_COMMA:     row = 5; col = 4; break;
                    case SDL_SCANCODE_MINUS:     row = 5; col = 5; break;
                    case SDL_SCANCODE_PERIOD:    row = 5; col = 6; break;
                    case SDL_SCANCODE_SLASH:     row = 5; col = 7; break;
                    // Row 6: ENTER CLEAR BREAK UP DOWN LEFT RIGHT SPACE
                    case SDL_SCANCODE_RETURN:    row = 6; col = 0; break;
                    case SDL_SCANCODE_HOME:      row = 6; col = 1; break; // CLEAR
                    case SDL_SCANCODE_ESCAPE:    row = 6; col = 2; break; // BREAK
                    case SDL_SCANCODE_UP:        row = 6; col = 3; break;
                    case SDL_SCANCODE_DOWN:      row = 6; col = 4; break;
                    case SDL_SCANCODE_BACKSPACE: row = 6; col = 5; break; // LEFT ARROW
                    case SDL_SCANCODE_LEFT:      row = 6; col = 5; break;
                    case SDL_SCANCODE_RIGHT:     row = 6; col = 6; break;
                    case SDL_SCANCODE_SPACE:     row = 6; col = 7; break;
                    default: break;
                }
            }
            
            if (row < 8) {
                // Store mapping so key-up can undo it exactly
                active_keys[(int)sc] = {row, col, shift_override};
                keyboard_matrix[row] |= (1 << col);
                if (shift_override == 1) {
                    synthetic_shift_count++;
                    keyboard_matrix[7] |= 0x01;
                } else if (shift_override == -1) {
                    synthetic_shift_count++;
                    keyboard_matrix[7] &= ~0x01;
                }
            }
        }
    }
    
    return running;
}

void Display::init_keyboard_matrix() {
    // All keys released (active high: 0 = not pressed)
    std::memset(keyboard_matrix, 0x00, sizeof(keyboard_matrix));
}

void Display::set_title(const std::string& title) {
    if (window) {
        SDL_SetWindowTitle(window, title.c_str());
    }
}
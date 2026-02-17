// src/video/Display.cpp
#include "Display.hpp"
#include "CharRom.hpp"
#include "../system/Bus.hpp"
#include <iostream>
#include <cstring>

// ============================================================================
// TRS-80 CHARACTER GENERATOR ROM
// This is the actual character pattern from the TRS-80 Model I
// Each character is 8 bytes (5×7 dot matrix, top bit unused)
// ============================================================================

Display::Display() {
    std::memset(keyboard_matrix, 0xFF, sizeof(keyboard_matrix));
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
    return char_generator[char_code * 8 + row];
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
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
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
            
            // Map SDL keys to TRS-80 keyboard matrix
            // TRS-80 Model I keyboard matrix (active low):
            // Address bits select rows, data bits are columns (D0-D7)
            // Row 0 (0x3801): @ A B C D E F G
            // Row 1 (0x3802): H I J K L M N O
            // Row 2 (0x3804): P Q R S T U V W
            // Row 3 (0x3808): X Y Z
            // Row 4 (0x3810): 0 1 2 3 4 5 6 7
            // Row 5 (0x3820): 8 9 : ; , - . /
            // Row 6 (0x3840): ENTER CLEAR BREAK UP DOWN LEFT RIGHT SPACE
            // Row 7 (0x3880): SHIFT
            
            uint8_t row = 0xFF;  // Unmapped by default
            uint8_t col = 0;
            
            switch (event.key.keysym.sym) {
                // Row 0: @ A B C D E F G
                case SDLK_AT:        row = 0; col = 0; break;
                case SDLK_a:         row = 0; col = 1; break;
                case SDLK_b:         row = 0; col = 2; break;
                case SDLK_c:         row = 0; col = 3; break;
                case SDLK_d:         row = 0; col = 4; break;
                case SDLK_e:         row = 0; col = 5; break;
                case SDLK_f:         row = 0; col = 6; break;
                case SDLK_g:         row = 0; col = 7; break;
                
                // Row 1: H I J K L M N O
                case SDLK_h:         row = 1; col = 0; break;
                case SDLK_i:         row = 1; col = 1; break;
                case SDLK_j:         row = 1; col = 2; break;
                case SDLK_k:         row = 1; col = 3; break;
                case SDLK_l:         row = 1; col = 4; break;
                case SDLK_m:         row = 1; col = 5; break;
                case SDLK_n:         row = 1; col = 6; break;
                case SDLK_o:         row = 1; col = 7; break;
                
                // Row 2: P Q R S T U V W
                case SDLK_p:         row = 2; col = 0; break;
                case SDLK_q:         row = 2; col = 1; break;
                case SDLK_r:         row = 2; col = 2; break;
                case SDLK_s:         row = 2; col = 3; break;
                case SDLK_t:         row = 2; col = 4; break;
                case SDLK_u:         row = 2; col = 5; break;
                case SDLK_v:         row = 2; col = 6; break;
                case SDLK_w:         row = 2; col = 7; break;
                
                // Row 3: X Y Z
                case SDLK_x:         row = 3; col = 0; break;
                case SDLK_y:         row = 3; col = 1; break;
                case SDLK_z:         row = 3; col = 2; break;
                
                // Row 4: 0 1 2 3 4 5 6 7
                case SDLK_0:         row = 4; col = 0; break;
                case SDLK_1:         row = 4; col = 1; break;
                case SDLK_2:         row = 4; col = 2; break;
                case SDLK_3:         row = 4; col = 3; break;
                case SDLK_4:         row = 4; col = 4; break;
                case SDLK_5:         row = 4; col = 5; break;
                case SDLK_6:         row = 4; col = 6; break;
                case SDLK_7:         row = 4; col = 7; break;
                
                // Row 5: 8 9 : ; , - . /
                case SDLK_8:         row = 5; col = 0; break;
                case SDLK_9:         row = 5; col = 1; break;
                case SDLK_COLON:     row = 5; col = 2; break;
                case SDLK_SEMICOLON: row = 5; col = 3; break;
                case SDLK_COMMA:     row = 5; col = 4; break;
                case SDLK_MINUS:     row = 5; col = 5; break;
                case SDLK_PERIOD:    row = 5; col = 6; break;
                case SDLK_SLASH:     row = 5; col = 7; break;
                
                // Row 6: ENTER CLEAR BREAK UP DOWN LEFT RIGHT SPACE
                case SDLK_RETURN:    row = 6; col = 0; break;
                case SDLK_BACKSPACE: row = 6; col = 1; break;  // CLEAR
                case SDLK_ESCAPE:    row = 6; col = 2; break;  // BREAK
                case SDLK_UP:        row = 6; col = 3; break;
                case SDLK_DOWN:      row = 6; col = 4; break;
                case SDLK_LEFT:      row = 6; col = 5; break;
                case SDLK_RIGHT:     row = 6; col = 6; break;
                case SDLK_SPACE:     row = 6; col = 7; break;
                
                // Row 7: SHIFT
                case SDLK_LSHIFT:
                case SDLK_RSHIFT:    row = 7; col = 0; break;
                
                default: break;
            }
            
            if (row < 8 && col < 8) {
                if (pressed) {
                    keyboard_matrix[row] &= ~(1 << col);  // Clear bit (active low)
                } else {
                    keyboard_matrix[row] |= (1 << col);   // Set bit
                }
            }
        }
    }
    
    return running;
}

void Display::init_keyboard_matrix() {
    // All keys released (bits set = not pressed, active low)
    std::memset(keyboard_matrix, 0xFF, sizeof(keyboard_matrix));
}

void Display::set_title(const std::string& title) {
    if (window) {
        SDL_SetWindowTitle(window, title.c_str());
    }
}
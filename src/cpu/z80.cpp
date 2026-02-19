// src/cpu/Z80.cpp
#include "z80.hpp"
#include "../system/Bus.hpp"
#include <cstdio>

Z80::Z80(Bus& b) : bus(b) {
    init_main_table();
    init_cb_table();
    init_ed_table();
    init_dd_table();
    init_fd_table();
}

void Z80::reset() {
    reg = {};
    reg.pc = 0x0000;
    reg.sp = 0xFFFF;
    t_states = 0;
    prefix = 0x00;
}

// ============================================================================
// MAIN EXECUTION STEP
// ============================================================================
int Z80::step() {
    t_states = 0;
    is_m1_cycle = true;
    
    // Handle prefix bytes
    if (prefix != 0x00) {
        uint8_t op = fetch(true);
        switch (prefix) {
            case 0xCB: cb_table[op](); break;
            case 0xED: ed_table[op](); break;
            case 0xDD: dd_table[op](); break;
            case 0xFD: fd_table[op](); break;
        }
        prefix = 0x00;
        return t_states;
    }
    
    // Check for HALT state
    if (reg.halted) {
        fetch(true);  // Consume a cycle
        add_ticks(4);
        reg.pc--;     // Don't advance PC
        return t_states;
    }
    
    // Execute main opcode
    uint8_t op = fetch(true);  // M1 cycle for TRS-80 contention
    main_table[op]();
    
    return t_states;
}

// ============================================================================
// LOW-LEVEL HELPERS
// ============================================================================
void Z80::set_flag(uint8_t flag, bool value) {
    if (value) reg.f |= flag;
    else reg.f &= ~flag;
}

bool Z80::get_flag(uint8_t flag) const {
    return (reg.f & flag) != 0;
}

void Z80::set_zf(uint8_t val) { set_flag(FLAG_Z, val == 0); }
void Z80::set_sf(uint8_t val) { set_flag(FLAG_S, val & 0x80); }
void Z80::set_hf(bool val)    { set_flag(FLAG_H, val); }
void Z80::set_nf(bool val)    { set_flag(FLAG_N, val); }
void Z80::set_cf(bool val)    { set_flag(FLAG_C, val); }

void Z80::set_pf(uint8_t val) {
    set_flag(FLAG_P, parity(val));
}

// Set undocumented flag bits 3 and 5 from a value
void Z80::set_f35(uint8_t val) {
    reg.f = (reg.f & ~(FLAG_F3 | FLAG_F5)) | (val & (FLAG_F3 | FLAG_F5));
}

bool Z80::parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return (val & 1) == 0;  // true if even parity
}

void Z80::set_flags_8bit(uint8_t result, uint8_t a, uint8_t b,
                          bool subtract, bool half_carry, bool carry) {
    set_sf(result);
    set_zf(result);
    set_hf(half_carry);
    // P/V = overflow for arithmetic
    if (subtract) {
        set_flag(FLAG_P, ((a ^ b) & (a ^ result)) & 0x80);
    } else {
        set_flag(FLAG_P, ((~(a ^ b)) & (a ^ result)) & 0x80);
    }
    set_nf(subtract);
    set_cf(carry);
    set_f35(result);
}

uint8_t Z80::read_mem(uint16_t addr, bool is_m1) {
    return bus.read(addr, is_m1);
}

void Z80::write_mem(uint16_t addr, uint8_t val) {
    bus.write(addr, val);
}

uint8_t Z80::fetch(bool is_m1) {
    uint8_t val = read_mem(reg.pc++, is_m1);
    // Z80: lower 7 bits of R increment on every M1 (opcode fetch) cycle.
    // Bit 7 is preserved. This makes LD A,R useful as a cheap PRNG source.
    if (is_m1) {
        reg.r = (reg.r & 0x80) | ((reg.r + 1) & 0x7F);
    }
    return val;
}

uint16_t Z80::fetch16() {
    uint8_t lo = fetch(false);
    uint8_t hi = fetch(false);
    return (hi << 8) | lo;
}

void Z80::add_ticks(int t) {
    t_states += t;
}

// ============================================================================
// REGISTER HELPERS
// ============================================================================
uint8_t& Z80::get_reg_8(uint8_t code) {
    switch (code) {
        case 0: return reg.b;
        case 1: return reg.c;
        case 2: return reg.d;
        case 3: return reg.e;
        case 4: return reg.h;
        case 5: return reg.l;
        case 6: {
            // (HL) - read through memory
            static uint8_t temp;
            temp = read_mem(reg.hl);
            return temp;
        }
        case 7: return reg.a;
        default: return reg.a;
    }
}

uint16_t& Z80::get_reg_16(uint8_t code) {
    switch (code) {
        case 0: return reg.bc;
        case 1: return reg.de;
        case 2: return reg.hl;
        case 3: return reg.sp;
        default: return reg.sp;
    }
}

void Z80::push(uint16_t val) {
    write_mem(--reg.sp, val >> 8);
    write_mem(--reg.sp, val & 0xFF);
}

uint16_t Z80::pop() {
    uint8_t lo = read_mem(reg.sp++);
    uint8_t hi = read_mem(reg.sp++);
    return (hi << 8) | lo;
}

// ============================================================================
// ARITHMETIC OPERATIONS
// ============================================================================
void Z80::op_add(uint8_t val) {
    uint8_t old_a = reg.a;
    uint16_t result = old_a + val;
    bool hc = (old_a & 0x0F) + (val & 0x0F) > 0x0F;
    reg.a = result & 0xFF;
    set_flags_8bit(reg.a, old_a, val, false, hc, result > 0xFF);
}

void Z80::op_adc(uint8_t val) {
    uint8_t old_a = reg.a;
    uint8_t carry = get_flag(FLAG_C) ? 1 : 0;
    uint16_t result = old_a + val + carry;
    bool hc = (old_a & 0x0F) + (val & 0x0F) + carry > 0x0F;
    reg.a = result & 0xFF;
    set_flags_8bit(reg.a, old_a, val, false, hc, result > 0xFF);
}

void Z80::op_sub(uint8_t val) {
    uint8_t old_a = reg.a;
    uint16_t result = old_a - val;
    bool hc = (old_a & 0x0F) < (val & 0x0F);
    reg.a = result & 0xFF;
    set_flags_8bit(reg.a, old_a, val, true, hc, result > 0xFF);
}

void Z80::op_sbc(uint8_t val) {
    uint8_t old_a = reg.a;
    uint8_t carry = get_flag(FLAG_C) ? 1 : 0;
    uint16_t result = old_a - val - carry;
    bool hc = (old_a & 0x0F) < (val & 0x0F) + carry;
    reg.a = result & 0xFF;
    set_flags_8bit(reg.a, old_a, val, true, hc, result > 0xFF);
}

void Z80::op_and(uint8_t val) {
    reg.a &= val;
    set_sf(reg.a);
    set_zf(reg.a);
    set_hf(true);
    set_pf(reg.a);
    set_nf(false);
    set_cf(false);
    set_f35(reg.a);
}

void Z80::op_xor(uint8_t val) {
    reg.a ^= val;
    set_sf(reg.a);
    set_zf(reg.a);
    set_hf(false);
    set_pf(reg.a);
    set_nf(false);
    set_cf(false);
    set_f35(reg.a);
}

void Z80::op_or(uint8_t val) {
    reg.a |= val;
    set_sf(reg.a);
    set_zf(reg.a);
    set_hf(false);
    set_pf(reg.a);
    set_nf(false);
    set_cf(false);
    set_f35(reg.a);
}

void Z80::op_cp(uint8_t val) {
    uint8_t result8 = reg.a - val;
    uint16_t result = reg.a - val;
    bool hc = (reg.a & 0x0F) < (val & 0x0F);
    set_sf(result8);
    set_zf(result8);
    set_hf(hc);
    set_flag(FLAG_P, ((reg.a ^ val) & (reg.a ^ result8)) & 0x80);  // overflow
    set_nf(true);
    set_cf(result > 0xFF);
    set_f35(val);  // CP: bits 3/5 come from the operand, not result
}

void Z80::op_inc(uint8_t& r) {
    bool hc = (r & 0x0F) == 0x0F;
    r++;
    set_sf(r);
    set_zf(r);
    set_hf(hc);
    set_flag(FLAG_P, r == 0x80);  // overflow: 0x7F -> 0x80
    set_nf(false);
    set_f35(r);
}

void Z80::op_dec(uint8_t& r) {
    bool hc = (r & 0x0F) == 0x00;
    r--;
    set_sf(r);
    set_zf(r);
    set_hf(hc);
    set_flag(FLAG_P, r == 0x7F);  // overflow: 0x80 -> 0x7F
    set_nf(true);
    set_f35(r);
}

void Z80::op_inc16(uint16_t& r) {
    r++;
}

void Z80::op_dec16(uint16_t& r) {
    r--;
}

void Z80::op_add16(uint16_t& r, uint16_t val) {
    uint32_t result = r + val;
    bool hc = (r & 0x0FFF) + (val & 0x0FFF) > 0x0FFF;
    r = result & 0xFFFF;
    set_hf(hc);
    set_nf(false);
    set_cf(result > 0xFFFF);
    set_f35(r >> 8);  // bits 3/5 from high byte of result
}

// ============================================================================
// BIT OPERATIONS
// ============================================================================
void Z80::op_bit(uint8_t bit, uint8_t val) {
    bool is_zero = !(val & (1 << bit));
    set_flag(FLAG_Z, is_zero);
    set_hf(true);
    set_nf(false);
    set_flag(FLAG_S, (bit == 7) && !is_zero);
    set_flag(FLAG_P, is_zero);  // P/V = Z for BIT
    set_f35(val);  // bits 3/5 from operand (register form)
}

void Z80::op_set(uint8_t bit, uint8_t& val) {
    val |= (1 << bit);
}

void Z80::op_res(uint8_t bit, uint8_t& val) {
    val &= ~(1 << bit);
}

void Z80::op_rl(uint8_t& val) {
    bool old_c = get_flag(FLAG_C);
    bool new_c = val & 0x80;
    val = (val << 1) | (old_c ? 1 : 0);
    set_cf(new_c);
    set_sf(val);
    set_zf(val);
    set_hf(false);
    set_pf(val);
    set_nf(false);
    set_f35(val);
}

void Z80::op_rr(uint8_t& val) {
    bool old_c = get_flag(FLAG_C);
    bool new_c = val & 0x01;
    val = (val >> 1) | (old_c ? 0x80 : 0);
    set_cf(new_c);
    set_sf(val);
    set_zf(val);
    set_hf(false);
    set_pf(val);
    set_nf(false);
    set_f35(val);
}

void Z80::op_rla() {
    bool old_c = get_flag(FLAG_C);
    bool new_c = reg.a & 0x80;
    reg.a = (reg.a << 1) | (old_c ? 1 : 0);
    set_cf(new_c);
    set_hf(false);
    set_nf(false);
    set_f35(reg.a);
}

void Z80::op_rra() {
    bool old_c = get_flag(FLAG_C);
    bool new_c = reg.a & 0x01;
    reg.a = (reg.a >> 1) | (old_c ? 0x80 : 0);
    set_cf(new_c);
    set_hf(false);
    set_nf(false);
    set_f35(reg.a);
}

void Z80::op_rlc(uint8_t& val) {
    bool c = val & 0x80;
    val = (val << 1) | (c ? 1 : 0);
    set_cf(c);
    set_sf(val);
    set_zf(val);
    set_hf(false);
    set_pf(val);
    set_nf(false);
    set_f35(val);
}

void Z80::op_rrc(uint8_t& val) {
    bool c = val & 0x01;
    val = (val >> 1) | (c ? 0x80 : 0);
    set_cf(c);
    set_sf(val);
    set_zf(val);
    set_hf(false);
    set_pf(val);
    set_nf(false);
    set_f35(val);
}

void Z80::op_sl(uint8_t& val) {
    bool c = val & 0x80;
    val <<= 1;
    set_cf(c);
    set_sf(val);
    set_zf(val);
    set_hf(false);
    set_pf(val);
    set_nf(false);
    set_f35(val);
}

void Z80::op_sr(uint8_t& val) {
    bool c = val & 0x01;
    bool sign = val & 0x80;
    val >>= 1;
    if (sign) val |= 0x80;  // Arithmetic shift
    set_cf(c);
    set_sf(val);
    set_zf(val);
    set_hf(false);
    set_pf(val);
    set_nf(false);
    set_f35(val);
}

// ============================================================================
// FLOW CONTROL
// ============================================================================
void Z80::op_call() {
    uint16_t addr = fetch16();
    push(reg.pc);
    reg.pc = addr;
    add_ticks(17);
}

void Z80::op_ret() {
    reg.pc = pop();
    add_ticks(10);
}

void Z80::op_reti() {
    reg.pc = pop();
    reg.iff1 = reg.iff2 = true;
    add_ticks(14);
}

void Z80::op_jp() {
    reg.pc = fetch16();
    add_ticks(10);
}

void Z80::op_jr() {
    int8_t offset = static_cast<int8_t>(fetch(false));
    reg.pc += offset;
    add_ticks(12);
}

void Z80::op_rst(uint8_t addr) {
    push(reg.pc);
    reg.pc = addr;
    add_ticks(11);
}

// ============================================================================
// SPECIAL OPERATIONS
// ============================================================================
void Z80::op_halt() {
    reg.halted = true;
}

void Z80::op_di() {
    reg.iff1 = reg.iff2 = false;
}

void Z80::op_ei() {
    reg.iff1 = reg.iff2 = true;
}

void Z80::op_nop() {
}

void Z80::op_ex_af() {
    std::swap(reg.a, reg.a2);
    std::swap(reg.f, reg.f2);
}

void Z80::op_ex_de_hl() {
    std::swap(reg.d, reg.h);
    std::swap(reg.e, reg.l);
}

void Z80::op_exx() {
    std::swap(reg.bc, reg.bc2);
    std::swap(reg.de, reg.de2);
    std::swap(reg.hl, reg.hl2);
}

void Z80::op_ld_i_a() {
    reg.i = reg.a;
}

void Z80::op_ld_r_a() {
    reg.r = reg.a;
}

void Z80::op_ld_a_i() {
    reg.a = reg.i;
    set_sf(reg.a);
    set_zf(reg.a);
    set_hf(false);
    set_nf(false);
    set_flag(FLAG_P, reg.iff2);
    set_f35(reg.a);
}

void Z80::op_ld_a_r() {
    reg.a = reg.r;
    set_sf(reg.a);
    set_zf(reg.a);
    set_hf(false);
    set_nf(false);
    set_flag(FLAG_P, reg.iff2);
    set_f35(reg.a);
}

void Z80::op_ld_sp_hl() {
    reg.sp = reg.hl;
}

void Z80::op_ld_hl_sp() {
    reg.hl = fetch16();
}

void Z80::op_daa() {
    // Algorithm matches MAME Z80 core (hardware-verified)
    uint8_t old_a = reg.a;
    uint8_t a = old_a;
    bool n_flag = get_flag(FLAG_N);
    bool half = get_flag(FLAG_H);
    bool carry = get_flag(FLAG_C);

    if (n_flag) {
        // After subtraction
        if (half || (old_a & 0x0F) > 9) a -= 6;
        if (carry || old_a > 0x99) a -= 0x60;
    } else {
        // After addition
        if (half || (old_a & 0x0F) > 9) a += 6;
        if (carry || old_a > 0x99) a += 0x60;
    }

    reg.a = a;
    // Flags: preserve N, compute C/H/S/Z/P/F3/F5
    // C: old_C OR (original A > 0x99)
    // H: XOR of bit 4 between original and result
    reg.f = (reg.f & (FLAG_C | FLAG_N)) | (old_a > 0x99 ? FLAG_C : 0) | ((old_a ^ a) & FLAG_H);
    set_sf(reg.a);
    set_zf(reg.a);
    set_pf(reg.a);
    set_f35(reg.a);
}

// ============================================================================
// OPCODE TABLE INITIALIZATION (MAIN TABLE - 0x00 to 0xFF)
// ============================================================================
void Z80::init_main_table() {
    // Fill all with unknown handler (with logging)
    for (int i = 0; i < 256; i++) {
        main_table[i] = [this, i]() {
            static int warn_count = 0;
            if (warn_count < 50) {
                fprintf(stderr, "UNIMPL main 0x%02X at PC=0x%04X\n", i, (unsigned)(reg.pc - 1));
                warn_count++;
            }
            add_ticks(4);
        };
    }
    
    // --- 8-bit Load Group (0x40-0x7F) ---
    // LD r, r matrix - using lambda capture for register references
    main_table[0x40] = [this]() { reg.b = reg.b; add_ticks(4); };
    main_table[0x41] = [this]() { reg.b = reg.c; add_ticks(4); };
    main_table[0x42] = [this]() { reg.b = reg.d; add_ticks(4); };
    main_table[0x43] = [this]() { reg.b = reg.e; add_ticks(4); };
    main_table[0x44] = [this]() { reg.b = reg.h; add_ticks(4); };
    main_table[0x45] = [this]() { reg.b = reg.l; add_ticks(4); };
    main_table[0x47] = [this]() { reg.b = reg.a; add_ticks(4); };
    
    main_table[0x48] = [this]() { reg.c = reg.b; add_ticks(4); };
    main_table[0x49] = [this]() { reg.c = reg.c; add_ticks(4); };
    main_table[0x4A] = [this]() { reg.c = reg.d; add_ticks(4); };
    main_table[0x4B] = [this]() { reg.c = reg.e; add_ticks(4); };
    main_table[0x4C] = [this]() { reg.c = reg.h; add_ticks(4); };
    main_table[0x4D] = [this]() { reg.c = reg.l; add_ticks(4); };
    main_table[0x4F] = [this]() { reg.c = reg.a; add_ticks(4); };
    
    main_table[0x50] = [this]() { reg.d = reg.b; add_ticks(4); };
    main_table[0x51] = [this]() { reg.d = reg.c; add_ticks(4); };
    main_table[0x52] = [this]() { reg.d = reg.d; add_ticks(4); };
    main_table[0x53] = [this]() { reg.d = reg.e; add_ticks(4); };
    main_table[0x54] = [this]() { reg.d = reg.h; add_ticks(4); };
    main_table[0x55] = [this]() { reg.d = reg.l; add_ticks(4); };
    main_table[0x57] = [this]() { reg.d = reg.a; add_ticks(4); };
    
    main_table[0x58] = [this]() { reg.e = reg.b; add_ticks(4); };
    main_table[0x59] = [this]() { reg.e = reg.c; add_ticks(4); };
    main_table[0x5A] = [this]() { reg.e = reg.d; add_ticks(4); };
    main_table[0x5B] = [this]() { reg.e = reg.e; add_ticks(4); };
    main_table[0x5C] = [this]() { reg.e = reg.h; add_ticks(4); };
    main_table[0x5D] = [this]() { reg.e = reg.l; add_ticks(4); };
    main_table[0x5F] = [this]() { reg.e = reg.a; add_ticks(4); };
    
    main_table[0x60] = [this]() { reg.h = reg.b; add_ticks(4); };
    main_table[0x61] = [this]() { reg.h = reg.c; add_ticks(4); };
    main_table[0x62] = [this]() { reg.h = reg.d; add_ticks(4); };
    main_table[0x63] = [this]() { reg.h = reg.e; add_ticks(4); };
    main_table[0x64] = [this]() { reg.h = reg.h; add_ticks(4); };
    main_table[0x65] = [this]() { reg.h = reg.l; add_ticks(4); };
    main_table[0x67] = [this]() { reg.h = reg.a; add_ticks(4); };
    
    main_table[0x68] = [this]() { reg.l = reg.b; add_ticks(4); };
    main_table[0x69] = [this]() { reg.l = reg.c; add_ticks(4); };
    main_table[0x6A] = [this]() { reg.l = reg.d; add_ticks(4); };
    main_table[0x6B] = [this]() { reg.l = reg.e; add_ticks(4); };
    main_table[0x6C] = [this]() { reg.l = reg.h; add_ticks(4); };
    main_table[0x6D] = [this]() { reg.l = reg.l; add_ticks(4); };
    main_table[0x6F] = [this]() { reg.l = reg.a; add_ticks(4); };
    
    main_table[0x78] = [this]() { reg.a = reg.b; add_ticks(4); };
    main_table[0x79] = [this]() { reg.a = reg.c; add_ticks(4); };
    main_table[0x7A] = [this]() { reg.a = reg.d; add_ticks(4); };
    main_table[0x7B] = [this]() { reg.a = reg.e; add_ticks(4); };
    main_table[0x7C] = [this]() { reg.a = reg.h; add_ticks(4); };
    main_table[0x7D] = [this]() { reg.a = reg.l; add_ticks(4); };
    main_table[0x7F] = [this]() { reg.a = reg.a; add_ticks(4); };
    
    // LD r, (HL)
    main_table[0x46] = [this]() { reg.b = read_mem(reg.hl); add_ticks(7); };
    main_table[0x4E] = [this]() { reg.c = read_mem(reg.hl); add_ticks(7); };
    main_table[0x56] = [this]() { reg.d = read_mem(reg.hl); add_ticks(7); };
    main_table[0x5E] = [this]() { reg.e = read_mem(reg.hl); add_ticks(7); };
    main_table[0x66] = [this]() { reg.h = read_mem(reg.hl); add_ticks(7); };
    main_table[0x6E] = [this]() { reg.l = read_mem(reg.hl); add_ticks(7); };
    main_table[0x7E] = [this]() { reg.a = read_mem(reg.hl); add_ticks(7); };
    
    // LD (HL), r
    main_table[0x70] = [this]() { write_mem(reg.hl, reg.b); add_ticks(7); };
    main_table[0x71] = [this]() { write_mem(reg.hl, reg.c); add_ticks(7); };
    main_table[0x72] = [this]() { write_mem(reg.hl, reg.d); add_ticks(7); };
    main_table[0x73] = [this]() { write_mem(reg.hl, reg.e); add_ticks(7); };
    main_table[0x74] = [this]() { write_mem(reg.hl, reg.h); add_ticks(7); };
    main_table[0x75] = [this]() { write_mem(reg.hl, reg.l); add_ticks(7); };
    main_table[0x77] = [this]() { write_mem(reg.hl, reg.a); add_ticks(7); };
    
    // LD r, n (immediate)
    main_table[0x06] = [this]() { reg.b = fetch(false); add_ticks(7); };
    main_table[0x0E] = [this]() { reg.c = fetch(false); add_ticks(7); };
    main_table[0x16] = [this]() { reg.d = fetch(false); add_ticks(7); };
    main_table[0x1E] = [this]() { reg.e = fetch(false); add_ticks(7); };
    main_table[0x26] = [this]() { reg.h = fetch(false); add_ticks(7); };
    main_table[0x2E] = [this]() { reg.l = fetch(false); add_ticks(7); };
    main_table[0x3E] = [this]() { reg.a = fetch(false); add_ticks(7); };
    main_table[0x36] = [this]() { write_mem(reg.hl, fetch(false)); add_ticks(10); };
    
    // --- 16-bit Load Group ---
    main_table[0x01] = [this]() { reg.bc = fetch16(); add_ticks(10); };
    main_table[0x11] = [this]() { reg.de = fetch16(); add_ticks(10); };
    main_table[0x21] = [this]() { reg.hl = fetch16(); add_ticks(10); };
    main_table[0x31] = [this]() { reg.sp = fetch16(); add_ticks(10); };
    
    main_table[0x09] = [this]() { op_add16(reg.hl, reg.bc); add_ticks(11); };
    main_table[0x19] = [this]() { op_add16(reg.hl, reg.de); add_ticks(11); };
    main_table[0x29] = [this]() { op_add16(reg.hl, reg.hl); add_ticks(11); };
    main_table[0x39] = [this]() { op_add16(reg.hl, reg.sp); add_ticks(11); };
    
    main_table[0x0A] = [this]() { reg.a = read_mem(reg.bc); add_ticks(7); };
    main_table[0x1A] = [this]() { reg.a = read_mem(reg.de); add_ticks(7); };
    main_table[0x02] = [this]() { write_mem(reg.bc, reg.a); add_ticks(7); };
    main_table[0x12] = [this]() { write_mem(reg.de, reg.a); add_ticks(7); };
    
    main_table[0x2A] = [this]() { uint16_t addr = fetch16(); reg.hl = read_mem(addr) | (read_mem(addr + 1) << 8); add_ticks(16); };
    main_table[0x22] = [this]() { uint16_t addr = fetch16(); write_mem(addr, reg.hl & 0xFF); write_mem(addr + 1, reg.hl >> 8); add_ticks(16); };
    main_table[0x3A] = [this]() { uint16_t addr = fetch16(); reg.a = read_mem(addr); add_ticks(13); };
    main_table[0x32] = [this]() { uint16_t addr = fetch16(); write_mem(addr, reg.a); add_ticks(13); };
    
    // --- Exchange Operations ---
    main_table[0x08] = [this]() { op_ex_af(); add_ticks(4); };
    main_table[0xE3] = [this]() { uint16_t val = reg.hl; reg.hl = read_mem(reg.sp) | (read_mem(reg.sp + 1) << 8); write_mem(reg.sp, val & 0xFF); write_mem(reg.sp + 1, val >> 8); add_ticks(19); };
    main_table[0xE5] = [this]() { push(reg.hl); add_ticks(11); };
    main_table[0xD5] = [this]() { push(reg.de); add_ticks(11); };
    main_table[0xC5] = [this]() { push(reg.bc); add_ticks(11); };
    main_table[0xF5] = [this]() { push((static_cast<uint16_t>(reg.a) << 8) | reg.f); add_ticks(11); };
    main_table[0xE1] = [this]() { reg.hl = pop(); add_ticks(10); };
    main_table[0xD1] = [this]() { reg.de = pop(); add_ticks(10); };
    main_table[0xC1] = [this]() { reg.bc = pop(); add_ticks(10); };
    main_table[0xF1] = [this]() { uint16_t af = pop(); reg.f = af & 0xFF; reg.a = af >> 8; add_ticks(10); };  // POP AF
    main_table[0xEB] = [this]() { op_ex_de_hl(); add_ticks(4); };
    main_table[0xD9] = [this]() { op_exx(); add_ticks(4); };
    
    // --- Arithmetic Group ---
    main_table[0x80] = [this]() { op_add(reg.b); add_ticks(4); };
    main_table[0x81] = [this]() { op_add(reg.c); add_ticks(4); };
    main_table[0x82] = [this]() { op_add(reg.d); add_ticks(4); };
    main_table[0x83] = [this]() { op_add(reg.e); add_ticks(4); };
    main_table[0x84] = [this]() { op_add(reg.h); add_ticks(4); };
    main_table[0x85] = [this]() { op_add(reg.l); add_ticks(4); };
    main_table[0x86] = [this]() { op_add(read_mem(reg.hl)); add_ticks(7); };
    main_table[0x87] = [this]() { op_add(reg.a); add_ticks(4); };
    
    main_table[0x88] = [this]() { op_adc(reg.b); add_ticks(4); };
    main_table[0x89] = [this]() { op_adc(reg.c); add_ticks(4); };
    main_table[0x8A] = [this]() { op_adc(reg.d); add_ticks(4); };
    main_table[0x8B] = [this]() { op_adc(reg.e); add_ticks(4); };
    main_table[0x8C] = [this]() { op_adc(reg.h); add_ticks(4); };
    main_table[0x8D] = [this]() { op_adc(reg.l); add_ticks(4); };
    main_table[0x8E] = [this]() { op_adc(read_mem(reg.hl)); add_ticks(7); };
    main_table[0x8F] = [this]() { op_adc(reg.a); add_ticks(4); };
    
    main_table[0x90] = [this]() { op_sub(reg.b); add_ticks(4); };
    main_table[0x91] = [this]() { op_sub(reg.c); add_ticks(4); };
    main_table[0x92] = [this]() { op_sub(reg.d); add_ticks(4); };
    main_table[0x93] = [this]() { op_sub(reg.e); add_ticks(4); };
    main_table[0x94] = [this]() { op_sub(reg.h); add_ticks(4); };
    main_table[0x95] = [this]() { op_sub(reg.l); add_ticks(4); };
    main_table[0x96] = [this]() { op_sub(read_mem(reg.hl)); add_ticks(7); };
    main_table[0x97] = [this]() { op_sub(reg.a); add_ticks(4); };
    
    main_table[0x98] = [this]() { op_sbc(reg.b); add_ticks(4); };
    main_table[0x99] = [this]() { op_sbc(reg.c); add_ticks(4); };
    main_table[0x9A] = [this]() { op_sbc(reg.d); add_ticks(4); };
    main_table[0x9B] = [this]() { op_sbc(reg.e); add_ticks(4); };
    main_table[0x9C] = [this]() { op_sbc(reg.h); add_ticks(4); };
    main_table[0x9D] = [this]() { op_sbc(reg.l); add_ticks(4); };
    main_table[0x9E] = [this]() { op_sbc(read_mem(reg.hl)); add_ticks(7); };
    main_table[0x9F] = [this]() { op_sbc(reg.a); add_ticks(4); };
    
    main_table[0xA0] = [this]() { op_and(reg.b); add_ticks(4); };
    main_table[0xA1] = [this]() { op_and(reg.c); add_ticks(4); };
    main_table[0xA2] = [this]() { op_and(reg.d); add_ticks(4); };
    main_table[0xA3] = [this]() { op_and(reg.e); add_ticks(4); };
    main_table[0xA4] = [this]() { op_and(reg.h); add_ticks(4); };
    main_table[0xA5] = [this]() { op_and(reg.l); add_ticks(4); };
    main_table[0xA6] = [this]() { op_and(read_mem(reg.hl)); add_ticks(7); };
    main_table[0xA7] = [this]() { op_and(reg.a); add_ticks(4); };
    
    main_table[0xA8] = [this]() { op_xor(reg.b); add_ticks(4); };
    main_table[0xA9] = [this]() { op_xor(reg.c); add_ticks(4); };
    main_table[0xAA] = [this]() { op_xor(reg.d); add_ticks(4); };
    main_table[0xAB] = [this]() { op_xor(reg.e); add_ticks(4); };
    main_table[0xAC] = [this]() { op_xor(reg.h); add_ticks(4); };
    main_table[0xAD] = [this]() { op_xor(reg.l); add_ticks(4); };
    main_table[0xAE] = [this]() { op_xor(read_mem(reg.hl)); add_ticks(7); };
    main_table[0xAF] = [this]() { op_xor(reg.a); add_ticks(4); };
    
    main_table[0xB0] = [this]() { op_or(reg.b); add_ticks(4); };
    main_table[0xB1] = [this]() { op_or(reg.c); add_ticks(4); };
    main_table[0xB2] = [this]() { op_or(reg.d); add_ticks(4); };
    main_table[0xB3] = [this]() { op_or(reg.e); add_ticks(4); };
    main_table[0xB4] = [this]() { op_or(reg.h); add_ticks(4); };
    main_table[0xB5] = [this]() { op_or(reg.l); add_ticks(4); };
    main_table[0xB6] = [this]() { op_or(read_mem(reg.hl)); add_ticks(7); };
    main_table[0xB7] = [this]() { op_or(reg.a); add_ticks(4); };
    
    main_table[0xB8] = [this]() { op_cp(reg.b); add_ticks(4); };
    main_table[0xB9] = [this]() { op_cp(reg.c); add_ticks(4); };
    main_table[0xBA] = [this]() { op_cp(reg.d); add_ticks(4); };
    main_table[0xBB] = [this]() { op_cp(reg.e); add_ticks(4); };
    main_table[0xBC] = [this]() { op_cp(reg.h); add_ticks(4); };
    main_table[0xBD] = [this]() { op_cp(reg.l); add_ticks(4); };
    main_table[0xBE] = [this]() { op_cp(read_mem(reg.hl)); add_ticks(7); };
    main_table[0xBF] = [this]() { op_cp(reg.a); add_ticks(4); };
    
    // --- Immediate ALU ---
    main_table[0xC6] = [this]() { op_add(fetch(false)); add_ticks(7); };
    main_table[0xCE] = [this]() { op_adc(fetch(false)); add_ticks(7); };
    main_table[0xD6] = [this]() { op_sub(fetch(false)); add_ticks(7); };
    main_table[0xDE] = [this]() { op_sbc(fetch(false)); add_ticks(7); };
    main_table[0xE6] = [this]() { op_and(fetch(false)); add_ticks(7); };
    main_table[0xEE] = [this]() { op_xor(fetch(false)); add_ticks(7); };
    main_table[0xF6] = [this]() { op_or(fetch(false)); add_ticks(7); };
    main_table[0xFE] = [this]() { op_cp(fetch(false)); add_ticks(7); };
    
    // --- Increment/Decrement ---
    main_table[0x04] = [this]() { op_inc(reg.b); add_ticks(4); };
    main_table[0x0C] = [this]() { op_inc(reg.c); add_ticks(4); };
    main_table[0x14] = [this]() { op_inc(reg.d); add_ticks(4); };
    main_table[0x1C] = [this]() { op_inc(reg.e); add_ticks(4); };
    main_table[0x24] = [this]() { op_inc(reg.h); add_ticks(4); };
    main_table[0x2C] = [this]() { op_inc(reg.l); add_ticks(4); };
    main_table[0x34] = [this]() { uint8_t v = read_mem(reg.hl); op_inc(v); write_mem(reg.hl, v); add_ticks(11); };
    main_table[0x3C] = [this]() { op_inc(reg.a); add_ticks(4); };
    
    main_table[0x05] = [this]() { op_dec(reg.b); add_ticks(4); };
    main_table[0x0D] = [this]() { op_dec(reg.c); add_ticks(4); };
    main_table[0x15] = [this]() { op_dec(reg.d); add_ticks(4); };
    main_table[0x1D] = [this]() { op_dec(reg.e); add_ticks(4); };
    main_table[0x25] = [this]() { op_dec(reg.h); add_ticks(4); };
    main_table[0x2D] = [this]() { op_dec(reg.l); add_ticks(4); };
    main_table[0x35] = [this]() { uint8_t v = read_mem(reg.hl); op_dec(v); write_mem(reg.hl, v); add_ticks(11); };
    main_table[0x3D] = [this]() { op_dec(reg.a); add_ticks(4); };
    
    main_table[0x03] = [this]() { op_inc16(reg.bc); add_ticks(6); };
    main_table[0x13] = [this]() { op_inc16(reg.de); add_ticks(6); };
    main_table[0x23] = [this]() { op_inc16(reg.hl); add_ticks(6); };
    main_table[0x33] = [this]() { op_inc16(reg.sp); add_ticks(6); };
    
    main_table[0x0B] = [this]() { op_dec16(reg.bc); add_ticks(6); };
    main_table[0x1B] = [this]() { op_dec16(reg.de); add_ticks(6); };
    main_table[0x2B] = [this]() { op_dec16(reg.hl); add_ticks(6); };
    main_table[0x3B] = [this]() { op_dec16(reg.sp); add_ticks(6); };
    
    // --- General Purpose Arithmetic ---
    main_table[0x27] = [this]() { op_daa(); add_ticks(4); };
    main_table[0x2F] = [this]() { reg.a = ~reg.a; set_hf(true); set_nf(true); set_f35(reg.a); add_ticks(4); };  // CPL
    main_table[0x3F] = [this]() { bool old_c = get_flag(FLAG_C); set_hf(old_c); set_cf(!old_c); set_nf(false); set_f35(reg.a); add_ticks(4); };  // CCF
    main_table[0x37] = [this]() { set_cf(true); set_hf(false); set_nf(false); set_f35(reg.a); add_ticks(4); };  // SCF
    
    // --- Rotate Accumulator ---
    main_table[0x07] = [this]() { bool c = reg.a & 0x80; reg.a = (reg.a << 1) | (c ? 1 : 0); set_cf(c); set_hf(false); set_nf(false); set_f35(reg.a); add_ticks(4); };  // RLCA
    main_table[0x0F] = [this]() { bool c = reg.a & 0x01; reg.a = (reg.a >> 1) | (c ? 0x80 : 0); set_cf(c); set_hf(false); set_nf(false); set_f35(reg.a); add_ticks(4); };  // RRCA
    main_table[0x17] = [this]() { op_rla(); add_ticks(4); };  // RLA
    main_table[0x1F] = [this]() { op_rra(); add_ticks(4); };  // RRA
    
    // --- DJNZ ---
    main_table[0x10] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.b--; if (reg.b != 0) { reg.pc += d; add_ticks(13); } else { add_ticks(8); } };
    
    // --- Jump Group ---
    main_table[0xC3] = [this]() { op_jp(); };
    main_table[0xC2] = [this]() { uint16_t addr = fetch16(); if (!get_flag(FLAG_Z)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    main_table[0xCA] = [this]() { uint16_t addr = fetch16(); if (get_flag(FLAG_Z)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    main_table[0xD2] = [this]() { uint16_t addr = fetch16(); if (!get_flag(FLAG_C)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    main_table[0xDA] = [this]() { uint16_t addr = fetch16(); if (get_flag(FLAG_C)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    main_table[0xE2] = [this]() { uint16_t addr = fetch16(); if (!get_flag(FLAG_P)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    main_table[0xEA] = [this]() { uint16_t addr = fetch16(); if (get_flag(FLAG_P)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    main_table[0xF2] = [this]() { uint16_t addr = fetch16(); if (!get_flag(FLAG_S)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    main_table[0xFA] = [this]() { uint16_t addr = fetch16(); if (get_flag(FLAG_S)) reg.pc = addr; else add_ticks(0); add_ticks(10); };
    
    main_table[0x18] = [this]() { op_jr(); };
    main_table[0x20] = [this]() { int8_t offset = static_cast<int8_t>(fetch(false)); if (!get_flag(FLAG_Z)) { reg.pc += offset; add_ticks(12); } else { add_ticks(7); } };
    main_table[0x28] = [this]() { int8_t offset = static_cast<int8_t>(fetch(false)); if (get_flag(FLAG_Z)) { reg.pc += offset; add_ticks(12); } else { add_ticks(7); } };
    main_table[0x30] = [this]() { int8_t offset = static_cast<int8_t>(fetch(false)); if (!get_flag(FLAG_C)) { reg.pc += offset; add_ticks(12); } else { add_ticks(7); } };
    main_table[0x38] = [this]() { int8_t offset = static_cast<int8_t>(fetch(false)); if (get_flag(FLAG_C)) { reg.pc += offset; add_ticks(12); } else { add_ticks(7); } };
    
    main_table[0xE9] = [this]() { reg.pc = reg.hl; add_ticks(4); };  // JP (HL)
    
    // --- Call/Return Group ---
    main_table[0xCD] = [this]() { op_call(); };
    main_table[0xC4] = [this]() { uint16_t addr = fetch16(); if (!get_flag(FLAG_Z)) { push(reg.pc); reg.pc = addr; add_ticks(17); } else { add_ticks(10); } };
    main_table[0xCC] = [this]() { uint16_t addr = fetch16(); if (get_flag(FLAG_Z)) { push(reg.pc); reg.pc = addr; add_ticks(17); } else { add_ticks(10); } };
    main_table[0xD4] = [this]() { uint16_t addr = fetch16(); if (!get_flag(FLAG_C)) { push(reg.pc); reg.pc = addr; add_ticks(17); } else { add_ticks(10); } };
    main_table[0xDC] = [this]() { uint16_t addr = fetch16(); if (get_flag(FLAG_C)) { push(reg.pc); reg.pc = addr; add_ticks(17); } else { add_ticks(10); } };
    
    main_table[0xC9] = [this]() { op_ret(); };
    main_table[0xC0] = [this]() { if (!get_flag(FLAG_Z)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };
    main_table[0xC8] = [this]() { if (get_flag(FLAG_Z)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };
    main_table[0xD0] = [this]() { if (!get_flag(FLAG_C)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };
    main_table[0xD8] = [this]() { if (get_flag(FLAG_C)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };
    main_table[0xE0] = [this]() { if (!get_flag(FLAG_P)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };  // RET PO
    main_table[0xE8] = [this]() { if (get_flag(FLAG_P)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };   // RET PE
    main_table[0xF0] = [this]() { if (!get_flag(FLAG_S)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };  // RET P
    main_table[0xF8] = [this]() { if (get_flag(FLAG_S)) { op_ret(); add_ticks(1); } else { add_ticks(5); } };   // RET M
    
    main_table[0xE4] = [this]() { uint16_t a = fetch16(); if (!get_flag(FLAG_P)) { push(reg.pc); reg.pc = a; add_ticks(17); } else { add_ticks(10); } };  // CALL PO
    main_table[0xEC] = [this]() { uint16_t a = fetch16(); if (get_flag(FLAG_P)) { push(reg.pc); reg.pc = a; add_ticks(17); } else { add_ticks(10); } };   // CALL PE
    main_table[0xF4] = [this]() { uint16_t a = fetch16(); if (!get_flag(FLAG_S)) { push(reg.pc); reg.pc = a; add_ticks(17); } else { add_ticks(10); } };  // CALL P
    main_table[0xFC] = [this]() { uint16_t a = fetch16(); if (get_flag(FLAG_S)) { push(reg.pc); reg.pc = a; add_ticks(17); } else { add_ticks(10); } };   // CALL M
    
    main_table[0xED] = [this]() { prefix = 0xED; add_ticks(4); };  // ED prefix
    main_table[0xDD] = [this]() { prefix = 0xDD; add_ticks(4); };  // DD prefix
    main_table[0xFD] = [this]() { prefix = 0xFD; add_ticks(4); };  // FD prefix
    
    // --- Restart ---
    main_table[0xC7] = [this]() { op_rst(0x00); };
    main_table[0xCF] = [this]() { op_rst(0x08); };
    main_table[0xD7] = [this]() { op_rst(0x10); };
    main_table[0xDF] = [this]() { op_rst(0x18); };
    main_table[0xE7] = [this]() { op_rst(0x20); };
    main_table[0xEF] = [this]() { op_rst(0x28); };
    main_table[0xF7] = [this]() { op_rst(0x30); };
    main_table[0xFF] = [this]() { op_rst(0x38); };
    
    // --- I/O Group ---
    main_table[0xD3] = [this]() { uint8_t port = fetch(false); bus.write_port(port, reg.a); add_ticks(11); };  // OUT (n), A
    main_table[0xDB] = [this]() { uint8_t port = fetch(false); reg.a = bus.read_port(port); add_ticks(11); };  // IN A, (n)
    
    // --- LD SP,HL ---
    main_table[0xF9] = [this]() { reg.sp = reg.hl; add_ticks(6); };
    
    // --- Special ---
    main_table[0x00] = [this]() { op_nop(); add_ticks(4); };
    main_table[0x76] = [this]() { op_halt(); add_ticks(4); };
    main_table[0xF3] = [this]() { op_di(); add_ticks(4); };
    main_table[0xFB] = [this]() { op_ei(); add_ticks(4); };
    
    // --- CB Prefix ---
    main_table[0xCB] = [this]() { prefix = 0xCB; add_ticks(4); };
}

// ============================================================================
// CB PREFIX TABLE (Bit Operations - 0x00 to 0xFF)
// ============================================================================
void Z80::init_cb_table() {
    for (auto& op : cb_table) {
        op = [this]() { add_ticks(4); };
    }
    
    // RLC r (0x00-0x07)
    cb_table[0x00] = [this]() { op_rlc(reg.b); add_ticks(4); };
    cb_table[0x01] = [this]() { op_rlc(reg.c); add_ticks(4); };
    cb_table[0x02] = [this]() { op_rlc(reg.d); add_ticks(4); };
    cb_table[0x03] = [this]() { op_rlc(reg.e); add_ticks(4); };
    cb_table[0x04] = [this]() { op_rlc(reg.h); add_ticks(4); };
    cb_table[0x05] = [this]() { op_rlc(reg.l); add_ticks(4); };
    cb_table[0x06] = [this]() { uint8_t v = read_mem(reg.hl); op_rlc(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x07] = [this]() { op_rlc(reg.a); add_ticks(4); };
    
    // RRC r (0x08-0x0F)
    cb_table[0x08] = [this]() { op_rrc(reg.b); add_ticks(4); };
    cb_table[0x09] = [this]() { op_rrc(reg.c); add_ticks(4); };
    cb_table[0x0A] = [this]() { op_rrc(reg.d); add_ticks(4); };
    cb_table[0x0B] = [this]() { op_rrc(reg.e); add_ticks(4); };
    cb_table[0x0C] = [this]() { op_rrc(reg.h); add_ticks(4); };
    cb_table[0x0D] = [this]() { op_rrc(reg.l); add_ticks(4); };
    cb_table[0x0E] = [this]() { uint8_t v = read_mem(reg.hl); op_rrc(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x0F] = [this]() { op_rrc(reg.a); add_ticks(4); };
    
    // RL r (0x10-0x17)
    cb_table[0x10] = [this]() { op_rl(reg.b); add_ticks(4); };
    cb_table[0x11] = [this]() { op_rl(reg.c); add_ticks(4); };
    cb_table[0x12] = [this]() { op_rl(reg.d); add_ticks(4); };
    cb_table[0x13] = [this]() { op_rl(reg.e); add_ticks(4); };
    cb_table[0x14] = [this]() { op_rl(reg.h); add_ticks(4); };
    cb_table[0x15] = [this]() { op_rl(reg.l); add_ticks(4); };
    cb_table[0x16] = [this]() { uint8_t v = read_mem(reg.hl); op_rl(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x17] = [this]() { op_rl(reg.a); add_ticks(4); };
    
    // RR r (0x18-0x1F)
    cb_table[0x18] = [this]() { op_rr(reg.b); add_ticks(4); };
    cb_table[0x19] = [this]() { op_rr(reg.c); add_ticks(4); };
    cb_table[0x1A] = [this]() { op_rr(reg.d); add_ticks(4); };
    cb_table[0x1B] = [this]() { op_rr(reg.e); add_ticks(4); };
    cb_table[0x1C] = [this]() { op_rr(reg.h); add_ticks(4); };
    cb_table[0x1D] = [this]() { op_rr(reg.l); add_ticks(4); };
    cb_table[0x1E] = [this]() { uint8_t v = read_mem(reg.hl); op_rr(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x1F] = [this]() { op_rr(reg.a); add_ticks(4); };
    
    // SLA r (0x20-0x27)
    cb_table[0x20] = [this]() { op_sl(reg.b); add_ticks(4); };
    cb_table[0x21] = [this]() { op_sl(reg.c); add_ticks(4); };
    cb_table[0x22] = [this]() { op_sl(reg.d); add_ticks(4); };
    cb_table[0x23] = [this]() { op_sl(reg.e); add_ticks(4); };
    cb_table[0x24] = [this]() { op_sl(reg.h); add_ticks(4); };
    cb_table[0x25] = [this]() { op_sl(reg.l); add_ticks(4); };
    cb_table[0x26] = [this]() { uint8_t v = read_mem(reg.hl); op_sl(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x27] = [this]() { op_sl(reg.a); add_ticks(4); };
    
    // SRA r (0x28-0x2F)
    cb_table[0x28] = [this]() { op_sr(reg.b); add_ticks(4); };
    cb_table[0x29] = [this]() { op_sr(reg.c); add_ticks(4); };
    cb_table[0x2A] = [this]() { op_sr(reg.d); add_ticks(4); };
    cb_table[0x2B] = [this]() { op_sr(reg.e); add_ticks(4); };
    cb_table[0x2C] = [this]() { op_sr(reg.h); add_ticks(4); };
    cb_table[0x2D] = [this]() { op_sr(reg.l); add_ticks(4); };
    cb_table[0x2E] = [this]() { uint8_t v = read_mem(reg.hl); op_sr(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x2F] = [this]() { op_sr(reg.a); add_ticks(4); };
    
    // SLL r (0x30-0x37) - undocumented: shift left, bit 0 = 1
    cb_table[0x30] = [this]() { bool c = reg.b & 0x80; reg.b = (reg.b << 1) | 1; set_cf(c); set_sf(reg.b); set_zf(reg.b); set_hf(false); set_pf(reg.b); set_nf(false); set_f35(reg.b); add_ticks(4); };
    cb_table[0x31] = [this]() { bool c = reg.c & 0x80; reg.c = (reg.c << 1) | 1; set_cf(c); set_sf(reg.c); set_zf(reg.c); set_hf(false); set_pf(reg.c); set_nf(false); set_f35(reg.c); add_ticks(4); };
    cb_table[0x32] = [this]() { bool c = reg.d & 0x80; reg.d = (reg.d << 1) | 1; set_cf(c); set_sf(reg.d); set_zf(reg.d); set_hf(false); set_pf(reg.d); set_nf(false); set_f35(reg.d); add_ticks(4); };
    cb_table[0x33] = [this]() { bool c = reg.e & 0x80; reg.e = (reg.e << 1) | 1; set_cf(c); set_sf(reg.e); set_zf(reg.e); set_hf(false); set_pf(reg.e); set_nf(false); set_f35(reg.e); add_ticks(4); };
    cb_table[0x34] = [this]() { bool c = reg.h & 0x80; reg.h = (reg.h << 1) | 1; set_cf(c); set_sf(reg.h); set_zf(reg.h); set_hf(false); set_pf(reg.h); set_nf(false); set_f35(reg.h); add_ticks(4); };
    cb_table[0x35] = [this]() { bool c = reg.l & 0x80; reg.l = (reg.l << 1) | 1; set_cf(c); set_sf(reg.l); set_zf(reg.l); set_hf(false); set_pf(reg.l); set_nf(false); set_f35(reg.l); add_ticks(4); };
    cb_table[0x36] = [this]() { uint8_t v = read_mem(reg.hl); bool c = v & 0x80; v = (v << 1) | 1; set_cf(c); set_sf(v); set_zf(v); set_hf(false); set_pf(v); set_nf(false); set_f35(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x37] = [this]() { bool c = reg.a & 0x80; reg.a = (reg.a << 1) | 1; set_cf(c); set_sf(reg.a); set_zf(reg.a); set_hf(false); set_pf(reg.a); set_nf(false); set_f35(reg.a); add_ticks(4); };

    // SRL r (0x38-0x3F)
    cb_table[0x38] = [this]() { bool c = reg.b & 1; reg.b >>= 1; set_cf(c); set_sf(reg.b); set_zf(reg.b); set_hf(false); set_pf(reg.b); set_nf(false); set_f35(reg.b); add_ticks(4); };
    cb_table[0x39] = [this]() { bool c = reg.c & 1; reg.c >>= 1; set_cf(c); set_sf(reg.c); set_zf(reg.c); set_hf(false); set_pf(reg.c); set_nf(false); set_f35(reg.c); add_ticks(4); };
    cb_table[0x3A] = [this]() { bool c = reg.d & 1; reg.d >>= 1; set_cf(c); set_sf(reg.d); set_zf(reg.d); set_hf(false); set_pf(reg.d); set_nf(false); set_f35(reg.d); add_ticks(4); };
    cb_table[0x3B] = [this]() { bool c = reg.e & 1; reg.e >>= 1; set_cf(c); set_sf(reg.e); set_zf(reg.e); set_hf(false); set_pf(reg.e); set_nf(false); set_f35(reg.e); add_ticks(4); };
    cb_table[0x3C] = [this]() { bool c = reg.h & 1; reg.h >>= 1; set_cf(c); set_sf(reg.h); set_zf(reg.h); set_hf(false); set_pf(reg.h); set_nf(false); set_f35(reg.h); add_ticks(4); };
    cb_table[0x3D] = [this]() { bool c = reg.l & 1; reg.l >>= 1; set_cf(c); set_sf(reg.l); set_zf(reg.l); set_hf(false); set_pf(reg.l); set_nf(false); set_f35(reg.l); add_ticks(4); };
    cb_table[0x3E] = [this]() { uint8_t v = read_mem(reg.hl); bool c = v & 1; v >>= 1; set_cf(c); set_sf(v); set_zf(v); set_hf(false); set_pf(v); set_nf(false); set_f35(v); write_mem(reg.hl, v); add_ticks(11); };
    cb_table[0x3F] = [this]() { bool c = reg.a & 1; reg.a >>= 1; set_cf(c); set_sf(reg.a); set_zf(reg.a); set_hf(false); set_pf(reg.a); set_nf(false); set_f35(reg.a); add_ticks(4); };
    
    // BIT b, r (0x40-0x7F)
    for (uint8_t bit = 0; bit < 8; bit++) {
        for (uint8_t reg_code = 0; reg_code < 8; reg_code++) {
            uint8_t opcode = 0x40 | (bit << 3) | reg_code;
            cb_table[opcode] = [this, bit, reg_code]() {
                uint8_t val = 0;
                if (reg_code == 6) {
                    val = read_mem(reg.hl);
                    op_bit(bit, val);
                    // For BIT on (HL), F3/F5 come from high byte of address
                    set_f35(reg.h);
                    add_ticks(8);
                } else {
                    val = get_reg_8(reg_code);
                    op_bit(bit, val);
                    add_ticks(4);
                }
            };
        }
    }
    
    // RES b, r (0x80-0xBF)
    for (uint8_t bit = 0; bit < 8; bit++) {
        for (uint8_t reg_code = 0; reg_code < 8; reg_code++) {
            uint8_t opcode = 0x80 | (bit << 3) | reg_code;
            cb_table[opcode] = [this, bit, reg_code]() {
                if (reg_code == 6) {
                    uint8_t v = read_mem(reg.hl);
                    op_res(bit, v);
                    write_mem(reg.hl, v);
                    add_ticks(11);
                } else {
                    op_res(bit, get_reg_8(reg_code));
                    add_ticks(4);
                }
            };
        }
    }
    
    // SET b, r (0xC0-0xFF)
    for (uint8_t bit = 0; bit < 8; bit++) {
        for (uint8_t reg_code = 0; reg_code < 8; reg_code++) {
            uint8_t opcode = 0xC0 | (bit << 3) | reg_code;
            cb_table[opcode] = [this, bit, reg_code]() {
                if (reg_code == 6) {
                    uint8_t v = read_mem(reg.hl);
                    op_set(bit, v);
                    write_mem(reg.hl, v);
                    add_ticks(11);
                } else {
                    op_set(bit, get_reg_8(reg_code));
                    add_ticks(4);
                }
            };
        }
    }
}

// ============================================================================
// ED PREFIX TABLE (Extended Operations)
// ============================================================================
void Z80::init_ed_table() {
    for (auto& op : ed_table) {
        op = [this]() { add_ticks(4); };  // Unknown/Invalid = NOP (8 T)
    }

    // ---- IN r, (C) ----
    auto ed_in = [this](uint8_t& r) {
        r = bus.read_port(reg.c);
        set_sf(r); set_zf(r); set_hf(false); set_pf(r); set_nf(false);
        set_f35(r);
        add_ticks(8);
    };
    ed_table[0x40] = [this, ed_in]() { ed_in(reg.b); };
    ed_table[0x48] = [this, ed_in]() { ed_in(reg.c); };
    ed_table[0x50] = [this, ed_in]() { ed_in(reg.d); };
    ed_table[0x58] = [this, ed_in]() { ed_in(reg.e); };
    ed_table[0x60] = [this, ed_in]() { ed_in(reg.h); };
    ed_table[0x68] = [this, ed_in]() { ed_in(reg.l); };
    ed_table[0x70] = [this]() { uint8_t tmp = bus.read_port(reg.c); set_sf(tmp); set_zf(tmp); set_hf(false); set_pf(tmp); set_nf(false); set_f35(tmp); add_ticks(8); }; // IN (C) — flags only
    ed_table[0x78] = [this, ed_in]() { ed_in(reg.a); };

    // ---- OUT (C), r ----
    ed_table[0x41] = [this]() { bus.write_port(reg.c, reg.b); add_ticks(8); };
    ed_table[0x49] = [this]() { bus.write_port(reg.c, reg.c); add_ticks(8); };
    ed_table[0x51] = [this]() { bus.write_port(reg.c, reg.d); add_ticks(8); };
    ed_table[0x59] = [this]() { bus.write_port(reg.c, reg.e); add_ticks(8); };
    ed_table[0x61] = [this]() { bus.write_port(reg.c, reg.h); add_ticks(8); };
    ed_table[0x69] = [this]() { bus.write_port(reg.c, reg.l); add_ticks(8); };
    ed_table[0x71] = [this]() { bus.write_port(reg.c, 0); add_ticks(8); }; // OUT (C), 0
    ed_table[0x79] = [this]() { bus.write_port(reg.c, reg.a); add_ticks(8); };

    // ---- SBC HL, rr ----
    auto ed_sbc_hl = [this](uint16_t val) {
        uint8_t carry = get_flag(FLAG_C) ? 1 : 0;
        uint32_t result = reg.hl - val - carry;
        bool hc = (reg.hl & 0x0FFF) < (val & 0x0FFF) + carry;
        // Overflow: sign of result differs when operands had different signs
        bool ov = ((reg.hl ^ val) & 0x8000) && ((reg.hl ^ result) & 0x8000);
        reg.hl = result & 0xFFFF;
        set_sf(reg.hl >> 8);
        set_zf(reg.hl == 0 ? 0 : 1); // Z set if HL==0
        set_flag(FLAG_Z, reg.hl == 0);
        set_hf(hc);
        set_flag(FLAG_P, ov);
        set_nf(true);
        set_cf(result > 0xFFFF);
        set_f35(reg.hl >> 8);
        add_ticks(11);
    };
    ed_table[0x42] = [this, ed_sbc_hl]() { ed_sbc_hl(reg.bc); };
    ed_table[0x52] = [this, ed_sbc_hl]() { ed_sbc_hl(reg.de); };
    ed_table[0x62] = [this, ed_sbc_hl]() { ed_sbc_hl(reg.hl); };
    ed_table[0x72] = [this, ed_sbc_hl]() { ed_sbc_hl(reg.sp); };

    // ---- ADC HL, rr ----
    auto ed_adc_hl = [this](uint16_t val) {
        uint8_t carry = get_flag(FLAG_C) ? 1 : 0;
        uint32_t result = reg.hl + val + carry;
        bool hc = (reg.hl & 0x0FFF) + (val & 0x0FFF) + carry > 0x0FFF;
        bool ov = !((reg.hl ^ val) & 0x8000) && ((reg.hl ^ result) & 0x8000);
        reg.hl = result & 0xFFFF;
        set_sf(reg.hl >> 8);
        set_flag(FLAG_Z, reg.hl == 0);
        set_hf(hc);
        set_flag(FLAG_P, ov);
        set_nf(false);
        set_cf(result > 0xFFFF);
        set_f35(reg.hl >> 8);
        add_ticks(11);
    };
    ed_table[0x4A] = [this, ed_adc_hl]() { ed_adc_hl(reg.bc); };
    ed_table[0x5A] = [this, ed_adc_hl]() { ed_adc_hl(reg.de); };
    ed_table[0x6A] = [this, ed_adc_hl]() { ed_adc_hl(reg.hl); };
    ed_table[0x7A] = [this, ed_adc_hl]() { ed_adc_hl(reg.sp); };

    // ---- LD (nn), rr ----
    ed_table[0x43] = [this]() { uint16_t addr = fetch16(); write_mem(addr, reg.c); write_mem(addr+1, reg.b); add_ticks(16); };
    ed_table[0x53] = [this]() { uint16_t addr = fetch16(); write_mem(addr, reg.e); write_mem(addr+1, reg.d); add_ticks(16); };
    ed_table[0x63] = [this]() { uint16_t addr = fetch16(); write_mem(addr, reg.l); write_mem(addr+1, reg.h); add_ticks(16); };
    ed_table[0x73] = [this]() { uint16_t addr = fetch16(); write_mem(addr, reg.sp & 0xFF); write_mem(addr+1, reg.sp >> 8); add_ticks(16); };

    // ---- LD rr, (nn) ----
    ed_table[0x4B] = [this]() { uint16_t addr = fetch16(); reg.c = read_mem(addr); reg.b = read_mem(addr+1); add_ticks(16); };
    ed_table[0x5B] = [this]() { uint16_t addr = fetch16(); reg.e = read_mem(addr); reg.d = read_mem(addr+1); add_ticks(16); };
    ed_table[0x6B] = [this]() { uint16_t addr = fetch16(); reg.l = read_mem(addr); reg.h = read_mem(addr+1); add_ticks(16); };
    ed_table[0x7B] = [this]() { uint16_t addr = fetch16(); uint8_t lo = read_mem(addr); uint8_t hi = read_mem(addr+1); reg.sp = (hi << 8) | lo; add_ticks(16); };

    // ---- NEG ----
    ed_table[0x44] = [this]() {
        uint8_t old = reg.a;
        reg.a = 0 - reg.a;
        set_sf(reg.a);
        set_zf(reg.a);
        set_hf((0 & 0x0F) < (old & 0x0F));
        set_flag(FLAG_P, old == 0x80);  // Overflow if A was 0x80
        set_nf(true);
        set_cf(old != 0);
        set_f35(reg.a);
        add_ticks(4);
    };

    // ---- RETN ----
    ed_table[0x45] = [this]() { reg.pc = pop(); reg.iff1 = reg.iff2; add_ticks(10); };

    // ---- RETI ----
    ed_table[0x4D] = [this]() { reg.pc = pop(); reg.iff1 = reg.iff2; add_ticks(10); };

    // ---- Interrupt Mode ----
    ed_table[0x46] = [this]() { reg.im = 0; add_ticks(4); };
    ed_table[0x56] = [this]() { reg.im = 1; add_ticks(4); };
    ed_table[0x5E] = [this]() { reg.im = 2; add_ticks(4); };

    // ---- LD I,A / LD R,A / LD A,I / LD A,R ----
    ed_table[0x47] = [this]() { reg.i = reg.a; add_ticks(5); };
    ed_table[0x4F] = [this]() { reg.r = reg.a; add_ticks(5); };
    ed_table[0x57] = [this]() {
        reg.a = reg.i;
        set_sf(reg.a); set_zf(reg.a); set_hf(false); set_nf(false);
        set_flag(FLAG_P, reg.iff2);
        set_f35(reg.a);
        add_ticks(5);
    };
    ed_table[0x5F] = [this]() {
        reg.a = reg.r;
        set_sf(reg.a); set_zf(reg.a); set_hf(false); set_nf(false);
        set_flag(FLAG_P, reg.iff2);
        set_f35(reg.a);
        add_ticks(5);
    };

    // ---- RRD ----
    ed_table[0x67] = [this]() {
        uint8_t mem = read_mem(reg.hl);
        uint8_t lo_a = reg.a & 0x0F;
        reg.a = (reg.a & 0xF0) | (mem & 0x0F);
        mem = (lo_a << 4) | (mem >> 4);
        write_mem(reg.hl, mem);
        set_sf(reg.a); set_zf(reg.a); set_hf(false); set_pf(reg.a); set_nf(false);
        set_f35(reg.a);
        add_ticks(14);
    };

    // ---- RLD ----
    ed_table[0x6F] = [this]() {
        uint8_t mem = read_mem(reg.hl);
        uint8_t lo_a = reg.a & 0x0F;
        reg.a = (reg.a & 0xF0) | (mem >> 4);
        mem = (mem << 4) | lo_a;
        write_mem(reg.hl, mem);
        set_sf(reg.a); set_zf(reg.a); set_hf(false); set_pf(reg.a); set_nf(false);
        set_f35(reg.a);
        add_ticks(14);
    };

    // ==== BLOCK OPERATIONS ====

    // ---- LDI (ED A0) ----
    ed_table[0xA0] = [this]() {
        uint8_t val = read_mem(reg.hl);
        write_mem(reg.de, val);
        reg.hl++; reg.de++; reg.bc--;
        set_hf(false); set_nf(false);
        set_flag(FLAG_P, reg.bc != 0);
        uint8_t n = reg.a + val;
        set_flag(FLAG_F5, n & 0x02);  // bit 1 -> flag bit 5
        set_flag(FLAG_F3, n & 0x08);  // bit 3 -> flag bit 3
        add_ticks(12);
    };

    // ---- LDIR (ED B0) ----
    ed_table[0xB0] = [this]() {
        uint8_t val = read_mem(reg.hl);
        write_mem(reg.de, val);
        reg.hl++; reg.de++; reg.bc--;
        set_hf(false); set_nf(false);
        set_flag(FLAG_P, false);
        uint8_t n = reg.a + val;
        set_flag(FLAG_F5, n & 0x02);
        set_flag(FLAG_F3, n & 0x08);
        if (reg.bc != 0) {
            reg.pc -= 2;  // Repeat instruction
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };

    // ---- LDD (ED A8) ----
    ed_table[0xA8] = [this]() {
        uint8_t val = read_mem(reg.hl);
        write_mem(reg.de, val);
        reg.hl--; reg.de--; reg.bc--;
        set_hf(false); set_nf(false);
        set_flag(FLAG_P, reg.bc != 0);
        uint8_t n = reg.a + val;
        set_flag(FLAG_F5, n & 0x02);
        set_flag(FLAG_F3, n & 0x08);
        add_ticks(12);
    };

    // ---- LDDR (ED B8) ----
    ed_table[0xB8] = [this]() {
        uint8_t val = read_mem(reg.hl);
        write_mem(reg.de, val);
        reg.hl--; reg.de--; reg.bc--;
        set_hf(false); set_nf(false);
        set_flag(FLAG_P, false);
        uint8_t n = reg.a + val;
        set_flag(FLAG_F5, n & 0x02);
        set_flag(FLAG_F3, n & 0x08);
        if (reg.bc != 0) {
            reg.pc -= 2;
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };

    // ---- CPI (ED A1) ----
    ed_table[0xA1] = [this]() {
        uint8_t val = read_mem(reg.hl);
        uint8_t result = reg.a - val;
        bool hc = (reg.a & 0x0F) < (val & 0x0F);
        reg.hl++; reg.bc--;
        set_sf(result); set_zf(result);
        set_hf(hc);
        set_nf(true);
        set_flag(FLAG_P, reg.bc != 0);
        uint8_t n = result - (hc ? 1 : 0);
        set_flag(FLAG_F5, n & 0x02);
        set_flag(FLAG_F3, n & 0x08);
        add_ticks(12);
    };

    // ---- CPIR (ED B1) ----
    ed_table[0xB1] = [this]() {
        uint8_t val = read_mem(reg.hl);
        uint8_t result = reg.a - val;
        bool hc = (reg.a & 0x0F) < (val & 0x0F);
        reg.hl++; reg.bc--;
        set_sf(result); set_zf(result);
        set_hf(hc);
        set_nf(true);
        set_flag(FLAG_P, reg.bc != 0);
        uint8_t n = result - (hc ? 1 : 0);
        set_flag(FLAG_F5, n & 0x02);
        set_flag(FLAG_F3, n & 0x08);
        if (reg.bc != 0 && result != 0) {
            reg.pc -= 2;
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };

    // ---- CPD (ED A9) ----
    ed_table[0xA9] = [this]() {
        uint8_t val = read_mem(reg.hl);
        uint8_t result = reg.a - val;
        bool hc = (reg.a & 0x0F) < (val & 0x0F);
        reg.hl--; reg.bc--;
        set_sf(result); set_zf(result);
        set_hf(hc);
        set_nf(true);
        set_flag(FLAG_P, reg.bc != 0);
        uint8_t n = result - (hc ? 1 : 0);
        set_flag(FLAG_F5, n & 0x02);
        set_flag(FLAG_F3, n & 0x08);
        add_ticks(12);
    };

    // ---- CPDR (ED B9) ----
    ed_table[0xB9] = [this]() {
        uint8_t val = read_mem(reg.hl);
        uint8_t result = reg.a - val;
        bool hc = (reg.a & 0x0F) < (val & 0x0F);
        reg.hl--; reg.bc--;
        set_sf(result); set_zf(result);
        set_hf(hc);
        set_nf(true);
        set_flag(FLAG_P, reg.bc != 0);
        uint8_t n = result - (hc ? 1 : 0);
        set_flag(FLAG_F5, n & 0x02);
        set_flag(FLAG_F3, n & 0x08);
        if (reg.bc != 0 && result != 0) {
            reg.pc -= 2;
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };

    // ---- INI (ED A2) ----
    ed_table[0xA2] = [this]() {
        uint8_t val = bus.read_port(reg.c);
        write_mem(reg.hl, val);
        reg.hl++; reg.b--;
        set_zf(reg.b); set_nf(true);
        add_ticks(12);
    };

    // ---- INIR (ED B2) ----
    ed_table[0xB2] = [this]() {
        uint8_t val = bus.read_port(reg.c);
        write_mem(reg.hl, val);
        reg.hl++; reg.b--;
        set_zf(reg.b); set_nf(true);
        if (reg.b != 0) {
            reg.pc -= 2;
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };

    // ---- IND (ED AA) ----
    ed_table[0xAA] = [this]() {
        uint8_t val = bus.read_port(reg.c);
        write_mem(reg.hl, val);
        reg.hl--; reg.b--;
        set_zf(reg.b); set_nf(true);
        add_ticks(12);
    };

    // ---- INDR (ED BA) ----
    ed_table[0xBA] = [this]() {
        uint8_t val = bus.read_port(reg.c);
        write_mem(reg.hl, val);
        reg.hl--; reg.b--;
        set_zf(reg.b); set_nf(true);
        if (reg.b != 0) {
            reg.pc -= 2;
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };

    // ---- OUTI (ED A3) ----
    ed_table[0xA3] = [this]() {
        uint8_t val = read_mem(reg.hl);
        bus.write_port(reg.c, val);
        reg.hl++; reg.b--;
        set_zf(reg.b); set_nf(true);
        add_ticks(12);
    };

    // ---- OTIR (ED B3) ----
    ed_table[0xB3] = [this]() {
        uint8_t val = read_mem(reg.hl);
        bus.write_port(reg.c, val);
        reg.hl++; reg.b--;
        set_zf(reg.b); set_nf(true);
        if (reg.b != 0) {
            reg.pc -= 2;
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };

    // ---- OUTD (ED AB) ----
    ed_table[0xAB] = [this]() {
        uint8_t val = read_mem(reg.hl);
        bus.write_port(reg.c, val);
        reg.hl--; reg.b--;
        set_zf(reg.b); set_nf(true);
        add_ticks(12);
    };

    // ---- OTDR (ED BB) ----
    ed_table[0xBB] = [this]() {
        uint8_t val = read_mem(reg.hl);
        bus.write_port(reg.c, val);
        reg.hl--; reg.b--;
        set_zf(reg.b); set_nf(true);
        if (reg.b != 0) {
            reg.pc -= 2;
            add_ticks(17);
        } else {
            add_ticks(12);
        }
    };
}

// ============================================================================
// DD/FD PREFIX TABLES (IX/IY Index Registers)
// ============================================================================
void Z80::init_dd_table() {
    // Default: treat as NOP (but log if still hitting unknowns)
    for (int i = 0; i < 256; i++) {
        dd_table[i] = [this, i]() {
            static int warn_count = 0;
            if (warn_count < 50) {
                fprintf(stderr, "UNIMPL DD 0x%02X at PC=0x%04X\n", i, (unsigned)(reg.pc - 1));
                warn_count++;
            }
            add_ticks(4);
        };
    }

    // LD IX, nn
    dd_table[0x21] = [this]() { reg.ix = fetch16(); add_ticks(10); };

    // LD (nn), IX / LD IX, (nn)
    dd_table[0x22] = [this]() { uint16_t a = fetch16(); write_mem(a, reg.ix & 0xFF); write_mem(a+1, reg.ix >> 8); add_ticks(16); };
    dd_table[0x2A] = [this]() { uint16_t a = fetch16(); reg.ix = read_mem(a) | (read_mem(a+1) << 8); add_ticks(16); };

    // INC/DEC IX
    dd_table[0x23] = [this]() { reg.ix++; add_ticks(6); };
    dd_table[0x2B] = [this]() { reg.ix--; add_ticks(6); };

    // ADD IX, rr
    dd_table[0x09] = [this]() { op_add16(reg.ix, reg.bc); add_ticks(11); };
    dd_table[0x19] = [this]() { op_add16(reg.ix, reg.de); add_ticks(11); };
    dd_table[0x29] = [this]() { op_add16(reg.ix, reg.ix); add_ticks(11); };
    dd_table[0x39] = [this]() { op_add16(reg.ix, reg.sp); add_ticks(11); };

    // INC/DEC (IX+d)
    dd_table[0x34] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); uint16_t a = reg.ix+d; uint8_t v = read_mem(a); op_inc(v); write_mem(a, v); add_ticks(19); };
    dd_table[0x35] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); uint16_t a = reg.ix+d; uint8_t v = read_mem(a); op_dec(v); write_mem(a, v); add_ticks(19); };

    // LD (IX+d), n
    dd_table[0x36] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); uint8_t v = fetch(false); write_mem(reg.ix+d, v); add_ticks(15); };

    // LD r, (IX+d)
    dd_table[0x46] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.b = read_mem(reg.ix+d); add_ticks(15); };
    dd_table[0x4E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.c = read_mem(reg.ix+d); add_ticks(15); };
    dd_table[0x56] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.d = read_mem(reg.ix+d); add_ticks(15); };
    dd_table[0x5E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.e = read_mem(reg.ix+d); add_ticks(15); };
    dd_table[0x66] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.h = read_mem(reg.ix+d); add_ticks(15); };
    dd_table[0x6E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.l = read_mem(reg.ix+d); add_ticks(15); };
    dd_table[0x7E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.a = read_mem(reg.ix+d); add_ticks(15); };

    // LD (IX+d), r  ← THE MISSING ONES
    dd_table[0x70] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.ix+d, reg.b); add_ticks(15); };
    dd_table[0x71] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.ix+d, reg.c); add_ticks(15); };
    dd_table[0x72] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.ix+d, reg.d); add_ticks(15); };
    dd_table[0x73] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.ix+d, reg.e); add_ticks(15); };
    dd_table[0x74] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.ix+d, reg.h); add_ticks(15); };
    dd_table[0x75] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.ix+d, reg.l); add_ticks(15); };
    dd_table[0x77] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.ix+d, reg.a); add_ticks(15); };

    // Arithmetic with (IX+d)
    dd_table[0x86] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_add(read_mem(reg.ix+d)); add_ticks(15); };
    dd_table[0x8E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_adc(read_mem(reg.ix+d)); add_ticks(15); };
    dd_table[0x96] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_sub(read_mem(reg.ix+d)); add_ticks(15); };
    dd_table[0x9E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_sbc(read_mem(reg.ix+d)); add_ticks(15); };
    dd_table[0xA6] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_and(read_mem(reg.ix+d)); add_ticks(15); };
    dd_table[0xAE] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_xor(read_mem(reg.ix+d)); add_ticks(15); };
    dd_table[0xB6] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_or(read_mem(reg.ix+d)); add_ticks(15); };
    dd_table[0xBE] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_cp(read_mem(reg.ix+d)); add_ticks(15); };

    // PUSH/POP IX
    dd_table[0xE5] = [this]() { push(reg.ix); add_ticks(11); };
    dd_table[0xE1] = [this]() { reg.ix = pop(); add_ticks(10); };

    // EX (SP), IX
    dd_table[0xE3] = [this]() { uint16_t v = reg.ix; reg.ix = read_mem(reg.sp) | (read_mem(reg.sp+1) << 8); write_mem(reg.sp, v & 0xFF); write_mem(reg.sp+1, v >> 8); add_ticks(19); };

    // JP (IX)
    dd_table[0xE9] = [this]() { reg.pc = reg.ix; add_ticks(4); };

    // LD SP, IX
    dd_table[0xF9] = [this]() { reg.sp = reg.ix; add_ticks(6); };

    // ---- Undocumented IXH/IXL operations ----
    // INC/DEC IXH/IXL
    dd_table[0x24] = [this]() { op_inc(reg.ixh); add_ticks(4); };
    dd_table[0x25] = [this]() { op_dec(reg.ixh); add_ticks(4); };
    dd_table[0x2C] = [this]() { op_inc(reg.ixl); add_ticks(4); };
    dd_table[0x2D] = [this]() { op_dec(reg.ixl); add_ticks(4); };

    // LD IXH/IXL, n
    dd_table[0x26] = [this]() { reg.ixh = fetch(false); add_ticks(7); };
    dd_table[0x2E] = [this]() { reg.ixl = fetch(false); add_ticks(7); };

    // LD r, IXH/IXL
    dd_table[0x44] = [this]() { reg.b = reg.ixh; add_ticks(4); };
    dd_table[0x45] = [this]() { reg.b = reg.ixl; add_ticks(4); };
    dd_table[0x4C] = [this]() { reg.c = reg.ixh; add_ticks(4); };
    dd_table[0x4D] = [this]() { reg.c = reg.ixl; add_ticks(4); };
    dd_table[0x54] = [this]() { reg.d = reg.ixh; add_ticks(4); };
    dd_table[0x55] = [this]() { reg.d = reg.ixl; add_ticks(4); };
    dd_table[0x5C] = [this]() { reg.e = reg.ixh; add_ticks(4); };
    dd_table[0x5D] = [this]() { reg.e = reg.ixl; add_ticks(4); };
    dd_table[0x60] = [this]() { reg.ixh = reg.b; add_ticks(4); };
    dd_table[0x61] = [this]() { reg.ixh = reg.c; add_ticks(4); };
    dd_table[0x62] = [this]() { reg.ixh = reg.d; add_ticks(4); };
    dd_table[0x63] = [this]() { reg.ixh = reg.e; add_ticks(4); };
    dd_table[0x64] = [this]() { add_ticks(4); };  // LD IXH,IXH (nop)
    dd_table[0x65] = [this]() { reg.ixh = reg.ixl; add_ticks(4); };
    dd_table[0x67] = [this]() { reg.ixh = reg.a; add_ticks(4); };
    dd_table[0x68] = [this]() { reg.ixl = reg.b; add_ticks(4); };
    dd_table[0x69] = [this]() { reg.ixl = reg.c; add_ticks(4); };
    dd_table[0x6A] = [this]() { reg.ixl = reg.d; add_ticks(4); };
    dd_table[0x6B] = [this]() { reg.ixl = reg.e; add_ticks(4); };
    dd_table[0x6C] = [this]() { reg.ixl = reg.ixh; add_ticks(4); };
    dd_table[0x6D] = [this]() { add_ticks(4); };  // LD IXL,IXL (nop)
    dd_table[0x6F] = [this]() { reg.ixl = reg.a; add_ticks(4); };
    dd_table[0x7C] = [this]() { reg.a = reg.ixh; add_ticks(4); };
    dd_table[0x7D] = [this]() { reg.a = reg.ixl; add_ticks(4); };

    // LD r,r (non-IX variants that still need to work under DD prefix)
    dd_table[0x40] = [this]() { reg.b = reg.b; add_ticks(4); };
    dd_table[0x41] = [this]() { reg.b = reg.c; add_ticks(4); };
    dd_table[0x42] = [this]() { reg.b = reg.d; add_ticks(4); };
    dd_table[0x43] = [this]() { reg.b = reg.e; add_ticks(4); };
    dd_table[0x47] = [this]() { reg.b = reg.a; add_ticks(4); };
    dd_table[0x48] = [this]() { reg.c = reg.b; add_ticks(4); };
    dd_table[0x49] = [this]() { reg.c = reg.c; add_ticks(4); };
    dd_table[0x4A] = [this]() { reg.c = reg.d; add_ticks(4); };
    dd_table[0x4B] = [this]() { reg.c = reg.e; add_ticks(4); };
    dd_table[0x4F] = [this]() { reg.c = reg.a; add_ticks(4); };
    dd_table[0x50] = [this]() { reg.d = reg.b; add_ticks(4); };
    dd_table[0x51] = [this]() { reg.d = reg.c; add_ticks(4); };
    dd_table[0x52] = [this]() { reg.d = reg.d; add_ticks(4); };
    dd_table[0x53] = [this]() { reg.d = reg.e; add_ticks(4); };
    dd_table[0x57] = [this]() { reg.d = reg.a; add_ticks(4); };
    dd_table[0x58] = [this]() { reg.e = reg.b; add_ticks(4); };
    dd_table[0x59] = [this]() { reg.e = reg.c; add_ticks(4); };
    dd_table[0x5A] = [this]() { reg.e = reg.d; add_ticks(4); };
    dd_table[0x5B] = [this]() { reg.e = reg.e; add_ticks(4); };
    dd_table[0x5F] = [this]() { reg.e = reg.a; add_ticks(4); };
    dd_table[0x78] = [this]() { reg.a = reg.b; add_ticks(4); };
    dd_table[0x79] = [this]() { reg.a = reg.c; add_ticks(4); };
    dd_table[0x7A] = [this]() { reg.a = reg.d; add_ticks(4); };
    dd_table[0x7B] = [this]() { reg.a = reg.e; add_ticks(4); };
    dd_table[0x7F] = [this]() { reg.a = reg.a; add_ticks(4); };

    // ALU with IXH/IXL
    dd_table[0x84] = [this]() { op_add(reg.ixh); add_ticks(4); };
    dd_table[0x85] = [this]() { op_add(reg.ixl); add_ticks(4); };
    dd_table[0x8C] = [this]() { op_adc(reg.ixh); add_ticks(4); };
    dd_table[0x8D] = [this]() { op_adc(reg.ixl); add_ticks(4); };
    dd_table[0x94] = [this]() { op_sub(reg.ixh); add_ticks(4); };
    dd_table[0x95] = [this]() { op_sub(reg.ixl); add_ticks(4); };
    dd_table[0x9C] = [this]() { op_sbc(reg.ixh); add_ticks(4); };
    dd_table[0x9D] = [this]() { op_sbc(reg.ixl); add_ticks(4); };
    dd_table[0xA4] = [this]() { op_and(reg.ixh); add_ticks(4); };
    dd_table[0xA5] = [this]() { op_and(reg.ixl); add_ticks(4); };
    dd_table[0xAC] = [this]() { op_xor(reg.ixh); add_ticks(4); };
    dd_table[0xAD] = [this]() { op_xor(reg.ixl); add_ticks(4); };
    dd_table[0xB4] = [this]() { op_or(reg.ixh); add_ticks(4); };
    dd_table[0xB5] = [this]() { op_or(reg.ixl); add_ticks(4); };
    dd_table[0xBC] = [this]() { op_cp(reg.ixh); add_ticks(4); };
    dd_table[0xBD] = [this]() { op_cp(reg.ixl); add_ticks(4); };

    // ---- DD CB prefix (bit ops on IX+d) ----
    dd_table[0xCB] = [this]() {
        int8_t d = static_cast<int8_t>(fetch(false));
        uint8_t op = fetch(false);
        uint16_t addr = reg.ix + d;
        uint8_t val = read_mem(addr);
        
        if (op < 0x40) {
            // Shift/rotate operations
            switch ((op >> 3) & 7) {
                case 0: op_rlc(val); break;
                case 1: op_rrc(val); break;
                case 2: op_rl(val); break;
                case 3: op_rr(val); break;
                case 4: op_sl(val); break;
                case 5: op_sr(val); break;
                case 6: { // SLL (undocumented) - shift left, bit 0 = 1
                    bool c = val & 0x80;
                    val = (val << 1) | 1;
                    set_cf(c); set_sf(val); set_zf(val); set_hf(false); set_pf(val); set_nf(false); set_f35(val);
                    break;
                }
                case 7: { // SRL
                    bool c = val & 1;
                    val >>= 1;
                    set_cf(c); set_sf(val); set_zf(val); set_hf(false); set_pf(val); set_nf(false); set_f35(val);
                    break;
                }
            }
            write_mem(addr, val);
            // Store result in register too (undocumented)
            uint8_t reg_code = op & 7;
            if (reg_code != 6) {
                switch (reg_code) {
                    case 0: reg.b = val; break;
                    case 1: reg.c = val; break;
                    case 2: reg.d = val; break;
                    case 3: reg.e = val; break;
                    case 4: reg.h = val; break;
                    case 5: reg.l = val; break;
                    case 7: reg.a = val; break;
                }
            }
            add_ticks(19);
        } else if (op < 0x80) {
            // BIT b, (IX+d)
            uint8_t bit = (op >> 3) & 7;
            op_bit(bit, val);
            // For BIT on (IX+d), F3/F5 come from high byte of address
            set_f35((addr >> 8) & 0xFF);
            add_ticks(16);
        } else if (op < 0xC0) {
            // RES b, (IX+d)
            uint8_t bit = (op >> 3) & 7;
            val &= ~(1 << bit);
            write_mem(addr, val);
            uint8_t reg_code = op & 7;
            if (reg_code != 6) {
                switch (reg_code) {
                    case 0: reg.b = val; break;
                    case 1: reg.c = val; break;
                    case 2: reg.d = val; break;
                    case 3: reg.e = val; break;
                    case 4: reg.h = val; break;
                    case 5: reg.l = val; break;
                    case 7: reg.a = val; break;
                }
            }
            add_ticks(19);
        } else {
            // SET b, (IX+d)
            uint8_t bit = (op >> 3) & 7;
            val |= (1 << bit);
            write_mem(addr, val);
            uint8_t reg_code = op & 7;
            if (reg_code != 6) {
                switch (reg_code) {
                    case 0: reg.b = val; break;
                    case 1: reg.c = val; break;
                    case 2: reg.d = val; break;
                    case 3: reg.e = val; break;
                    case 4: reg.h = val; break;
                    case 5: reg.l = val; break;
                    case 7: reg.a = val; break;
                }
            }
            add_ticks(19);
        }
    };
}

void Z80::init_fd_table() {
    // Default: log unimplemented
    for (int i = 0; i < 256; i++) {
        fd_table[i] = [this, i]() {
            static int warn_count = 0;
            if (warn_count < 50) {
                fprintf(stderr, "UNIMPL FD 0x%02X at PC=0x%04X\n", i, (unsigned)(reg.pc - 1));
                warn_count++;
            }
            add_ticks(4);
        };
    }

    // LD IY, nn
    fd_table[0x21] = [this]() { reg.iy = fetch16(); add_ticks(10); };

    // LD (nn), IY / LD IY, (nn)
    fd_table[0x22] = [this]() { uint16_t a = fetch16(); write_mem(a, reg.iy & 0xFF); write_mem(a+1, reg.iy >> 8); add_ticks(16); };
    fd_table[0x2A] = [this]() { uint16_t a = fetch16(); reg.iy = read_mem(a) | (read_mem(a+1) << 8); add_ticks(16); };

    // INC/DEC IY
    fd_table[0x23] = [this]() { reg.iy++; add_ticks(6); };
    fd_table[0x2B] = [this]() { reg.iy--; add_ticks(6); };

    // ADD IY, rr
    fd_table[0x09] = [this]() { op_add16(reg.iy, reg.bc); add_ticks(11); };
    fd_table[0x19] = [this]() { op_add16(reg.iy, reg.de); add_ticks(11); };
    fd_table[0x29] = [this]() { op_add16(reg.iy, reg.iy); add_ticks(11); };
    fd_table[0x39] = [this]() { op_add16(reg.iy, reg.sp); add_ticks(11); };

    // INC/DEC (IY+d)
    fd_table[0x34] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); uint16_t a = reg.iy+d; uint8_t v = read_mem(a); op_inc(v); write_mem(a, v); add_ticks(19); };
    fd_table[0x35] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); uint16_t a = reg.iy+d; uint8_t v = read_mem(a); op_dec(v); write_mem(a, v); add_ticks(19); };

    // LD (IY+d), n
    fd_table[0x36] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); uint8_t v = fetch(false); write_mem(reg.iy+d, v); add_ticks(15); };

    // LD r, (IY+d)
    fd_table[0x46] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.b = read_mem(reg.iy+d); add_ticks(15); };
    fd_table[0x4E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.c = read_mem(reg.iy+d); add_ticks(15); };
    fd_table[0x56] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.d = read_mem(reg.iy+d); add_ticks(15); };
    fd_table[0x5E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.e = read_mem(reg.iy+d); add_ticks(15); };
    fd_table[0x66] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.h = read_mem(reg.iy+d); add_ticks(15); };
    fd_table[0x6E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.l = read_mem(reg.iy+d); add_ticks(15); };
    fd_table[0x7E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); reg.a = read_mem(reg.iy+d); add_ticks(15); };

    // LD (IY+d), r
    fd_table[0x70] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.iy+d, reg.b); add_ticks(15); };
    fd_table[0x71] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.iy+d, reg.c); add_ticks(15); };
    fd_table[0x72] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.iy+d, reg.d); add_ticks(15); };
    fd_table[0x73] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.iy+d, reg.e); add_ticks(15); };
    fd_table[0x74] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.iy+d, reg.h); add_ticks(15); };
    fd_table[0x75] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.iy+d, reg.l); add_ticks(15); };
    fd_table[0x77] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); write_mem(reg.iy+d, reg.a); add_ticks(15); };

    // Arithmetic with (IY+d)
    fd_table[0x86] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_add(read_mem(reg.iy+d)); add_ticks(15); };
    fd_table[0x8E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_adc(read_mem(reg.iy+d)); add_ticks(15); };
    fd_table[0x96] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_sub(read_mem(reg.iy+d)); add_ticks(15); };
    fd_table[0x9E] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_sbc(read_mem(reg.iy+d)); add_ticks(15); };
    fd_table[0xA6] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_and(read_mem(reg.iy+d)); add_ticks(15); };
    fd_table[0xAE] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_xor(read_mem(reg.iy+d)); add_ticks(15); };
    fd_table[0xB6] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_or(read_mem(reg.iy+d)); add_ticks(15); };
    fd_table[0xBE] = [this]() { int8_t d = static_cast<int8_t>(fetch(false)); op_cp(read_mem(reg.iy+d)); add_ticks(15); };

    // PUSH/POP IY
    fd_table[0xE5] = [this]() { push(reg.iy); add_ticks(11); };
    fd_table[0xE1] = [this]() { reg.iy = pop(); add_ticks(10); };

    // EX (SP), IY
    fd_table[0xE3] = [this]() { uint16_t v = reg.iy; reg.iy = read_mem(reg.sp) | (read_mem(reg.sp+1) << 8); write_mem(reg.sp, v & 0xFF); write_mem(reg.sp+1, v >> 8); add_ticks(19); };

    // JP (IY)
    fd_table[0xE9] = [this]() { reg.pc = reg.iy; add_ticks(4); };

    // LD SP, IY
    fd_table[0xF9] = [this]() { reg.sp = reg.iy; add_ticks(6); };

    // ---- Undocumented IYH/IYL operations ----
    // INC/DEC IYH/IYL
    fd_table[0x24] = [this]() { op_inc(reg.iyh); add_ticks(4); };
    fd_table[0x25] = [this]() { op_dec(reg.iyh); add_ticks(4); };
    fd_table[0x2C] = [this]() { op_inc(reg.iyl); add_ticks(4); };
    fd_table[0x2D] = [this]() { op_dec(reg.iyl); add_ticks(4); };

    // LD IYH/IYL, n
    fd_table[0x26] = [this]() { reg.iyh = fetch(false); add_ticks(7); };
    fd_table[0x2E] = [this]() { reg.iyl = fetch(false); add_ticks(7); };

    // LD r, IYH/IYL
    fd_table[0x44] = [this]() { reg.b = reg.iyh; add_ticks(4); };
    fd_table[0x45] = [this]() { reg.b = reg.iyl; add_ticks(4); };
    fd_table[0x4C] = [this]() { reg.c = reg.iyh; add_ticks(4); };
    fd_table[0x4D] = [this]() { reg.c = reg.iyl; add_ticks(4); };
    fd_table[0x54] = [this]() { reg.d = reg.iyh; add_ticks(4); };
    fd_table[0x55] = [this]() { reg.d = reg.iyl; add_ticks(4); };
    fd_table[0x5C] = [this]() { reg.e = reg.iyh; add_ticks(4); };
    fd_table[0x5D] = [this]() { reg.e = reg.iyl; add_ticks(4); };
    fd_table[0x60] = [this]() { reg.iyh = reg.b; add_ticks(4); };
    fd_table[0x61] = [this]() { reg.iyh = reg.c; add_ticks(4); };
    fd_table[0x62] = [this]() { reg.iyh = reg.d; add_ticks(4); };
    fd_table[0x63] = [this]() { reg.iyh = reg.e; add_ticks(4); };
    fd_table[0x64] = [this]() { add_ticks(4); };  // LD IYH,IYH (nop)
    fd_table[0x65] = [this]() { reg.iyh = reg.iyl; add_ticks(4); };
    fd_table[0x67] = [this]() { reg.iyh = reg.a; add_ticks(4); };
    fd_table[0x68] = [this]() { reg.iyl = reg.b; add_ticks(4); };
    fd_table[0x69] = [this]() { reg.iyl = reg.c; add_ticks(4); };
    fd_table[0x6A] = [this]() { reg.iyl = reg.d; add_ticks(4); };
    fd_table[0x6B] = [this]() { reg.iyl = reg.e; add_ticks(4); };
    fd_table[0x6C] = [this]() { reg.iyl = reg.iyh; add_ticks(4); };
    fd_table[0x6D] = [this]() { add_ticks(4); };  // LD IYL,IYL (nop)
    fd_table[0x6F] = [this]() { reg.iyl = reg.a; add_ticks(4); };
    fd_table[0x7C] = [this]() { reg.a = reg.iyh; add_ticks(4); };
    fd_table[0x7D] = [this]() { reg.a = reg.iyl; add_ticks(4); };

    // LD r,r (non-IY variants that still need to work under FD prefix)
    fd_table[0x40] = [this]() { reg.b = reg.b; add_ticks(4); };
    fd_table[0x41] = [this]() { reg.b = reg.c; add_ticks(4); };
    fd_table[0x42] = [this]() { reg.b = reg.d; add_ticks(4); };
    fd_table[0x43] = [this]() { reg.b = reg.e; add_ticks(4); };
    fd_table[0x47] = [this]() { reg.b = reg.a; add_ticks(4); };
    fd_table[0x48] = [this]() { reg.c = reg.b; add_ticks(4); };
    fd_table[0x49] = [this]() { reg.c = reg.c; add_ticks(4); };
    fd_table[0x4A] = [this]() { reg.c = reg.d; add_ticks(4); };
    fd_table[0x4B] = [this]() { reg.c = reg.e; add_ticks(4); };
    fd_table[0x4F] = [this]() { reg.c = reg.a; add_ticks(4); };
    fd_table[0x50] = [this]() { reg.d = reg.b; add_ticks(4); };
    fd_table[0x51] = [this]() { reg.d = reg.c; add_ticks(4); };
    fd_table[0x52] = [this]() { reg.d = reg.d; add_ticks(4); };
    fd_table[0x53] = [this]() { reg.d = reg.e; add_ticks(4); };
    fd_table[0x57] = [this]() { reg.d = reg.a; add_ticks(4); };
    fd_table[0x58] = [this]() { reg.e = reg.b; add_ticks(4); };
    fd_table[0x59] = [this]() { reg.e = reg.c; add_ticks(4); };
    fd_table[0x5A] = [this]() { reg.e = reg.d; add_ticks(4); };
    fd_table[0x5B] = [this]() { reg.e = reg.e; add_ticks(4); };
    fd_table[0x5F] = [this]() { reg.e = reg.a; add_ticks(4); };
    fd_table[0x78] = [this]() { reg.a = reg.b; add_ticks(4); };
    fd_table[0x79] = [this]() { reg.a = reg.c; add_ticks(4); };
    fd_table[0x7A] = [this]() { reg.a = reg.d; add_ticks(4); };
    fd_table[0x7B] = [this]() { reg.a = reg.e; add_ticks(4); };
    fd_table[0x7F] = [this]() { reg.a = reg.a; add_ticks(4); };

    // ALU with IYH/IYL
    fd_table[0x84] = [this]() { op_add(reg.iyh); add_ticks(4); };
    fd_table[0x85] = [this]() { op_add(reg.iyl); add_ticks(4); };
    fd_table[0x8C] = [this]() { op_adc(reg.iyh); add_ticks(4); };
    fd_table[0x8D] = [this]() { op_adc(reg.iyl); add_ticks(4); };
    fd_table[0x94] = [this]() { op_sub(reg.iyh); add_ticks(4); };
    fd_table[0x95] = [this]() { op_sub(reg.iyl); add_ticks(4); };
    fd_table[0x9C] = [this]() { op_sbc(reg.iyh); add_ticks(4); };
    fd_table[0x9D] = [this]() { op_sbc(reg.iyl); add_ticks(4); };
    fd_table[0xA4] = [this]() { op_and(reg.iyh); add_ticks(4); };
    fd_table[0xA5] = [this]() { op_and(reg.iyl); add_ticks(4); };
    fd_table[0xAC] = [this]() { op_xor(reg.iyh); add_ticks(4); };
    fd_table[0xAD] = [this]() { op_xor(reg.iyl); add_ticks(4); };
    fd_table[0xB4] = [this]() { op_or(reg.iyh); add_ticks(4); };
    fd_table[0xB5] = [this]() { op_or(reg.iyl); add_ticks(4); };
    fd_table[0xBC] = [this]() { op_cp(reg.iyh); add_ticks(4); };
    fd_table[0xBD] = [this]() { op_cp(reg.iyl); add_ticks(4); };

    // ---- FD CB prefix (bit ops on IY+d) ----
    fd_table[0xCB] = [this]() {
        int8_t d = static_cast<int8_t>(fetch(false));
        uint8_t op = fetch(false);
        uint16_t addr = reg.iy + d;
        uint8_t val = read_mem(addr);
        
        if (op < 0x40) {
            // Shift/rotate operations
            switch ((op >> 3) & 7) {
                case 0: op_rlc(val); break;
                case 1: op_rrc(val); break;
                case 2: op_rl(val); break;
                case 3: op_rr(val); break;
                case 4: op_sl(val); break;
                case 5: op_sr(val); break;
                case 6: { // SLL (undocumented)
                    bool c = val & 0x80;
                    val = (val << 1) | 1;
                    set_cf(c); set_sf(val); set_zf(val); set_hf(false); set_pf(val); set_nf(false); set_f35(val);
                    break;
                }
                case 7: { // SRL
                    bool c = val & 1;
                    val >>= 1;
                    set_cf(c); set_sf(val); set_zf(val); set_hf(false); set_pf(val); set_nf(false); set_f35(val);
                    break;
                }
            }
            write_mem(addr, val);
            uint8_t reg_code = op & 7;
            if (reg_code != 6) {
                switch (reg_code) {
                    case 0: reg.b = val; break;
                    case 1: reg.c = val; break;
                    case 2: reg.d = val; break;
                    case 3: reg.e = val; break;
                    case 4: reg.h = val; break;
                    case 5: reg.l = val; break;
                    case 7: reg.a = val; break;
                }
            }
            add_ticks(19);
        } else if (op < 0x80) {
            // BIT b, (IY+d)
            uint8_t bit = (op >> 3) & 7;
            op_bit(bit, val);
            set_f35((addr >> 8) & 0xFF);
            add_ticks(16);
        } else if (op < 0xC0) {
            // RES b, (IY+d)
            uint8_t bit = (op >> 3) & 7;
            val &= ~(1 << bit);
            write_mem(addr, val);
            uint8_t reg_code = op & 7;
            if (reg_code != 6) {
                switch (reg_code) {
                    case 0: reg.b = val; break;
                    case 1: reg.c = val; break;
                    case 2: reg.d = val; break;
                    case 3: reg.e = val; break;
                    case 4: reg.h = val; break;
                    case 5: reg.l = val; break;
                    case 7: reg.a = val; break;
                }
            }
            add_ticks(19);
        } else {
            // SET b, (IY+d)
            uint8_t bit = (op >> 3) & 7;
            val |= (1 << bit);
            write_mem(addr, val);
            uint8_t reg_code = op & 7;
            if (reg_code != 6) {
                switch (reg_code) {
                    case 0: reg.b = val; break;
                    case 1: reg.c = val; break;
                    case 2: reg.d = val; break;
                    case 3: reg.e = val; break;
                    case 4: reg.h = val; break;
                    case 5: reg.l = val; break;
                    case 7: reg.a = val; break;
                }
            }
            add_ticks(19);
        }
    };
}
// src/cpu/Z80.hpp
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>

// Flag Constants
constexpr uint8_t FLAG_C  = 0x01;  // Carry
constexpr uint8_t FLAG_N  = 0x02;  // Subtract
constexpr uint8_t FLAG_P  = 0x04;  // Parity/Overflow
constexpr uint8_t FLAG_F3 = 0x08;  // Undocumented (bit 3, Y flag)
constexpr uint8_t FLAG_H  = 0x10;  // Half Carry
constexpr uint8_t FLAG_F5 = 0x20;  // Undocumented (bit 5, X flag)
constexpr uint8_t FLAG_Z  = 0x40;  // Zero
constexpr uint8_t FLAG_S  = 0x80;  // Sign

// Forward declaration
class Bus;

class Z80 {
public:
    Z80(Bus& bus);
    int step();
    void reset();

    // Debug access
    uint16_t get_pc() const { return reg.pc; }
    uint16_t get_sp() const { return reg.sp; }
    uint8_t get_a() const { return reg.a; }
    uint8_t get_f() const { return reg.f; }
    uint8_t get_b() const { return reg.b; }
    uint8_t get_c() const { return reg.c; }
    uint8_t get_d() const { return reg.d; }
    uint8_t get_e() const { return reg.e; }
    uint8_t get_h() const { return reg.h; }
    uint8_t get_l() const { return reg.l; }
    uint16_t get_bc() const { return reg.bc; }
    uint16_t get_de() const { return reg.de; }
    uint16_t get_hl() const { return reg.hl; }

    // Debug mutation (for test harnesses)
    void set_pc(uint16_t val) { reg.pc = val; }
    void set_sp(uint16_t val) { reg.sp = val; }

private:
    // ------------------------------------------------------------------------
    // REGISTER STRUCT WITH UNIONS (Little-Endian Safe for M4)
    // ------------------------------------------------------------------------
    struct Registers {
        // AF Pair
        uint8_t a = 0, f = 0;
        
        // BC Pair (c = low byte, b = high byte)
        union {
            struct { uint8_t c, b; };
            uint16_t bc = 0;
        };

        // DE Pair (e = low byte, d = high byte)
        union {
            struct { uint8_t e, d; };
            uint16_t de = 0;
        };

        // HL Pair (l = low byte, h = high byte)
        union {
            struct { uint8_t l, h; };
            uint16_t hl = 0;
        };

        // Stack Pointer & Program Counter
        uint16_t sp = 0, pc = 0;

        // Alternate Register Set
        uint8_t a2 = 0, f2 = 0;
        uint16_t bc2 = 0, de2 = 0, hl2 = 0;

        // Index Registers
        union {
            struct { uint8_t ixl, ixh; };
            uint16_t ix = 0;
        };
        union {
            struct { uint8_t iyl, iyh; };
            uint16_t iy = 0;
        };

        // Interrupt Vector & Refresh Registers
        uint8_t i = 0, r = 0;

        // Interrupt State
        bool iff1 = false, iff2 = false;
        uint8_t im = 0;
        bool halted = false;
    } reg;

    Bus& bus;
    int t_states = 0;
    uint8_t prefix = 0x00;
    bool is_m1_cycle = true;

    // Opcode Tables
    using OpcodeFunc = std::function<void()>;
    std::array<OpcodeFunc, 256> main_table;
    std::array<OpcodeFunc, 256> cb_table;
    std::array<OpcodeFunc, 256> ed_table;
    std::array<OpcodeFunc, 256> dd_table;
    std::array<OpcodeFunc, 256> fd_table;

    // ------------------------------------------------------------------------
    // Internal Helpers
    // ------------------------------------------------------------------------
    void set_flag(uint8_t flag, bool value);
    bool get_flag(uint8_t flag) const;
    void set_zf(uint8_t val);
    void set_sf(uint8_t val);
    void set_hf(bool val);
    void set_pf(uint8_t val);
    void set_nf(bool val);
    void set_cf(bool val);
    void set_f35(uint8_t val);
    static bool parity(uint8_t val);
    void set_flags_8bit(uint8_t result, uint8_t a, uint8_t b, bool subtract, bool half_carry, bool carry = false);
    
    uint8_t read_mem(uint16_t addr, bool is_m1 = false);
    void write_mem(uint16_t addr, uint8_t val);
    uint8_t fetch(bool is_m1 = true);
    uint16_t fetch16();
    void add_ticks(int t);
    
    uint8_t& get_reg_8(uint8_t code);
    uint16_t& get_reg_16(uint8_t code);
    void push(uint16_t val);
    uint16_t pop();
    
    // Operations
    void op_add(uint8_t val);
    void op_adc(uint8_t val);
    void op_sub(uint8_t val);
    void op_sbc(uint8_t val);
    void op_and(uint8_t val);
    void op_xor(uint8_t val);
    void op_or(uint8_t val);
    void op_cp(uint8_t val);
    void op_inc(uint8_t& r);
    void op_dec(uint8_t& r);
    void op_inc16(uint16_t& r);  // Now works with unions
    void op_dec16(uint16_t& r);
    void op_add16(uint16_t& r, uint16_t val);
    void op_daa();
    void op_bit(uint8_t bit, uint8_t val);
    void op_set(uint8_t bit, uint8_t& val);
    void op_res(uint8_t bit, uint8_t& val);
    void op_sl(uint8_t& val);
    void op_sr(uint8_t& val);
    void op_rl(uint8_t& val);
    void op_rr(uint8_t& val);
    void op_rla();
    void op_rra();
    void op_rlc(uint8_t& val);
    void op_rrc(uint8_t& val);
    void op_call();
    void op_ret();
    void op_reti();
    void op_jp();
    void op_jr();
    void op_rst(uint8_t addr);
    void op_halt();
    void op_di();
    void op_ei();
    void op_nop();
    void op_ex_af();
    void op_ex_de_hl();
    void op_exx();
    void op_ld_i_a();
    void op_ld_r_a();
    void op_ld_a_i();
    void op_ld_a_r();
    void op_ld_sp_hl();
    void op_ld_hl_sp();

    // Initialization
    void init_main_table();
    void init_cb_table();
    void init_ed_table();
    void init_dd_table();
    void init_fd_table();
};
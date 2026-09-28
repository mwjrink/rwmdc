#pragma once

#include <lib/grim/bp.h>

// ====================================================================================================================
// =                                                         u64                                                      =
// ====================================================================================================================

force_inline(u64) bitarray64_pop_lowest_bit(rop(rw u64) bits) {
    u32 index = (u32)__builtin_ctzll(*bits);
    *bits &= *bits - 1;
    return index;
}

force_inline(u64) bitarray64_calc_size(u64 size) {
    return (size >> 6) + ((size & 63) != 0);
}

force_inline(void) bitarray64_set(rop(rw u64) bits, u64 idx) {
    bits[idx >> 6] |= 1ULL << (idx & 63);
}

force_inline(void) bitarray64_unset(rop(rw u64) bits, u64 idx) {
    bits[idx >> 6] &= ~(1ULL << (idx & 63));
}

force_inline(u64) bitarray64_test(rop(ro u64) bits, u64 idx) {
    return (bits[idx >> 6] >> (idx & 63)) & 1ULL;
}

// ====================================================================================================================
// =                                                         u32                                                      =
// ====================================================================================================================

force_inline(u32) bitarray32_calc_size(u32 size) {
    return (size >> 5) + ((size & 31) != 0);
}

force_inline(void) bitarray32_set(rop(rw u32) bits, u32 idx) {
    bits[idx >> 5] |= 1u << (idx & 31);
}

force_inline(void) bitarray32_unset(rop(rw u32) bits, u32 idx) {
    bits[idx >> 5] &= ~(1u << (idx & 31));
}

force_inline(u32) bitarray32_test(rop(ro u32) bits, u32 idx) {
    return (bits[idx >> 5] >> (idx & 31)) & 1u;
}

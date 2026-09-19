#pragma once

#include <lib/grim/bp.h>

force_inline(u32) bitarray_calc_size(u32 size) {
    return (size + 31) >> 5;
}

force_inline(void) bitarray_set(rop(rw u32) bits, u32 idx) {
    bits[idx >> 5] |= 1u << (idx & 31);
}

force_inline(void) bitarray_unset(rop(rw u32) bits, u32 idx) {
    bits[idx >> 5] &= ~(1u << (idx & 31));
}

force_inline(u32) bitarray_test(rop(ro u32) bits, u32 idx) {
    return (bits[idx >> 5] >> (idx & 31)) & 1u;
}

#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <tmmintrin.h>

// #include <builtins>

#define PREFETCH(p) __builtin_prefetch(p)

void test(void* x) {
    PREFETCH(x);
}

// ====================================================================================================================
// =                                                    SSE2/SSSE3                                                    =
// ====================================================================================================================

typedef __m128i simdv_i8;

typedef struct SimdByteScanMany {
    simdv_i8 low[2];
    simdv_i8 high[2];
} SimdByteScanMany;

simdv_i8 splat(u8 byte) {
    return _mm_set1_epi8((char)byte);
}

SimdByteScanMany simd_byte_scan_many_init(rop(ro u8) bytes, u32 count) {
    u8 low[2][16]  = {{0}};
    u8 high[2][16] = {{0}};
    for (u32 i = 0; i < count; ++i) {
        u8  byte  = bytes[i];
        u32 group = byte >> 7;
        u8  bit   = (u8)(1u << ((byte >> 4) & 7));
        low[group][byte & 15] |= bit;
        high[group][(byte >> 4) & 15] = bit;
    }
    SimdByteScanMany table;
    for (u32 i = 0; i < 2; ++i) {
        table.low[i]  = _mm_loadu_si128((const simdv_i8*)low[i]);
        table.high[i] = _mm_loadu_si128((const simdv_i8*)high[i]);
    }
    return table;
}

// count must be a multiple of 64. src must contain count readable bytes;
// masks must hold count / 64 u64s. Bit i of masks[b] matches src[b * 64 + i].
// byte is a pre-broadcast vector (e.g. splat(value)); No tail handling.
void simd_byte_scan(rop(ro u8) src, u32 count, simdv_i8 byte, rop(rw u64) masks) {
    assert(SCOPE_SIMD, (count & 63) == 0);

    for (u32 block = 0; block < count / 64; ++block) {
        u64 hits = 0;
        for (u32 offset = 0; offset < 64; offset += 16) {
            simdv_i8 input = _mm_loadu_si128((const simdv_i8*)(src + block * 64 + offset));
            hits |= (u64)(u32)_mm_movemask_epi8(_mm_cmpeq_epi8(input, byte)) << offset;
        }
        masks[block] = hits;
    }
}

// count must be a multiple of 64. src must contain count readable bytes;
// masks must hold count / 64 u64s. Bit i of masks[b] matches src[b * 64 + i]
// against any byte in the precomputed table. No tail handling.
void simd_byte_scan_many(rop(ro u8) src, u32 count, rop(ro SimdByteScanMany) table, rop(rw u64) masks) {
    assert(SCOPE_SIMD, (count & 63) == 0);

    const simdv_i8 nibbles = _mm_set1_epi8(15);
    const simdv_i8 zero    = _mm_setzero_si128();
    for (u32 block = 0; block < count / 64; ++block) {
        u64 hits = 0;
        for (u32 offset = 0; offset < 64; offset += 16) {
            simdv_i8 input = _mm_loadu_si128((const simdv_i8*)(src + block * 64 + offset));
            simdv_i8 low   = _mm_and_si128(input, nibbles);
            simdv_i8 high  = _mm_and_si128(_mm_srli_epi16(input, 4), nibbles);
            simdv_i8 a = _mm_and_si128(_mm_shuffle_epi8(table->low[0], low), _mm_shuffle_epi8(table->high[0], high));
            simdv_i8 b = _mm_and_si128(_mm_shuffle_epi8(table->low[1], low), _mm_shuffle_epi8(table->high[1], high));
            simdv_i8 found = _mm_or_si128(a, b);
            hits |= (u64)((u32)_mm_movemask_epi8(_mm_cmpeq_epi8(found, zero)) ^ 0xffffu) << offset;
        }
        masks[block] = hits;
    }
}

void simd_byte_reverse(rop(ro u8) src, rop(rw u8) dst, u32 size) {
    u64 n           = size / sizeof(__m128);
    u64 rem         = size % sizeof(__m128);
    u8  mask_vals[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

    simdv_i8  mask = _mm_loadu_si128((simdv_i8*)mask_vals); // Requires SSE2
    simdv_i8  tmp;
    simdv_i8* s = (simdv_i8*)src;
    simdv_i8* d = (simdv_i8*)(dst + rem);

    for (u64 i = 0; i < n; i++) {
        tmp = _mm_loadu_si128(&s[i]);         // Requires SSE2
        tmp = _mm_shuffle_epi8(tmp, mask);    // Requires SSSE3
        _mm_storeu_si128(&d[n - i - 1], tmp); // Requires SSE2
    }

    for (u64 i = 0; i < rem; i++) {
        dst[i] = src[size - i - 1];
    }
}

void simd_byte_reverse_ip(rop(rw u8) buf, u32 size) {
    u64 n           = size / (sizeof(__m128) * 2);
    u64 rem         = size % (sizeof(__m128) * 2);
    u8  mask_vals[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

    simdv_i8  mask = _mm_loadu_si128((simdv_i8*)mask_vals); // Requires SSE2
    simdv_i8  a, b;
    simdv_i8* start = (simdv_i8*)buf;
    simdv_i8* end   = (simdv_i8*)&buf[size];

    for (u64 i = 0; i < n; i++) {
        a = _mm_loadu_si128(start + i);   // Requires SSE2
        b = _mm_loadu_si128(end - i - 1); // Requires SSE2
        a = _mm_shuffle_epi8(a, mask);    // Requires SSSE3
        b = _mm_shuffle_epi8(b, mask);    // Requires SSSE3
        _mm_storeu_si128(start + i, b);   // Requires SSE2
        _mm_storeu_si128(end - i - 1, a); // Requires SSE2
    }

    for (u64 i = 0; i < rem / 2; i++) {
        u8 tmp                                = buf[n * sizeof(__m128) + i];
        buf[n * sizeof(__m128) + i]           = buf[n * sizeof(__m128) + rem - i - 1];
        buf[n * sizeof(__m128) + rem - i - 1] = tmp;
    }
}

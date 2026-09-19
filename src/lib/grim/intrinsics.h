#pragma once

#include <lib/grim/bp.h>
#include <tmmintrin.h>

// #include <builtins>

#define PREFETCH(p) __builtin_prefetch(p)

void test(void* x) {
    PREFETCH(x);
}

// ====================================================================================================================
// =                                                    SSE2/SSSE3                                                    =
// ====================================================================================================================

void simd_byte_reverse(rop(ro u8) src, rop(rw u8) dst, u32 size) {
    u64 n           = size / sizeof(__m128);
    u64 rem         = size % sizeof(__m128);
    u8  mask_vals[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

    __m128i  mask = _mm_loadu_si128((__m128i*)mask_vals); // Requires SSE2
    __m128i  tmp;
    __m128i* s = (__m128i*)src;
    __m128i* d = (__m128i*)(dst + rem);

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

    __m128i  mask = _mm_loadu_si128((__m128i*)mask_vals); // Requires SSE2
    __m128i  a, b;
    __m128i* start = (__m128i*)buf;
    __m128i* end   = (__m128i*)&buf[size];

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

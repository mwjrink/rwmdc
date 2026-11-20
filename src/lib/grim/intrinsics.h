#pragma once

#include <immintrin.h>
#include <lib/grim/bp.h>
#include <lib/grim/logger.h>

// #include <builtins>

#define PREFETCH(p)   __builtin_prefetch(p)
#define SIMD_USE_AVX2 1
#undef SIMD_USE_AVX512

void test(void* x) {
    PREFETCH(x);
}

// ====================================================================================================================
// =                                                    SSE2/SSSE3                                                    =
// ====================================================================================================================

typedef __m128i simdv_i128;
typedef __m256i simdv_i256;

// TODO these:
// typedef union {
//     __m128i vec;
//     i32     elements[4];
// } vi128 i32x4;

typedef struct SimdByteScanMany {
    simdv_i128 low[2];
    simdv_i128 high[2];
} SimdByteScanMany;

typedef struct SimdByteRunCarry {
    simdv_i256 previous;
    u8         started;
} SimdByteRunCarry;

simdv_i128 splat(u8 byte) {
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
        table.low[i]  = _mm_loadu_si128((const simdv_i128*)low[i]);
        table.high[i] = _mm_loadu_si128((const simdv_i128*)high[i]);
    }
    return table;
}

// count must be a multiple of 64. src must contain count readable bytes;
// masks must hold count / 64 u64s. Bit i of masks[b] matches src[b * 64 + i].
// byte is a pre-broadcast vector (e.g. splat(value)); No tail handling.
void simd_byte_scan(rop(ro u8) src, u32 count, simdv_i128 byte, rop(rw u64) masks) {
    assert(SCOPE_SIMD, (count & 63) == 0);

    for (u32 block = 0; block < count / 64; ++block) {
        u64 hits = 0;
        for (u32 offset = 0; offset < 64; offset += 16) {
            simdv_i128 input = _mm_loadu_si128((const simdv_i128*)(src + block * 64 + offset));
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

    // TODO all these _mm_ etc should be simd_* macros that route to neon vs avx etc.
    const simdv_i128 nibbles = _mm_set1_epi8(15);
    const simdv_i128 zero    = _mm_setzero_si128();
    for (u32 block = 0; block < count / 64; ++block) {
        u64 hits = 0;
        for (u32 offset = 0; offset < 64; offset += 16) {
            simdv_i128 input = _mm_loadu_si128((const simdv_i128*)(src + block * 64 + offset));
            simdv_i128 low   = _mm_and_si128(input, nibbles);
            simdv_i128 high  = _mm_and_si128(_mm_srli_epi16(input, 4), nibbles);
            simdv_i128 a = _mm_and_si128(_mm_shuffle_epi8(table->low[0], low), _mm_shuffle_epi8(table->high[0], high));
            simdv_i128 b = _mm_and_si128(_mm_shuffle_epi8(table->low[1], low), _mm_shuffle_epi8(table->high[1], high));
            simdv_i128 found = _mm_or_si128(a, b);
            hits |= (u64)((u32)_mm_movemask_epi8(_mm_cmpeq_epi8(found, zero)) ^ 0xffffu) << offset;
        }
        masks[block] = hits;
    }
}

#if defined(SIMD_USE_AVX2)
static const u64 simd_bit_indices8[256] = {
    UINT64_C(0x8080808080808080), UINT64_C(0x8080808080808000), UINT64_C(0x8080808080808001),
    UINT64_C(0x8080808080800100), UINT64_C(0x8080808080808002), UINT64_C(0x8080808080800200),
    UINT64_C(0x8080808080800201), UINT64_C(0x8080808080020100), UINT64_C(0x8080808080808003),
    UINT64_C(0x8080808080800300), UINT64_C(0x8080808080800301), UINT64_C(0x8080808080030100),
    UINT64_C(0x8080808080800302), UINT64_C(0x8080808080030200), UINT64_C(0x8080808080030201),
    UINT64_C(0x8080808003020100), UINT64_C(0x8080808080808004), UINT64_C(0x8080808080800400),
    UINT64_C(0x8080808080800401), UINT64_C(0x8080808080040100), UINT64_C(0x8080808080800402),
    UINT64_C(0x8080808080040200), UINT64_C(0x8080808080040201), UINT64_C(0x8080808004020100),
    UINT64_C(0x8080808080800403), UINT64_C(0x8080808080040300), UINT64_C(0x8080808080040301),
    UINT64_C(0x8080808004030100), UINT64_C(0x8080808080040302), UINT64_C(0x8080808004030200),
    UINT64_C(0x8080808004030201), UINT64_C(0x8080800403020100), UINT64_C(0x8080808080808005),
    UINT64_C(0x8080808080800500), UINT64_C(0x8080808080800501), UINT64_C(0x8080808080050100),
    UINT64_C(0x8080808080800502), UINT64_C(0x8080808080050200), UINT64_C(0x8080808080050201),
    UINT64_C(0x8080808005020100), UINT64_C(0x8080808080800503), UINT64_C(0x8080808080050300),
    UINT64_C(0x8080808080050301), UINT64_C(0x8080808005030100), UINT64_C(0x8080808080050302),
    UINT64_C(0x8080808005030200), UINT64_C(0x8080808005030201), UINT64_C(0x8080800503020100),
    UINT64_C(0x8080808080800504), UINT64_C(0x8080808080050400), UINT64_C(0x8080808080050401),
    UINT64_C(0x8080808005040100), UINT64_C(0x8080808080050402), UINT64_C(0x8080808005040200),
    UINT64_C(0x8080808005040201), UINT64_C(0x8080800504020100), UINT64_C(0x8080808080050403),
    UINT64_C(0x8080808005040300), UINT64_C(0x8080808005040301), UINT64_C(0x8080800504030100),
    UINT64_C(0x8080808005040302), UINT64_C(0x8080800504030200), UINT64_C(0x8080800504030201),
    UINT64_C(0x8080050403020100), UINT64_C(0x8080808080808006), UINT64_C(0x8080808080800600),
    UINT64_C(0x8080808080800601), UINT64_C(0x8080808080060100), UINT64_C(0x8080808080800602),
    UINT64_C(0x8080808080060200), UINT64_C(0x8080808080060201), UINT64_C(0x8080808006020100),
    UINT64_C(0x8080808080800603), UINT64_C(0x8080808080060300), UINT64_C(0x8080808080060301),
    UINT64_C(0x8080808006030100), UINT64_C(0x8080808080060302), UINT64_C(0x8080808006030200),
    UINT64_C(0x8080808006030201), UINT64_C(0x8080800603020100), UINT64_C(0x8080808080800604),
    UINT64_C(0x8080808080060400), UINT64_C(0x8080808080060401), UINT64_C(0x8080808006040100),
    UINT64_C(0x8080808080060402), UINT64_C(0x8080808006040200), UINT64_C(0x8080808006040201),
    UINT64_C(0x8080800604020100), UINT64_C(0x8080808080060403), UINT64_C(0x8080808006040300),
    UINT64_C(0x8080808006040301), UINT64_C(0x8080800604030100), UINT64_C(0x8080808006040302),
    UINT64_C(0x8080800604030200), UINT64_C(0x8080800604030201), UINT64_C(0x8080060403020100),
    UINT64_C(0x8080808080800605), UINT64_C(0x8080808080060500), UINT64_C(0x8080808080060501),
    UINT64_C(0x8080808006050100), UINT64_C(0x8080808080060502), UINT64_C(0x8080808006050200),
    UINT64_C(0x8080808006050201), UINT64_C(0x8080800605020100), UINT64_C(0x8080808080060503),
    UINT64_C(0x8080808006050300), UINT64_C(0x8080808006050301), UINT64_C(0x8080800605030100),
    UINT64_C(0x8080808006050302), UINT64_C(0x8080800605030200), UINT64_C(0x8080800605030201),
    UINT64_C(0x8080060503020100), UINT64_C(0x8080808080060504), UINT64_C(0x8080808006050400),
    UINT64_C(0x8080808006050401), UINT64_C(0x8080800605040100), UINT64_C(0x8080808006050402),
    UINT64_C(0x8080800605040200), UINT64_C(0x8080800605040201), UINT64_C(0x8080060504020100),
    UINT64_C(0x8080808006050403), UINT64_C(0x8080800605040300), UINT64_C(0x8080800605040301),
    UINT64_C(0x8080060504030100), UINT64_C(0x8080800605040302), UINT64_C(0x8080060504030200),
    UINT64_C(0x8080060504030201), UINT64_C(0x8006050403020100), UINT64_C(0x8080808080808007),
    UINT64_C(0x8080808080800700), UINT64_C(0x8080808080800701), UINT64_C(0x8080808080070100),
    UINT64_C(0x8080808080800702), UINT64_C(0x8080808080070200), UINT64_C(0x8080808080070201),
    UINT64_C(0x8080808007020100), UINT64_C(0x8080808080800703), UINT64_C(0x8080808080070300),
    UINT64_C(0x8080808080070301), UINT64_C(0x8080808007030100), UINT64_C(0x8080808080070302),
    UINT64_C(0x8080808007030200), UINT64_C(0x8080808007030201), UINT64_C(0x8080800703020100),
    UINT64_C(0x8080808080800704), UINT64_C(0x8080808080070400), UINT64_C(0x8080808080070401),
    UINT64_C(0x8080808007040100), UINT64_C(0x8080808080070402), UINT64_C(0x8080808007040200),
    UINT64_C(0x8080808007040201), UINT64_C(0x8080800704020100), UINT64_C(0x8080808080070403),
    UINT64_C(0x8080808007040300), UINT64_C(0x8080808007040301), UINT64_C(0x8080800704030100),
    UINT64_C(0x8080808007040302), UINT64_C(0x8080800704030200), UINT64_C(0x8080800704030201),
    UINT64_C(0x8080070403020100), UINT64_C(0x8080808080800705), UINT64_C(0x8080808080070500),
    UINT64_C(0x8080808080070501), UINT64_C(0x8080808007050100), UINT64_C(0x8080808080070502),
    UINT64_C(0x8080808007050200), UINT64_C(0x8080808007050201), UINT64_C(0x8080800705020100),
    UINT64_C(0x8080808080070503), UINT64_C(0x8080808007050300), UINT64_C(0x8080808007050301),
    UINT64_C(0x8080800705030100), UINT64_C(0x8080808007050302), UINT64_C(0x8080800705030200),
    UINT64_C(0x8080800705030201), UINT64_C(0x8080070503020100), UINT64_C(0x8080808080070504),
    UINT64_C(0x8080808007050400), UINT64_C(0x8080808007050401), UINT64_C(0x8080800705040100),
    UINT64_C(0x8080808007050402), UINT64_C(0x8080800705040200), UINT64_C(0x8080800705040201),
    UINT64_C(0x8080070504020100), UINT64_C(0x8080808007050403), UINT64_C(0x8080800705040300),
    UINT64_C(0x8080800705040301), UINT64_C(0x8080070504030100), UINT64_C(0x8080800705040302),
    UINT64_C(0x8080070504030200), UINT64_C(0x8080070504030201), UINT64_C(0x8007050403020100),
    UINT64_C(0x8080808080800706), UINT64_C(0x8080808080070600), UINT64_C(0x8080808080070601),
    UINT64_C(0x8080808007060100), UINT64_C(0x8080808080070602), UINT64_C(0x8080808007060200),
    UINT64_C(0x8080808007060201), UINT64_C(0x8080800706020100), UINT64_C(0x8080808080070603),
    UINT64_C(0x8080808007060300), UINT64_C(0x8080808007060301), UINT64_C(0x8080800706030100),
    UINT64_C(0x8080808007060302), UINT64_C(0x8080800706030200), UINT64_C(0x8080800706030201),
    UINT64_C(0x8080070603020100), UINT64_C(0x8080808080070604), UINT64_C(0x8080808007060400),
    UINT64_C(0x8080808007060401), UINT64_C(0x8080800706040100), UINT64_C(0x8080808007060402),
    UINT64_C(0x8080800706040200), UINT64_C(0x8080800706040201), UINT64_C(0x8080070604020100),
    UINT64_C(0x8080808007060403), UINT64_C(0x8080800706040300), UINT64_C(0x8080800706040301),
    UINT64_C(0x8080070604030100), UINT64_C(0x8080800706040302), UINT64_C(0x8080070604030200),
    UINT64_C(0x8080070604030201), UINT64_C(0x8007060403020100), UINT64_C(0x8080808080070605),
    UINT64_C(0x8080808007060500), UINT64_C(0x8080808007060501), UINT64_C(0x8080800706050100),
    UINT64_C(0x8080808007060502), UINT64_C(0x8080800706050200), UINT64_C(0x8080800706050201),
    UINT64_C(0x8080070605020100), UINT64_C(0x8080808007060503), UINT64_C(0x8080800706050300),
    UINT64_C(0x8080800706050301), UINT64_C(0x8080070605030100), UINT64_C(0x8080800706050302),
    UINT64_C(0x8080070605030200), UINT64_C(0x8080070605030201), UINT64_C(0x8007060503020100),
    UINT64_C(0x8080808007060504), UINT64_C(0x8080800706050400), UINT64_C(0x8080800706050401),
    UINT64_C(0x8080070605040100), UINT64_C(0x8080800706050402), UINT64_C(0x8080070605040200),
    UINT64_C(0x8080070605040201), UINT64_C(0x8007060504020100), UINT64_C(0x8080800706050403),
    UINT64_C(0x8080070605040300), UINT64_C(0x8080070605040301), UINT64_C(0x8007060504030100),
    UINT64_C(0x8080070605040302), UINT64_C(0x8007060504030200), UINT64_C(0x8007060504030201),
    UINT64_C(0x0706050403020100),
};

static const u8 simd_bit_counts8[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2,
    3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
    3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5,
    6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
    3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4,
    5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6,
    6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
};

static force_inline(u32) simd_emit_run_starts_avx2(
    simdv_i256 tags, u32 changes, u32 base, u32 available, rop(rw u8) classified, rop(rw u32) starts) {
    u8 bytes[32];
    _mm256_storeu_si256((simdv_i256*)bytes, tags);
    u32 written = 0;
    if (available < 32) {
        while (changes) {
            u32 index = (u32)__builtin_ctz(changes);
            changes &= changes - 1;
            classified[written] = bytes[index];
            starts[written++]   = base + index;
        }
    } else {
        for (u32 shift = 0; shift < 32; shift += 8) {
            u8         mask      = (u8)(changes >> shift);
            simdv_i128 indices   = _mm_loadl_epi64((const simdv_i128*)&simd_bit_indices8[mask]);
            simdv_i256 positions = _mm256_cvtepu8_epi32(indices);
            positions            = _mm256_add_epi32(positions, _mm256_set1_epi32((i32)(base + shift)));
            simdv_i128 part      = _mm_loadl_epi64((const simdv_i128*)(bytes + shift));
            simdv_i128 packed    = _mm_shuffle_epi8(part, indices);
            _mm256_storeu_si256((simdv_i256*)(starts + written), positions);
            _mm_storel_epi64((simdv_i128*)(classified + written), packed);
            written += simd_bit_counts8[mask];
        }
    }
    return written;
}

#else

#if !defined(__AVX512F__) || !defined(__AVX512VL__) || !defined(__AVX512BW__) || !defined(__AVX512VBMI2__)
#error "SIMD_USE_AVX512 requires AVX-512F, VL, BW and VBMI2 compiler flags"
#endif
static force_inline(u32)
    simd_emit_run_starts_avx512(simdv_i256 tags, u32 changes, u32 base, rop(rw u8) classified, rop(rw u32) starts) {
    const __m512i indices     = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    u32           produced    = (u32)__builtin_popcount(changes);
    simdv_i256    packed_tags = _mm256_maskz_compress_epi8((__mmask32)changes, tags);
    _mm256_mask_storeu_epi8(classified, (__mmask32)((UINT64_C(1) << produced) - 1), packed_tags);
    __m512i   positions     = _mm512_add_epi32(indices, _mm512_set1_epi32((i32)base));
    __mmask16 low_mask      = (__mmask16)changes;
    __mmask16 high_mask     = (__mmask16)(changes >> 16);
    u32       low_count     = (u32)__builtin_popcount((u32)low_mask);
    u32       high_count    = produced - low_count;
    __m512i   low_positions = _mm512_maskz_compress_epi32(low_mask, positions);
    positions               = _mm512_add_epi32(positions, _mm512_set1_epi32(16));
    __m512i high_positions  = _mm512_maskz_compress_epi32(high_mask, positions);
    _mm512_mask_storeu_epi32(starts, (__mmask16)((1u << low_count) - 1), low_positions);
    _mm512_mask_storeu_epi32(starts + low_count, (__mmask16)((1u << high_count) - 1), high_positions);
    return produced;
}

#endif

// ASCII candidates including NUL. Tags: unmatched = 0, NUL = 128.
// Both outputs need count entries; returns their shared count. Starts are relative to src.
// Zero-initialize carry. The first nonempty call includes start 0; continuations don't restart.
// Single spaces between unmatched bytes merge into text within each 32-byte block.
// A partial 32-byte block must be the final call. No EOF sentinel is emitted.
static force_inline(u32) simd_byte_scan_runs(rop(ro u8) src,
                                             u32 count,
                                             rop(ro SimdByteScanMany) table,
                                             rop(rw SimdByteRunCarry) carry,
                                             rop(rw u8) classified,
                                             rop(rw u32) starts) {
    const simdv_i256 nibbles  = _mm256_set1_epi8(15);
    const simdv_i256 zero     = _mm256_setzero_si256();
    const simdv_i256 low_lut  = _mm256_broadcastsi128_si256(table->low[0]);
    const simdv_i256 high_lut = _mm256_broadcastsi128_si256(table->high[0]);
    u32              first    = !carry->started;
    u32              written  = 0;

    for (u32 base = 0; base < count;) {
        u32       take     = min(32, count - base);
        const u8* input    = src + base;
        u8        tail[32] = {0};
        if (take < 32) {
            memcpy(tail, input, take);
            input = tail;
        }

        simdv_i256 raw   = _mm256_loadu_si256((const simdv_i256*)input);
        simdv_i256 low   = _mm256_and_si256(raw, nibbles);
        simdv_i256 high  = _mm256_and_si256(_mm256_srli_epi16(raw, 4), nibbles);
        simdv_i256 found = _mm256_and_si256(_mm256_shuffle_epi8(low_lut, low), _mm256_shuffle_epi8(high_lut, high));
        simdv_i256 tags  = _mm256_andnot_si256(_mm256_cmpeq_epi8(found, zero), raw);
        tags = _mm256_or_si256(tags, _mm256_and_si256(_mm256_cmpeq_epi8(raw, zero), _mm256_set1_epi8((char)128)));
        simdv_i256 bridge    = _mm256_permute2x128_si256(carry->previous, tags, 0x21);
        simdv_i256 preceding = _mm256_alignr_epi8(tags, bridge, 15);

        // Temporary special case to remove spaces in prose
        simdv_i256 text     = _mm256_cmpeq_epi8(tags, zero);
        simdv_i256 spaces   = _mm256_cmpeq_epi8(tags, _mm256_set1_epi8(' '));
        bridge              = _mm256_permute2x128_si256(zero, text, 0x20);
        simdv_i256 left     = _mm256_alignr_epi8(text, bridge, 15);
        bridge              = _mm256_permute2x128_si256(text, zero, 0x21);
        simdv_i256 right    = _mm256_alignr_epi8(bridge, text, 1);
        simdv_i256 interior = _mm256_and_si256(spaces, _mm256_and_si256(left, right));

        // Drop both boundaries of 0-space-0; block edges stay unmerged.
        bridge                = _mm256_permute2x128_si256(zero, interior, 0x20);
        simdv_i256 following  = _mm256_alignr_epi8(interior, bridge, 15);
        simdv_i256 drop       = _mm256_or_si256(interior, following);
        simdv_i256 same       = _mm256_cmpeq_epi8(tags, preceding);
        simdv_i256 boundaries = _mm256_andnot_si256(same, _mm256_cmpeq_epi8(zero, zero));
        boundaries            = _mm256_andnot_si256(drop, boundaries);
        u32 changes           = (u32)_mm256_movemask_epi8(boundaries);
        changes               = (changes | first) & (u32)((UINT64_C(1) << take) - 1);
        first                 = 0;

        if (changes) {
#if defined(SIMD_USE_AVX512)
            written += simd_emit_run_starts_avx512(tags, changes, base, classified + written, starts + written);
#else
            written +=
                simd_emit_run_starts_avx2(tags, changes, base, count - written, classified + written, starts + written);
#endif
        }

        carry->previous = tags;
        base += take;
    }
    carry->started |= count != 0;
    return written;
}

void simd_byte_reverse(rop(ro u8) src, rop(rw u8) dst, u32 size) {
    u64 n           = size / sizeof(__m128);
    u64 rem         = size % sizeof(__m128);
    u8  mask_vals[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

    simdv_i128  mask = _mm_loadu_si128((simdv_i128*)mask_vals); // Requires SSE2
    simdv_i128  tmp;
    simdv_i128* s = (simdv_i128*)src;
    simdv_i128* d = (simdv_i128*)(dst + rem);

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

    simdv_i128  mask = _mm_loadu_si128((simdv_i128*)mask_vals); // Requires SSE2
    simdv_i128  a, b;
    simdv_i128* start = (simdv_i128*)buf;
    simdv_i128* end   = (simdv_i128*)&buf[size];

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

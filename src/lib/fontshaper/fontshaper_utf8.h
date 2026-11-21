#ifndef FONTSHAPER_UTF8_H
#define FONTSHAPER_UTF8_H

#include <stdint.h>
#ifdef __AVX2__
#include <immintrin.h>
#endif

/* AVX2 widening/shuffle paths adapted from simdutf v9.4.0:
 * https://github.com/simdutf/simdutf/blob/v9.4.0/src/haswell/avx2_convert_utf8_to_utf32.cpp
 * ponytail: mixed-width and four-byte sequences use scalar decoding. For
 * other SIMD targets (e.g. NEON), see https://github.com/simdutf/simdutf/tree/v9.4.0/src/arm64
 *
 * Copyright 2021 The simdutf authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

typedef enum FontShaperUtf8Error {
    FONTSHAPER_UTF8_OK,
    FONTSHAPER_UTF8_INVALID_SEQUENCE,
    FONTSHAPER_UTF8_INVALID_SCALAR,
} FontShaperUtf8Error;

typedef struct FontShaperUtf8Result {
    uint32_t count;
    uint32_t bytes_read;
    FontShaperUtf8Error error;
    uint32_t skip_bytes; // Legacy shaping recovery; not part of the valid prefix.
} FontShaperUtf8Result;

/* Decode a strict UTF-8 prefix into two caller-provided arrays of capacity
 * elements each. Stops before the first malformed sequence (error != OK),
 * at end of input, or when output is full. Offsets are relative to text.
 * No input padding, output slack, or allocation required. */
static inline FontShaperUtf8Result font_shaper_decode_utf8_buffer(
    const char *text, uint32_t length, uint32_t *codepoints, uint32_t *offsets, uint32_t capacity)
{
    const unsigned char *bytes = (const unsigned char *)text;
    FontShaperUtf8Result result = {0, 0, FONTSHAPER_UTF8_OK, 0};
    while (result.bytes_read < length && result.count < capacity) {
        uint32_t at = result.bytes_read;
        uint32_t remaining = length - at;
#ifdef __AVX2__
        uint32_t room = capacity - result.count;
        if (remaining >= 16 && room >= 4) {
            __m128i input = _mm_loadu_si128((const __m128i *)(bytes + at));
            unsigned count = 0, width = 0;
            uint32_t *out = codepoints + result.count;
            if (room >= 16 && _mm_movemask_epi8(input) == 0) {
                _mm256_storeu_si256((__m256i *)out, _mm256_cvtepu8_epi32(input));
                _mm256_storeu_si256((__m256i *)(out + 8), _mm256_cvtepu8_epi32(_mm_srli_si128(input, 8)));
                count = 16;
                width = 1;
            } else {
                __m128i continuation = _mm_cmpeq_epi8(_mm_and_si128(input, _mm_set1_epi8(-64)), _mm_set1_epi8(-128));
                unsigned tails = (unsigned)_mm_movemask_epi8(continuation);
                if (room >= 8 && tails == 0xaaaa) {
                    // Signed byte comparisons select only C2..DF, excluding overlong leads.
                    __m128i leads = _mm_and_si128(_mm_cmpgt_epi8(input, _mm_set1_epi8(-63)),
                                                 _mm_cmpgt_epi8(_mm_set1_epi8(-32), input));
                    if (((unsigned)_mm_movemask_epi8(leads) & 0x5555) == 0x5555) {
                        __m128i pairs = _mm_shuffle_epi8(input, _mm_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14));
                        __m128i high = _mm_srli_epi16(_mm_and_si128(pairs, _mm_set1_epi16(0x1f00)), 2);
                        __m128i low = _mm_and_si128(pairs, _mm_set1_epi16(0x3f));
                        _mm256_storeu_si256((__m256i *)out, _mm256_cvtepu16_epi32(_mm_or_si128(high, low)));
                        count = 8;
                        width = 2;
                    }
                } else if ((tails & 0xfff) == 0xdb6) {
                    __m128i leads = _mm_cmpeq_epi8(_mm_and_si128(input, _mm_set1_epi8(-16)), _mm_set1_epi8(-32));
                    if (((unsigned)_mm_movemask_epi8(leads) & 0x249) == 0x249) {
                        __m128i triples = _mm_shuffle_epi8(input, _mm_setr_epi8(2, 1, 0, -1, 5, 4, 3, -1, 8, 7, 6, -1, 11, 10, 9, -1));
                        __m128i high = _mm_srli_epi32(_mm_and_si128(triples, _mm_set1_epi32(0x0f0000)), 4);
                        __m128i mid = _mm_srli_epi32(_mm_and_si128(triples, _mm_set1_epi32(0x3f00)), 2);
                        __m128i low = _mm_and_si128(triples, _mm_set1_epi32(0x3f));
                        __m128i decoded = _mm_or_si128(high, _mm_or_si128(mid, low));
                        __m128i overlong = _mm_cmpgt_epi32(_mm_set1_epi32(0x800), decoded);
                        __m128i surrogate = _mm_cmpeq_epi32(_mm_and_si128(decoded, _mm_set1_epi32(0xf800)), _mm_set1_epi32(0xd800));
                        if (_mm_movemask_epi8(_mm_or_si128(overlong, surrogate)) == 0) {
                            _mm_storeu_si128((__m128i *)out, decoded);
                            count = 4;
                            width = 3;
                        }
                    }
                }
            }
            if (count != 0) {
                for (unsigned i = 0; i < count; i += 1)
                    offsets[result.count + i] = at + i * width;
                result.count += count;
                result.bytes_read += count * width;
                continue;
            }
        }
#endif
        uint32_t cp = bytes[at], width = 1, minimum = 0;
        if (cp >= 0x80) {
            if (cp >= 0xc0 && cp <= 0xdf) {
                width = 2; minimum = 0x80; cp &= 0x1f;
            } else if (cp >= 0xe0 && cp <= 0xef) {
                width = 3; minimum = 0x800; cp &= 0x0f;
            } else if (cp >= 0xf0) {
                width = 4; minimum = 0x10000; cp &= 0x07;
            } else goto invalid;
            if (remaining < width) goto invalid;
            for (uint32_t i = 1; i < width; i += 1) {
                unsigned byte = bytes[at + i];
                if ((byte & 0xc0) != 0x80) {
                    result.skip_bytes = i + 1;
                    goto invalid;
                }
                cp = (cp << 6) | (byte & 0x3f);
            }
            if (bytes[at] > 0xf4 || cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                result.error = FONTSHAPER_UTF8_INVALID_SCALAR;
                return result;
            }
        }
        codepoints[result.count] = cp;
        offsets[result.count] = at;
        result.count += 1;
        result.bytes_read += width;
    }
    return result;

invalid:
    result.error = FONTSHAPER_UTF8_INVALID_SEQUENCE;
    if (result.skip_bytes == 0) result.skip_bytes = 1;
    return result;
}

#endif

// Build: cc -O3 -march=native -std=gnu11 md_scan_bench.c -o md_scan_bench
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const unsigned char candidates[] = {'\n', 0, '`', '*', '_', '~', '|', '[', ']', '!', '<', '\\', '&'};
static volatile uint64_t sink;

typedef struct {
    __m128i equals[sizeof(candidates)];
    __m128i low[2];
    __m128i high[2];
} Tables;

static void init_tables(Tables* tables) {
    unsigned char low[2][16] = {{0}};
    unsigned char high[2][16] = {{0}};
    for (unsigned i = 0; i < sizeof(candidates); ++i) {
        unsigned c = candidates[i];
        unsigned group = c >> 7;
        unsigned bit = 1u << ((c >> 4) & 7);
        low[group][c & 15] |= bit;
        high[group][c >> 4 & 15] = bit;
        tables->equals[i] = _mm_set1_epi8((char)c);
    }
    for (unsigned i = 0; i < 2; ++i) {
        tables->low[i] = _mm_loadu_si128((const __m128i*)low[i]);
        tables->high[i] = _mm_loadu_si128((const __m128i*)high[i]);
    }
}

static inline uint64_t scan_equals(const unsigned char* src, const Tables* tables) {
    uint64_t mask = 0;
    for (unsigned chunk = 0; chunk < 4; ++chunk) {
        __m128i v = _mm_loadu_si128((const __m128i*)(src + 16 * chunk));
        __m128i hits = _mm_setzero_si128();
        for (unsigned i = 0; i < sizeof(candidates); ++i)
            hits = _mm_or_si128(hits, _mm_cmpeq_epi8(v, tables->equals[i]));
        mask |= (uint64_t)(unsigned)_mm_movemask_epi8(hits) << (16 * chunk);
    }
    return mask;
}

static inline uint64_t scan_nibbles(const unsigned char* src, const Tables* tables) {
    const __m128i nibbles = _mm_set1_epi8(15);
    uint64_t mask = 0;
    for (unsigned chunk = 0; chunk < 4; ++chunk) {
        __m128i v = _mm_loadu_si128((const __m128i*)(src + 16 * chunk));
        __m128i lo = _mm_and_si128(v, nibbles);
        __m128i hi = _mm_and_si128(_mm_srli_epi16(v, 4), nibbles);
        __m128i a = _mm_and_si128(_mm_shuffle_epi8(tables->low[0], lo), _mm_shuffle_epi8(tables->high[0], hi));
        __m128i b = _mm_and_si128(_mm_shuffle_epi8(tables->low[1], lo), _mm_shuffle_epi8(tables->high[1], hi));
        __m128i hits = _mm_or_si128(a, b);
        mask |= (uint64_t)((unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(hits, _mm_setzero_si128())) ^ 0xffffu) << (16 * chunk);
    }
    return mask;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static uint64_t run(const unsigned char* text, size_t length, const Tables* tables, int nibble, unsigned rounds, double* seconds) {
    uint64_t checksum = 0;
    uint64_t start = now_ns();
    for (unsigned round = 0; round < rounds; ++round)
        for (size_t offset = 0; offset < length; offset += 64) {
            uint64_t mask = nibble ? scan_nibbles(text + offset, tables) : scan_equals(text + offset, tables);
            checksum += (uint64_t)__builtin_popcountll(mask);
        }
    *seconds = (now_ns() - start) / 1e9;
    sink = checksum;
    return checksum;
}

int main(int argc, char** argv) {
    size_t length = argc > 1 ? strtoull(argv[1], NULL, 10) : 4 * 1024 * 1024;
    unsigned rounds = argc > 2 ? strtoul(argv[2], NULL, 10) : 200;
    length = (length + 63) & ~(size_t)63;
    if (!length || !rounds) return 1;
    unsigned char* text = malloc(length);
    if (!text) return 1;
    static const char* fragments[] = {
        "This is a paragraph with **strong** and *emphasis*.\n",
        "## Heading with [link](https://example.org)\n",
        "> Quoted text, `code`, and &amp; entities.\n",
        "- item one\n- item two ~~removed~~ and ||spoiler||\n\n",
        "| Name | Value |\n| --- | --- |\n| alpha | 123 |\n",
        "```c\nint x = 42; // example\n```\n",
        "Plain prose with occasional punctuation, spaces, and newlines.\n",
    };
    uint32_t rng = 123456789;
    for (size_t pos = 0; pos < length;) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const char* fragment = fragments[rng % (sizeof(fragments) / sizeof(fragments[0]))];
        size_t n = strlen(fragment);
        if (n > length - pos) n = length - pos;
        memcpy(text + pos, fragment, n);
        pos += n;
    }
    Tables tables;
    init_tables(&tables);
    for (size_t pos = 0; pos < length; pos += 64) {
        uint64_t a = scan_equals(text + pos, &tables);
        uint64_t b = scan_nibbles(text + pos, &tables);
        if (a != b) {
            fprintf(stderr, "Mismatch at offset %zu: %016llx vs %016llx\n", pos, (unsigned long long)a, (unsigned long long)b);
            return 1;
        }
    }
    for (unsigned c = 0; c < 256; ++c) {
        unsigned char sample[64];
        memset(sample, c, sizeof(sample));
        if (scan_equals(sample, &tables) != scan_nibbles(sample, &tables)) {
            fprintf(stderr, "Mismatch on byte %u\n", c);
            free(text);
            return 1;
        }
    }
    double eq_seconds, nib_seconds;
    uint64_t eq = run(text, length, &tables, 0, rounds, &eq_seconds);
    uint64_t nib = run(text, length, &tables, 1, rounds, &nib_seconds);
    printf("%zu bytes, %u rounds, %llu matches per round\n", length, rounds, (unsigned long long)(eq / rounds));
    printf("broadcast equals: %.3f s, %.2f GiB/s\n", eq_seconds, length * (double)rounds / eq_seconds / (1024 * 1024 * 1024));
    printf("nibble lookup:    %.3f s, %.2f GiB/s\n", nib_seconds, length * (double)rounds / nib_seconds / (1024 * 1024 * 1024));
    free(text);
    return eq != nib;
}

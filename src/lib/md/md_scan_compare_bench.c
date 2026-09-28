// Build: cc -O3 -march=native -std=gnu11 md_scan_compare_bench.c -o /tmp/md_scan_compare_bench
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIZE (4u * 1024u * 1024u)
#define ROUNDS 60
static volatile uint64_t sink;
static const unsigned char markdown[] = {'\n', 0, '`', '*', '_', '~', '|', '[', ']', '!', '<', '\\', '&'};
typedef struct {
    unsigned char bytes[256];
    unsigned count;
    __m512i eq[256];
    __m512i lo[2], hi[2];
    __m512i lookup[4];
    __m512i present[4];
    uint64_t or_mask;
    unsigned char or_bit[8];
    unsigned or_count;
    uint64_t bitmap[4];
} Set;

static void init(Set* s, const unsigned char* bytes, unsigned count) {
    memset(s, 0, sizeof(*s));
    uint8_t low[2][64] = {{0}}, high[2][64] = {{0}}, lookup[4][64], present[4][64] = {{0}};
    memset(lookup, 0, sizeof(lookup));
    s->count = count;
    for (unsigned i = 0; i < count; ++i) {
        unsigned c = bytes[i];
        s->bytes[i] = c;
        s->eq[i] = _mm512_set1_epi8((char)c);
        s->or_mask |= c;
        s->bitmap[c / 64] |= 1ULL << (c % 64);
        lookup[c / 64][c % 64] = (unsigned char)c;
        present[c / 64][c % 64] = 1;
        low[c >> 7][c & 15] |= 1u << ((c >> 4) & 7);
        high[c >> 7][(c >> 4) & 15] = 1u << ((c >> 4) & 7);
    }
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned lane = 1; lane < 4; ++lane) {
            memcpy(low[i] + lane * 16, low[i], 16);
            memcpy(high[i] + lane * 16, high[i], 16);
        }
        s->lo[i] = _mm512_loadu_si512(low[i]);
        s->hi[i] = _mm512_loadu_si512(high[i]);
    }
    for (unsigned i = 0; i < 4; ++i) {
        s->lookup[i] = _mm512_loadu_si512(lookup[i]);
        s->present[i] = _mm512_loadu_si512(present[i]);
    }
    for (unsigned bit = 0; bit < 8; ++bit)
        if (!(s->or_mask & (1u << bit))) s->or_bit[s->or_count++] = bit;
}

static inline uint64_t eq_scan(const unsigned char* p, const Set* s) {
    __m512i v = _mm512_loadu_si512(p);
    __mmask64 hits = 0;
    for (unsigned i = 0; i < s->count; ++i) hits |= _mm512_cmpeq_epi8_mask(v, s->eq[i]);
    return hits;
}
static inline uint64_t nib_scan(const unsigned char* p, const Set* s) {
    __m512i v = _mm512_loadu_si512(p);
    __m512i lo = _mm512_and_si512(v, _mm512_set1_epi8(15));
    __m512i hi = _mm512_and_si512(_mm512_srli_epi16(v, 4), _mm512_set1_epi8(15));
    __m512i a = _mm512_and_si512(_mm512_shuffle_epi8(s->lo[0], lo), _mm512_shuffle_epi8(s->hi[0], hi));
    __m512i b = _mm512_and_si512(_mm512_shuffle_epi8(s->lo[1], lo), _mm512_shuffle_epi8(s->hi[1], hi));
    return ~_mm512_cmpeq_epi8_mask(_mm512_or_si512(a, b), _mm512_setzero_si512());
}
static inline uint64_t or_scan(const unsigned char* p, const Set* s) {
    __m512i v = _mm512_loadu_si512(p);
    __m512i forbidden = _mm512_andnot_si512(_mm512_set1_epi8((char)s->or_mask), v);
    uint64_t possible = _mm512_cmpeq_epi8_mask(forbidden, _mm512_setzero_si512());
    uint64_t result = 0;
    while (possible) {
        unsigned i = __builtin_ctzll(possible);
        possible &= possible - 1;
        unsigned c = p[i];
        result |= ((s->bitmap[c / 64] >> (c % 64)) & 1ULL) << i;
    }
    return result;
}
static inline uint64_t memchr_scan(const unsigned char* p, const Set* s) {
    uint64_t mask = 0;
    const unsigned char* at = p;
    while (at < p + 64) {
        const unsigned char* hit = memchr(at, s->bytes[0], (size_t)(p + 64 - at));
        if (!hit) break;
        mask |= 1ULL << (hit - p);
        at = hit + 1;
    }
    return mask;
}
static inline uint64_t lookup_scan(const unsigned char* p, const Set* s) {
    __m512i v = _mm512_loadu_si512(p);
    __m512i index = _mm512_and_si512(v, _mm512_set1_epi8(63));
    __m512i group = _mm512_and_si512(_mm512_srli_epi16(v, 6), _mm512_set1_epi8(3));
    __mmask64 hits = 0;
    for (unsigned i = 0; i < 4; ++i) {
        __m512i table = _mm512_permutexvar_epi8(index, s->lookup[i]);
        __m512i valid = _mm512_permutexvar_epi8(index, s->present[i]);
        hits |= _mm512_cmpeq_epi8_mask(v, table) & _mm512_cmpneq_epi8_mask(valid, _mm512_setzero_si512()) & _mm512_cmpeq_epi8_mask(group, _mm512_set1_epi8(i));
    }
    return hits;
}
static uint64_t scan(const unsigned char* p, const Set* s, unsigned method) {
    switch (method) {
        case 0: return eq_scan(p,s);
        case 1: return nib_scan(p,s);
        case 2: return or_scan(p,s);
        default: return lookup_scan(p,s);
    }
}
static uint64_t ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
int main(void) {
    unsigned char *p = aligned_alloc(64, SIZE);
    if (!p) return 1;
    const char* fragments[] = {
        "This is a paragraph with **strong** and *emphasis*.\n",
        "## Heading with [link](https://example.org)\n",
        "> Quoted text, `code`, and &amp; entities.\n",
        "- item one\n- item two ~~removed~~ and ||spoiler||\n\n",
        "| Name | Value |\n| --- | --- |\n| alpha | 123 |\n",
        "```c\nint x = 42; // example\n```\n",
        "Plain prose with occasional punctuation, spaces, and newlines.\n"};
    uint32_t rng = 123456789;
    for (size_t pos = 0; pos < SIZE;) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const char* f = fragments[rng % 7];
        size_t n = strlen(f); if (n > SIZE-pos) n=SIZE-pos;
        memcpy(p+pos, f, n); pos += n;
    }
    unsigned char sets[6][256];
    const unsigned counts[] = {1,4,8,13,32,100};
    for (unsigned j = 0; j < 6; ++j) {
        unsigned n = counts[j];
        if (n == 13) memcpy(sets[j], markdown, 13);
        else if (n <= 8) memcpy(sets[j], markdown, n);
        else {
            memcpy(sets[j], markdown, 13);
            for (unsigned x = 13; x < n; ++x) {
                unsigned c = (x * 73 + 29) & 255;
                for (;;) {
                    int exists=0;
                    for (unsigned k=0; k<x; ++k) if (sets[j][k] == c) exists=1;
                    if (!exists) break;
                    c=(c+1)&255;
                }
                sets[j][x] = c;
            }
        }
        Set *s = aligned_alloc(64, sizeof(Set));
        init(s, sets[j], n);
        unsigned char all[256];
        for (unsigned i=0;i<256;++i) all[i]=i;
        for (unsigned i=0;i<256;i+=64)
            for (unsigned m=1;m<4;++m)
                if (scan(all+i,s,0) != scan(all+i,s,m)) {
                    fprintf(stderr,"mismatch: count %u method %u byte block %u\n",n,m,i);
                    return 1;
                }
        for (size_t i=0;i<SIZE;i+=64)
            for (unsigned m=1;m<4;++m)
                if (scan(p+i,s,0) != scan(p+i,s,m)) {
                    fprintf(stderr,"mismatch at %zu count %u method %u\n",i,n,m);
                    return 1;
                }
        double t[4];
        double memchr_time = 0;
        if (n == 1) {
            for (size_t i=0;i<SIZE;i+=64)
                if (scan(p+i,s,0) != memchr_scan(p+i,s)) {
                    fprintf(stderr,"memchr mismatch at %zu\n", i);
                    return 1;
                }
            uint64_t start = ns(), sum = 0;
            for (unsigned r=0;r<ROUNDS;++r)
                for (size_t i=0;i<SIZE;i+=64) sum += __builtin_popcountll(memchr_scan(p+i,s));
            memchr_time = (ns()-start)/1e9;
            sink = sum;
        }
        for (unsigned pass=0;pass<4;++pass) {
            unsigned m=(pass+j)%4;
            uint64_t start=ns(), sum=0;
            for (unsigned r=0;r<ROUNDS;++r)
                for (size_t i=0;i<SIZE;i+=64) sum+=__builtin_popcountll(scan(p+i,s,m));
            t[m]=(ns()-start)/1e9;
            sink=sum;
        }
        printf("%3u candidates: eq %6.1f  nib %6.1f  OR+scalar %6.1f  VBMI-table %6.1f GiB/s\n",
               n, SIZE*(double)ROUNDS/t[0]/(1u<<30), SIZE*(double)ROUNDS/t[1]/(1u<<30),
               SIZE*(double)ROUNDS/t[2]/(1u<<30), SIZE*(double)ROUNDS/t[3]/(1u<<30));
        if (n == 1) printf("  memchr repeated (all matches): %.1f GiB/s\n", SIZE*(double)ROUNDS/memchr_time/(1u<<30));
        free(s);
    }
    free(p);
}

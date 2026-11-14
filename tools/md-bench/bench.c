#include <lib/grim/logger.h>
#include "baseline.h"
#include "candidate.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// O(1) adapters preserve each constructor's native return type and metadata layout.
typedef struct BenchState {
    const u8* l0_tags;
    const u32* l0_offsets;
    const void* l0_common;
    const void* l0_extra;
    const void* l0_metadata;
    u32 l0_count;
    u32 thin_metadata;
    u32 trailing_blank_count;
    const u8* l1_tags;
    const u32* l1_offsets;
    const u32* l1_lengths;
    u32 l1_count;
    const MdMap* mappings;
    u32 mappings_count;
} BenchState;
#include "adapters.h"

static volatile u64 sink;
static double now(clockid_t clock) {
    struct timespec time;
    if (clock_gettime(clock, &time)) abort();
    return time.tv_sec + time.tv_nsec * 1e-9;
}
static u64 check(const BenchState* state, const DocView* source) {
    if (!state->l0_count || state->l0_tags[state->l0_count - 1] != MD_L0_EOF ||
        state->l0_offsets[state->l0_count - 1] != source->total_length) abort();
    u64 hash = 1469598103934665603ULL;
    u32 extra_index = 0;
    for (u32 i = 0; i < state->l0_count; ++i) {
        hash = (hash ^ state->l0_offsets[i]) * 1099511628211ULL;
        hash = (hash ^ state->l0_tags[i]) * 1099511628211ULL;
        u8 tag = state->l0_tags[i];
        u32 pre_indent = 0, content_start = 0, extra = 0;
#if BENCH_THIN
        if (state->thin_metadata) {
            MdL0CommonThin common = ((const MdL0CommonThin*)state->l0_common)[i];
            if (tag == MD_L0_BLANK) extra = common.line_count;
            else {
                pre_indent = common.pre_indent;
                content_start = common.content_start;
            }
            if (tag == MD_L0_EOF) extra = state->trailing_blank_count;
            if (tag == MD_L0_BLOCK_QUOTE || tag == MD_L0_UNORDERED_LIST)
                extra = ((const MdL0ExtraThin*)state->l0_extra)[extra_index++].post_indent;
        } else
#endif
        if (state->l0_common) {
#if BENCH_SPLIT
            MdL0Common common = ((const MdL0Common*)state->l0_common)[i];
            pre_indent = common.pre_indent;
            content_start = common.content_start;
            if (tag != MD_L0_PARAGRAPH) {
                MdL0Extra metadata = ((const MdL0Extra*)state->l0_extra)[extra_index++];
                if (tag == MD_L0_BLANK || tag == MD_L0_EOF) extra = metadata.line_count;
                else extra = metadata.post_indent;
            }
#endif
        } else {
#if BENCH_LEGACY
            MdL0Metadata metadata = ((const MdL0Metadata*)state->l0_metadata)[i];
            pre_indent = metadata.pre_indent;
            if (tag == MD_L0_BLANK || tag == MD_L0_EOF) extra = metadata.line_count;
            else {
                content_start = metadata.marker_offset;
                if (tag != MD_L0_PARAGRAPH) {
                    content_start += metadata.marker_length + metadata.marker_padding;
                    extra = metadata.post_indent;
                }
            }
#endif
        }
        // Compare facts retained by all schemas, not discarded marker padding columns.
        if (tag != MD_L0_BLANK && tag != MD_L0_EOF &&
            tag != MD_L0_BLOCK_QUOTE && tag != MD_L0_UNORDERED_LIST)
            extra = 0;
        hash = (hash ^ pre_indent) * 1099511628211ULL;
        hash = (hash ^ content_start) * 1099511628211ULL;
        if (tag != MD_L0_PARAGRAPH)
            hash = (hash ^ extra) * 1099511628211ULL;
    }
    for (u32 i = 0; i + 1 < state->l0_count; ++i)
        if (state->l0_offsets[i] >= state->l0_offsets[i + 1] || state->l0_tags[i] == MD_L0_NONE) abort();
    for (u32 i = 0; i < state->l1_count; ++i) {
        if (!state->l1_lengths[i] || state->l1_offsets[i] > source->total_length ||
            state->l1_lengths[i] > source->total_length - state->l1_offsets[i]) abort();
        if (i && state->l1_offsets[i - 1] >= state->l1_offsets[i]) abort();
        hash = (hash ^ state->l1_offsets[i]) * 1099511628211ULL;
        hash = (hash ^ state->l1_lengths[i]) * 1099511628211ULL;
        hash = (hash ^ state->l1_tags[i]) * 1099511628211ULL;
    }
    for (u32 i = 0; i < state->mappings_count; ++i) {
        MdMap map = state->mappings[i];
        const u32 facts[] = {map.offset, map.length, map.repeat, map.kind, map.text_length};
        for (u32 j = 0; j < sizeof(facts) / sizeof(*facts); ++j)
            hash = (hash ^ facts[j]) * 1099511628211ULL;
        for (u32 j = 0; j < map.text_length; ++j)
            hash = (hash ^ map.text[j]) * 1099511628211ULL;
    }
    return hash;
}


static usize metadata_bytes(const BenchState* state) {
#if BENCH_THIN
    if (state->thin_metadata) {
        usize extras = 0;
        for (u32 i = 0; i < state->l0_count; ++i)
            if (state->l0_tags[i] == MD_L0_BLOCK_QUOTE || state->l0_tags[i] == MD_L0_UNORDERED_LIST)
                extras += 1;
        return state->l0_count * sizeof(MdL0CommonThin) + extras * sizeof(MdL0ExtraThin);
    }
#endif
    if (state->l0_common) {
#if BENCH_SPLIT
        usize extras = 0;
        for (u32 i = 0; i < state->l0_count; ++i)
            if (state->l0_tags[i] != MD_L0_PARAGRAPH) extras += 1;
        return state->l0_count * sizeof(MdL0Common) + extras * sizeof(MdL0Extra);
#endif
    }
#if BENCH_LEGACY
    return state->l0_count * sizeof(MdL0Metadata);
#else
    abort();
#endif
}

static unsigned long peak_rss_kib(void) {
    FILE* file = fopen("/proc/self/status", "r");
    if (!file) abort();
    char line[256];
    unsigned long kib = 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "VmHWM: %lu kB", &kib) == 1) break;
    }
    fclose(file);
    if (!kib) abort();
    return kib;
}

static const u8 candidates[] = {'\n', 0, '\t', '\r', ' ', '#', '>', '-', '+', '=', '`', '*', '_', '~', '|', '[', ']', '!', '<', '\\', '&'};

static void check_scan(const u8* bytes, u32 length, u32 piece) {
    SimdByteScanMany table = simd_byte_scan_many_init(candidates, sizeof(candidates));
    SimdByteRunCarry carry = {0};
    u8 lookup[256] = {0}, tags[1024], drop[1024] = {0};
    for (u32 i = 0; i < sizeof(candidates); ++i) lookup[candidates[i]] = candidates[i];
    lookup[0] = 128;
    for (u32 i = 0; i < length; ++i) tags[i] = lookup[bytes[i]];
    for (u32 i = 1; i + 1 < length; ++i) {
        if (i % 32 && i % 32 != 31 && tags[i] == ' ' && tags[i - 1] == 0 && tags[i + 1] == 0) {
            drop[i] = 1;
            drop[i + 1] = 1;
        }
    }
    u32 expected[1024], expected_count = 0;
    for (u32 i = 0; i < length; ++i)
        if ((!i || tags[i] != tags[i - 1]) && !drop[i]) expected[expected_count++] = i;
    u32 seen = 0, offset = 0;
    do {
        u32 count = min(piece, length - offset);
        u8 out_tags[4097];
        u32 starts[4097];
        out_tags[count] = 0xa5;
        starts[count] = 0xdeadbeef;
        u32 written = simd_byte_scan_runs(bytes + offset, count, &table, &carry, out_tags, starts);
        if (out_tags[count] != 0xa5 || starts[count] != 0xdeadbeef) abort();
        for (u32 i = 0; i < written; ++i) {
            if (seen == expected_count || starts[i] + offset != expected[seen] ||
                out_tags[i] != tags[expected[seen]]) abort();
            seen += 1;
        }
        offset += count;
    } while (offset < length);
    if (seen != expected_count) abort();
}

typedef BenchState (*Create)(const DocView*, Arena*, ScratchArena*);
static const struct {const char* name; Create create;} variants[] = {
    {"baseline", create_baseline},
    {"candidate", create_candidate}
};
enum {COUNT = sizeof(variants) / sizeof(*variants), TRIALS = 11};

static void compare_all(const u8* bytes, u32 length, Arena* arena, ScratchArena* scratch) {
    DocPiece pieces[] = {{.bytes = (u8*)bytes, .length = length / 2}, {.bytes = (u8*)bytes},
                        {.bytes = (u8*)bytes + length / 2, .length = length - length / 2}};
    DocView source = {.pieces = pieces, .count = 3, .total_length = length};
    arena->len = scratch->len = 0;
    BenchState baseline = create_baseline(&source, arena, scratch);
    u64 expected = check(&baseline, &source);
    for (u32 v = 0; v < COUNT; ++v) {
        arena->len = scratch->len = 0;
        BenchState state = variants[v].create(&source, arena, scratch);
        if (state.l0_count != baseline.l0_count || state.l1_count != baseline.l1_count ||
            state.mappings_count != baseline.mappings_count || check(&state, &source) != expected) abort();
    }
}
static void self_check(void) {
    Arena arena = arena_create();
    ScratchArena scratch = scratch_create();
    static const struct { const char* text; u8 tag; } markers[] = {
        {"# heading", MD_L0_ATX_HEADING}, {"####### text", MD_L0_PARAGRAPH},
        {"- item", MD_L0_UNORDERED_LIST}, {"+ item", MD_L0_UNORDERED_LIST},
        {"***", MD_L0_THEMATIC_BREAK}, {"---", MD_L0_THEMATIC_BREAK},
        {"___", MD_L0_THEMATIC_BREAK}, {"__", MD_L0_PARAGRAPH},
        {"```", MD_L0_FENCED_CODE_BLOCK}, {"~~", MD_L0_PARAGRAPH},
        {"> quote", MD_L0_BLOCK_QUOTE}, {"= text", MD_L0_SETEXT_HEADING},
    };
    for (u32 i = 0; i < sizeof(markers) / sizeof(*markers); ++i) {
        DocPiece piece = {.bytes = (u8*)markers[i].text, .length = strlen(markers[i].text)};
        DocView source = {.pieces = &piece, .count = 1, .total_length = piece.length};
        for (u32 v = 0; v < COUNT; ++v) {
            arena.len = scratch.len = 0;
            BenchState state = variants[v].create(&source, &arena, &scratch);
            if (state.l0_tags[0] != markers[i].tag) abort();
        }
    }
    const char* cases[] = {"", "a b", " a b ", "a  b", "a\tb", "a b\n", "# a b", "a ",
                           "\n  a b  \n\n", "\t# a b \n", "a *b c* d e\n"};
    for (u32 i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        u32 length = strlen(cases[i]);
        check_scan((const u8*)cases[i], length, 32);
        compare_all((const u8*)cases[i], length, &arena, &scratch);
    }
    u8 bytes[1024];
    for (u32 length = 1; length <= 96; ++length) {
        for (u32 space = 0; space < length; ++space) {
            memset(bytes, 'a', length);
            bytes[space] = ' ';
            check_scan(bytes, length, 32);
            check_scan(bytes, length, 64);
            compare_all(bytes, length, &arena, &scratch);
        }
    }
    u32 random = 12345;
    static const u8 alphabet[] = "abc   \t\n#>*-+_=~`|[]!<\\&\0\x80\xc3\xa9";
    for (u32 trial = 0; trial < 1000; ++trial) {
        u32 length = trial % sizeof(bytes);
        for (u32 i = 0; i < length; ++i) {
            random = random * 1664525u + 1013904223u;
            bytes[i] = alphabet[(random >> 16) % (sizeof(alphabet) - 1)];
        }
        check_scan(bytes, length, 32);
        check_scan(bytes, length, 4096);
        compare_all(bytes, length, &arena, &scratch);
    }
    u8 long_bytes[12345];
    for (u32 i = 0; i < sizeof(long_bytes); ++i) {
        long_bytes[i] = 'a';
        if (i % 7 == 0) long_bytes[i] = ' ';
        if (i % 97 == 0) long_bytes[i] = '\n';
        if (i % 233 == 0) long_bytes[i] = '#';
    }
    long_bytes[sizeof(long_bytes) - 1] = ' ';
    compare_all(long_bytes, sizeof(long_bytes), &arena, &scratch);
    arena_destroy(&arena);
    scratch_destroy(&scratch);
    puts("PASS: scalar scanner oracle, exact capacities, all lane/tail space positions, refills, fragmented parser comparison");
}



static void check_body_whitespace(void) {
    Arena arena = arena_create();
    ScratchArena scratch = scratch_create();
    static const u8 mixed[] = "a*b  \t \n  -\ttext\na*b \t\0 \nx  \t";
    compare_all(mixed, sizeof(mixed) - 1, &arena, &scratch);
    u8 bytes[12064];
    memcpy(bytes, "a*b", 3);
    memset(bytes + 3, ' ', 12000);
    static const char suffix[] = "c\t \t\n  -\ttext\na*b \t";
    memcpy(bytes + 12003, suffix, sizeof(suffix) - 1);
    compare_all(bytes, 12003 + sizeof(suffix) - 1, &arena, &scratch);
    for (u32 i = 0; i < 12000; ++i) {
        bytes[3 + i] = ' ';
        if (i & 1) bytes[3 + i] = '\t';
    }
    compare_all(bytes, 12003 + sizeof(suffix) - 1, &arena, &scratch);
    arena_destroy(&arena);
    scratch_destroy(&scratch);
}

int main(int argc, char** argv) {
    if (argc == 1) {
        self_check();
        check_body_whitespace();
        puts("PASS: body whitespace, trailing LF/EOF, NUL, and 12 KB mixed whitespace refills");
        return 0;
    }
    FILE* file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END)) abort();
    long file_length = ftell(file);
    if (file_length <= 0 || file_length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) abort();
    u32 length = file_length;
    u8* bytes = malloc(length);
    if (!bytes || fread(bytes, 1, length, file) != length) abort();
    fclose(file);
    u32 piece_bytes = length;
    if (argc > 2) piece_bytes = strtoul(argv[2], 0, 10);
    if (!piece_bytes) piece_bytes = length;
    piece_bytes = min(piece_bytes, length);
    u32 piece_count = (length + piece_bytes - 1) / piece_bytes;
    DocPiece* pieces = calloc(piece_count, sizeof(*pieces));
    if (!pieces) abort();
    for (u32 i = 0; i < piece_count; ++i)
        pieces[i] = (DocPiece){.bytes = bytes + i * piece_bytes, .length = min(piece_bytes, length - i * piece_bytes)};
    DocView source = {.pieces = pieces, .count = piece_count, .total_length = length};
    u32 selected = COUNT;
    if (argc > 3) {
        for (u32 v = 0; v < COUNT; ++v)
            if (!strcmp(argv[3], variants[v].name)) selected = v;
        if (selected == COUNT) abort();
    }
    Arena arena = arena_create();
    ScratchArena scratch = scratch_create();
    if (argc > 4 && !strcmp(argv[4], "memory")) {
        if (selected == COUNT) abort();
        unsigned long initial_rss = peak_rss_kib();
        BenchState state = {0};
        u64 fingerprint = 0;
        for (u32 i = 0; i < 3; ++i) {
            arena.len = scratch.len = 0;
            state = variants[selected].create(&source, &arena, &scratch);
            fingerprint = check(&state, &source);
        }
        printf("memory %s %zu %zu %zu %lu %lu %llu\n", variants[selected].name,
               scratch.len, arena.len, metadata_bytes(&state), peak_rss_kib(), initial_rss,
               (unsigned long long)fingerprint);
        free(pieces); free(bytes);
        arena_destroy(&arena); scratch_destroy(&scratch);
        return 0;
    }
    BenchState baseline = create_baseline(&source, &arena, &scratch);
    u64 fingerprint = check(&baseline, &source);
    for (u32 v = 0; v < COUNT; ++v) {
        if (selected != COUNT && v != selected) continue;
        arena.len = scratch.len = 0;
        BenchState state = variants[v].create(&source, &arena, &scratch);
        if (check(&state, &source) != fingerprint || state.l0_count != baseline.l0_count ||
            state.l1_count != baseline.l1_count || state.mappings_count != baseline.mappings_count) abort();
        printf("stats %s %u %u %u %u %zu %zu %llu\n", variants[v].name, length,
               state.l0_count, state.l1_count, state.mappings_count, scratch.len, arena.len,
               (unsigned long long)fingerprint);
    }
    double wall[COUNT][TRIALS] = {0}, cpu[COUNT][TRIALS] = {0};
    u32 calls = max(1u, (64u * 1024 * 1024) / length);
    if (argc > 4) calls = strtoul(argv[4], 0, 10);
    if (!calls) abort();
    for (u32 trial = 0; trial < TRIALS; ++trial) {
        for (u32 k = 0; k < COUNT; ++k) {
            u32 v = (k + trial) % COUNT;
            if (selected != COUNT && v != selected) continue;
            arena.len = scratch.len = 0;
            BenchState state = variants[v].create(&source, &arena, &scratch);
            double wall_begin = now(CLOCK_MONOTONIC_RAW);
            double cpu_begin = now(CLOCK_THREAD_CPUTIME_ID);
            for (u32 i = 0; i < calls; ++i) {
                arena.len = scratch.len = 0;
                state = variants[v].create(&source, &arena, &scratch);
                sink += state.l0_count + state.l1_count + state.mappings_count + state.l0_offsets[state.l0_count - 1];
            }
            cpu[v][trial] = now(CLOCK_THREAD_CPUTIME_ID) - cpu_begin;
            wall[v][trial] = now(CLOCK_MONOTONIC_RAW) - wall_begin;
            if ((trial == 0 || trial + 1 == TRIALS) && check(&state, &source) != fingerprint) abort();
        }
    }
    for (u32 v = 0; v < COUNT; ++v) {
        if (selected != COUNT && v != selected) continue;
        for (u32 trial = 0; trial < TRIALS; ++trial)
            printf("trial %s %u %u %.9f %.9f\n", variants[v].name, trial, calls, wall[v][trial], cpu[v][trial]);
    }
    free(pieces);
    free(bytes);
    arena_destroy(&arena);
    scratch_destroy(&scratch);
    return 0;
}

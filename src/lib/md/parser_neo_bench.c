/* Optimized end-to-end benchmark. No changes/instrumentation in either parser.
 * Usage: binary workload KiB pieces edit rounds [both|neo|fork]
 * Workloads: prose, list, paragraph, code, references, shared, design
 * Edits: middle, start, end, definition, insert, fence
 * Timings exclude source mutation, piece construction, resets and validation.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <linux/mman.h>
#include <unistd.h>
#define LOGGER_H
#define CRITICAL_LOG(...) ((void)0)
#define WARNING_LOG(...) ((void)0)
#define DEBUG_LOG(...) ((void)0)
#include "parser_neo.h"

int fork_bench_parse(const char*, uint32_t, uint32_t, uint64_t*, uint64_t*);
uint64_t fork_bench_checksum(void);
void fork_bench_reset(void);

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "benchmark failure line %d: %s\n", __LINE__, #x); exit(2); } } while (0)
static uint64_t clock_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static int compare_time(const void* a, const void* b) {
    uint64_t left = *(const uint64_t*)a, right = *(const uint64_t*)b;
    return (left > right) - (left < right);
}
static double percentile(uint64_t* times, u32 count, u32 percent) {
    qsort(times, count, sizeof(*times), compare_time);
    return times[(u64)(count - 1) * percent / 100] / 1000.0;
}
static u32 make_document(char* text, u32 target, const char* shape) {
    u32 used = 0, sequence = 0;
    if (!strcmp(shape, "design")) {
        FILE* input = fopen("md_design.md", "rb");
        REQUIRE(input);
        used = fread(text, 1, target, input);
        REQUIRE(feof(input) && !ferror(input));
        fclose(input);
        text[used] = 0;
        return used;
    }
    if (!strcmp(shape, "code")) {
        memcpy(text, "```c\n", 5);
        used = 5;
    }
    while (used < target) {
        const char* part;
        char definition[256];
        if (!strcmp(shape, "prose"))
            part = "## Section\n\nalpha **bold** and *italic*, [link](https://example.org), `code` &amp; text.\nAnother line of ordinary text.\n\n";
        else if (!strcmp(shape, "list"))
            part = "- alpha **bold** and *italic*, [link](https://example.org), `code` &amp; text.\n";
        else if (!strcmp(shape, "paragraph"))
            part = "alpha **bold** and *italic*, [link](https://example.org), `code` &amp; text. ";
        else if (!strcmp(shape, "code"))
            part = "int alpha = 123; /* ordinary fenced code, no Markdown interpretation */\n";
        else if (!strcmp(shape, "references")) {
            snprintf(definition, sizeof(definition), "alpha [label][id%06u] and **bold**.\n\n[id%06u]: /target%06u\n\n", sequence, sequence, sequence);
            part = definition;
        } else if (!strcmp(shape, "shared"))
            part = "alpha [label][shared] and **bold**, with ordinary surrounding text.\n\n";
        else {
            fprintf(stderr, "unknown workload: %s\n", shape);
            exit(2);
        }
        u32 n = (u32)strlen(part);
        memcpy(text + used, part, n);
        used += n;
        ++sequence;
    }
    if (!strcmp(shape, "code")) {
        memcpy(text + used, "```\n", 4);
        used += 4;
    }
    if (!strcmp(shape, "shared")) {
        const char* definition = "[shared]: /target\n";
        memcpy(text + used, definition, strlen(definition));
        used += strlen(definition);
    }
    text[used] = 0;
    return used;
}
static MdView split_document(char* text, u32 length, MdPiece* pieces, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        u32 begin = (u64)length * i / count, end = (u64)length * (i + 1) / count;
        pieces[i] = (MdPiece){(u8*)text + begin, end - begin};
    }
    return (MdView){pieces, count};
}

int main(int argc, char** argv) {
    REQUIRE(argc >= 6);
    const char* shape = argv[1];
    u32 target = (u32)strtoul(argv[2], NULL, 10) * 1024;
    u32 piece_count = (u32)strtoul(argv[3], NULL, 10);
    const char* edit = argv[4];
    u32 rounds = (u32)strtoul(argv[5], NULL, 10);
    const char* mode = argc > 6 ? argv[6] : "both";
    bool run_neo = strcmp(mode, "fork") != 0, run_fork = strcmp(mode, "neo") != 0;
    REQUIRE(target && target <= 16 * 1024 * 1024 && piece_count && rounds >= 3);
    char* text = malloc((usize)target + 4096);
    MdPiece* pieces = malloc((usize)piece_count * sizeof(*pieces));
    uint64_t* neo_times = calloc(rounds, sizeof(uint64_t));
    uint64_t* fork_times = calloc(rounds, sizeof(uint64_t));
    uint64_t* query_times = calloc(rounds, sizeof(uint64_t));
    REQUIRE(text && pieces && neo_times && fork_times && query_times);
    u32 length = make_document(text, target, shape);
    u32 desired = !strcmp(edit, "start") ? 0 : !strcmp(edit, "end") ? length - 256 : length / 2;
    char* position = !strcmp(shape, "design") ? strchr(text + desired, 'a') : strstr(text + desired, "alpha");
    if (!strcmp(edit, "definition")) {
        REQUIRE(!strcmp(shape, "shared"));
        position = strstr(text, "/target") + 1;
    }
    if (!strcmp(edit, "fence")) {
        REQUIRE(!strcmp(shape, "code"));
        position = text + length - 4;
    }
    REQUIRE(position);
    u32 offset = (u32)(position - text);
    MdView view = split_document(text, length, pieces, piece_count);
    const u32 flags = MD_DEFAULT_FLAGS | MD_DIALECT_GITHUB;
    /* Real project allocators, with their normal backing size. Huge pages are
     * disabled for reproducibility and to avoid depending on host reservation. */
    huge_pages_supported = false;
    Arena durable = arena_create(), output = arena_create();
    ScratchArena scratch = scratch_create();
    MdState state = {0};
    uint64_t cold_neo[3] = {0}, cold_fork[3] = {0}, fork_peak = 0, events = 0;
    for (u32 i = 0; i < 3; ++i) {
        if (run_neo) {
            md_destroy(&state);
            durable.len = scratch.len = 0;
            uint64_t start = clock_ns();
            state = md_create(&view, &durable, &scratch, flags);
            cold_neo[i] = clock_ns() - start;
            if (state.error) {
                fprintf(stderr, "create error=%u shape=%s bytes=%u scratch=%zu durable=%zu\n", state.error, shape, length, scratch.len, durable.len);
                return 2;
            }
        }
        if (run_fork) {
            fork_bench_reset();
            uint64_t start = clock_ns();
            int error = fork_bench_parse(text, length, flags, &fork_peak, &events);
            cold_fork[i] = clock_ns() - start;
            REQUIRE(!error);
        }
    }
    usize create_scratch = scratch.len, update_scratch = 0, query_scratch = 0;
    u64 checksum = 0;
    u32 nodes = 0;
    bool inserted = false;
    for (u32 i = 0; i < rounds + 5; ++i) {
        u32 added = 1, removed = 1;
        if (!strcmp(edit, "insert")) {
            if (!inserted) {
                memmove(text + offset + 1, text + offset, length - offset + 1);
                text[offset] = 'z';
                ++length;
                removed = 0;
            } else {
                memmove(text + offset, text + offset + 1, length - offset);
                --length;
                added = 0;
            }
            inserted = !inserted;
        } else if (!strcmp(edit, "fence")) {
            text[offset] = text[offset] == '`' ? 'x' : '`';
        } else {
            text[offset] = text[offset] == 'a' ? 'A' : 'a';
        }
        view = split_document(text, length, pieces, piece_count);
        /* Alternate execution order to reduce bias from transient CPU state. */
        for (u32 j = 0; j < 2; ++j) {
            if (((i + j) & 1) == 0 && run_neo) {
                scratch.len = 0;
                uint64_t start = clock_ns();
                md_update(&state, &scratch, &view, added, offset, removed);
                uint64_t elapsed = clock_ns() - start;
                if (state.error) {
                    fprintf(stderr, "update error=%u iteration=%u shape=%s scratch=%zu durable=%zu\n", state.error, i, shape, scratch.len, durable.len);
                    return 2;
                }
                if (i >= 5) neo_times[i - 5] = elapsed;
                if (scratch.len > update_scratch) update_scratch = scratch.len;
            } else if (((i + j) & 1) == 1 && run_fork) {
                uint64_t start = clock_ns();
                int error = fork_bench_parse(text, length, flags, &fork_peak, &events);
                uint64_t elapsed = clock_ns() - start;
                REQUIRE(!error);
                if (i >= 5) fork_times[i - 5] = elapsed;
            }
        }
        if (run_neo) {
            scratch.len = output.len = 0;
            u32 begin = offset > 1024 ? offset - 1024 : 0;
            u32 end = min(offset + 1024, length);
            uint64_t start = clock_ns();
            MdResult result = md_get(&state, &output, &scratch, begin, end);
            uint64_t elapsed = clock_ns() - start;
            REQUIRE(!state.error);
            nodes = result.count;
            for (u32 n = 0; n < result.count; ++n)
                checksum += result.nodes[n].kind + result.nodes[n].start_idx + result.nodes[n].end_idx;
            if (i >= 5) query_times[i - 5] = elapsed;
            if (scratch.len > query_scratch) query_scratch = scratch.len;
        }
    }
    /* Validate final incremental output against fresh construction, outside
     * every timing. This is a consistency check, not a grammar oracle. */
    if (run_neo) {
        output.len = scratch.len = 0;
        MdResult incremental = md_get(&state, &output, &scratch, 0, length);
        REQUIRE(!state.error);
        Arena fresh_arena = arena_create();
        scratch.len = 0;
        MdState fresh = md_create(&view, &fresh_arena, &scratch, flags);
        REQUIRE(!fresh.error);
        MdResult rebuilt = md_get(&fresh, &output, &scratch, 0, length);
        REQUIRE(!fresh.error && rebuilt.count == incremental.count);
        REQUIRE(!incremental.count || !memcmp(rebuilt.nodes, incremental.nodes, (usize)incremental.count * sizeof(MdNode)));
        md_destroy(&fresh);
        arena_destroy(&fresh_arena);
    }
    double neo_us = percentile(neo_times, rounds, 50), fork_us = percentile(fork_times, rounds, 50);
    printf("%s,%u,%u,%s,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu,%zu,%llu,%u,%u,%llu\n",
           shape, length, piece_count, edit, rounds,
           percentile(cold_neo, 3, 50), percentile(cold_fork, 3, 50),
           neo_us, percentile(neo_times, rounds, 95), fork_us, percentile(fork_times, rounds, 95),
           neo_us ? fork_us / neo_us : 0, percentile(query_times, rounds, 50), percentile(query_times, rounds, 95),
           durable.len, create_scratch, update_scratch, query_scratch,
           (unsigned long long)fork_peak, state.blocks_count, nodes,
           (unsigned long long)(checksum + fork_bench_checksum()));
    md_destroy(&state);
    fork_bench_reset();
    arena_destroy(&durable);
    arena_destroy(&output);
    scratch_destroy(&scratch);
    free(text); free(pieces); free(neo_times); free(fork_times); free(query_times);
    return 0;
}

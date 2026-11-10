#!/usr/bin/env python3
"""Compile the complete scratch parser and check the migrated md_get traversal."""
from pathlib import Path
import tempfile
from compare import ROOT, run

out = Path(tempfile.mkdtemp(prefix="md-check-get-"))
(out / "parser.h").write_bytes((ROOT / "src/lib/md/md_parser_scratch.h").read_bytes())
(out / "assert.h").write_text('''#if defined(STATIC_ASSERT)
#pragma push_macro("assert")
#include_next <assert.h>
#pragma pop_macro("assert")
#else
#include_next <assert.h>
#endif
''')
(out / "check.c").write_text(r'''
#include <lib/grim/logger.h>
#include "parser.h"
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); abort(); } } while (0)

// Scalar source-based oracle for the existing provisional block behavior.
// Fence closure, containers and inline interpretation are deliberately still TODOs.
static u32 reference(const MdState* state, const u8* bytes, u32 begin, u32 end, MdNode* out) {
    u32 count = 0;
    for (u32 i = 0; i + 1 < state->l0_count; ++i) {
        u32 start = state->l0_offsets[i], stop = state->l0_offsets[i + 1];
        u32 pos = start, col = 0;
        while (pos < stop) {
            if (bytes[pos] == ' ') col += 1;
            else if (bytes[pos] == '\t') col = (col + 4) & ~3u;
            else break;
            pos += 1;
        }
        u32 marker = pos;
        while (pos < stop && bytes[pos] == bytes[marker]) pos += 1;
        u32 width = pos - marker;
        if (state->l0_tags[i] == MD_L0_FENCED_CODE_BLOCK && col <= 3) break;
        if (stop <= begin || start >= end) continue;
        MdNode node = {.start_idx = start, .end_idx = stop, .kind = MD_NODE_PARAGRAPH,
                       .content_length = stop - start};
        switch (state->l0_tags[i]) {
            case MD_L0_ATX_HEADING:
                if (col <= 3) {
                    while (pos < stop && (bytes[pos] == ' ' || bytes[pos] == '\t')) pos += 1;
                    node.kind = MD_NODE_HEADING;
                    node.content_offset = pos - start;
                    node.content_length = stop - pos;
                    node.data.heading_level = width;
                }
                break;
            case MD_L0_BLOCK_QUOTE:
                node.kind = MD_NODE_QUOTE; node.data.quote_depth = width; break;
            case MD_L0_THEMATIC_BREAK:
                if (col <= 3) node.kind = MD_NODE_THEMATIC_BREAK;
                break;
            case MD_L0_BLANK:
                node.kind = MD_NODE_BLANK;
                for (u32 j = start; j < stop; ++j)
                    if (bytes[j] == '\n') node.data.line_count += 1;
                break;
        }
        out[count++] = node;
        for (u32 j = start; j < stop;) {
            if (bytes[j] != 0) { j += 1; continue; }
            u32 first = j;
            while (j < stop && bytes[j] == 0) j += 1;
            if (j <= begin || first >= end) continue;
            MdNode replacement = {.start_idx = first, .end_idx = j, .kind = MD_NODE_REPLACEMENT,
                                  .content_length = j - first};
            memcpy(replacement.data.replacement.text, "\xef\xbf\xbd", 3);
            replacement.data.replacement.length = 3;
            replacement.data.replacement.repeat = j - first;
            out[count++] = replacement;
        }
    }
    return count;
}

static void query(MdState* state, const u8* bytes, u32 begin, u32 end, Arena* output, ScratchArena* scratch) {
    output->len = scratch->len = 0;
    MdNode expected[2048];
    u32 count = reference(state, bytes, begin, end, expected);
    MdResult result = md_get(state, output, scratch, begin, end);
    CHECK(result.count == count);
    for (u32 i = 0; i < count; ++i) {
        const MdNode* a = &result.nodes[i];
        const MdNode* b = &expected[i];
        CHECK(a->kind == b->kind && a->start_idx == b->start_idx && a->end_idx == b->end_idx);
        CHECK(a->content_offset == b->content_offset && a->content_length == b->content_length);
        if (a->kind == MD_NODE_HEADING) CHECK(a->data.heading_level == b->data.heading_level);
        if (a->kind == MD_NODE_QUOTE) CHECK(a->data.quote_depth == b->data.quote_depth);
        if (a->kind == MD_NODE_BLANK) CHECK(a->data.line_count == b->data.line_count);
        if (a->kind == MD_NODE_REPLACEMENT) {
            CHECK(a->data.replacement.length == 3 && a->data.replacement.repeat == b->data.replacement.repeat);
            CHECK(memcmp(a->data.replacement.text, b->data.replacement.text, 3) == 0);
        }
    }
}

static void check(const u8* bytes, u32 length, Arena* index, Arena* output, ScratchArena* scratch) {
    index->len = scratch->len = 0;
    DocPiece piece = {.bytes = (u8*)bytes, .length = length};
    DocView source = {.pieces = &piece, .count = 1, .total_length = length};
    MdState state = md_create(&source, index, scratch);

    // Put the exact tag array at a guard-page boundary to catch tail overreads.
    usize page = (usize)sysconf(_SC_PAGESIZE);
    CHECK(state.l0_count < page);
    u8* allocation = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(allocation != MAP_FAILED && mprotect(allocation + page, page, PROT_NONE) == 0);
    u8* tags = allocation + page - state.l0_count;
    memcpy(tags, state.l0_tags, state.l0_count);
    state.l0_tags = tags;

    query(&state, bytes, 0, length + 1, output, scratch);
    for (u32 begin = 0; begin < length; begin += 17)
        query(&state, bytes, begin, min(length, begin + 89), output, scratch);
    // Exercise phase handoff exactly at, and just before, every L0 boundary.
    for (u32 i = 0; i < state.l0_count; ++i) {
        u32 boundary = state.l0_offsets[i];
        query(&state, bytes, boundary, boundary + 1, output, scratch);
        if (boundary) query(&state, bytes, boundary - 1, boundary + 1, output, scratch);
    }
    // Each one-byte window includes starts inside NUL runs and block boundaries.
    if (length < 80)
        for (u32 begin = 0; begin < length; ++begin)
            query(&state, bytes, begin, begin + 1, output, scratch);
    CHECK(munmap(allocation, page * 2) == 0);
}

int main(void) {
    Arena index = arena_create(), output = arena_create();
    ScratchArena scratch = scratch_create();
    check((const u8*)"", 0, &index, &output, &scratch);
    const u8 nul[] = {'a', 0, 0, 0, 'b', '\n', '#', ' ', 'h', '\n', 0, 0, '\n'};
    check(nul, sizeof(nul), &index, &output, &scratch);

    u8 bytes[8192];
    u32 length = 0;
    const char* lines[] = {"p *inline* & text\n", "# heading\n", "p\n\n\n", ">> quote\n", "***\n",
                           "  ##\theading\n", "    # deep\n", "- item\n", "= provisional\n", "`inline`\n"};
    for (u32 i = 0; i < 160; ++i) {
        const char* line = lines[i % 10];
        u32 n = strlen(line);
        memcpy(bytes + length, line, n); length += n;
    }
    check(bytes, length, &index, &output, &scratch);
    for (u32 n = 0; n <= 130; ++n) {
        length = 0;
        for (u32 i = 0; i < n; ++i) { memcpy(bytes + length, "p\n", 2); length += 2; }
        const char* suffix = "# h\n\n\nlast\n";
        memcpy(bytes + length, suffix, strlen(suffix)); length += strlen(suffix);
        check(bytes, length, &index, &output, &scratch);

        // Switch masks in the middle of full/tail blocks; closing is still a TODO.
        length = n * 2;
        const char* fence = "````\n# body\n~~~\n```\n````\n";
        memcpy(bytes + length, fence, strlen(fence)); length += strlen(fence);
        check(bytes, length, &index, &output, &scratch);
    }
    arena_destroy(&index); arena_destroy(&output); scratch_destroy(&scratch);
    puts("PASS: complete parser build; scalar getter oracle, sparse Extra, tail guards, fence masks, L1 seeks and NUL ranges/repeats");
}
''')
common = ["-g", "-march=x86-64-v3", "-I" + str(out), "-I" + str(ROOT / "src"),
          "-DWAYLAND", str(out / "check.c"), "-lwayland-client", "-lvulkan", "-lm"]
for name, flags in [("clang", ["-O3"]), ("gcc", ["-O3"]),
                    ("clang-asan", ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"])]:
    compiler = name.split("-")[0]
    run([compiler, *flags, *common, "-o", out / name], out / (name + "-build.txt"))
    print(name + ": " + run([out / name], out / (name + "-check.txt")).strip(), flush=True)
print("Sources and logs:", out)

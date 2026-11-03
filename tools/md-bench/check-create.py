#!/usr/bin/env python3
"""Check md_create in isolation while md_get is being migrated. Optional arguments: input files."""
import argparse
from pathlib import Path
import tempfile

from compare import ROOT, function, run

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("inputs", nargs="*", type=Path)
parser.add_argument("--candidate", type=Path, default=ROOT / "src/lib/md/md_parser_scratch.h")
args = parser.parse_args()
out = Path(tempfile.mkdtemp(prefix="md-check-create-"))
source = args.candidate.read_text()
_, end = function(source)
(out / "create-only.h").write_text(source[:end] + "\n")
# Preserve the project's two-argument assert through repeated libc includes.
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
#include "create-only.h"
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); abort(); } } while (0)
#define HASH(v) hash = (hash ^ (u64)(v)) * 1099511628211ULL
static u8 punctuation[256];
static u32 whitespace(const u8* bytes, u32 length, u32* pos, u32 col) {
    while (*pos < length) {
        if (bytes[*pos] == ' ') col += 1;
        else if (bytes[*pos] == '\t') col = (col + 4) & ~3u;
        else break;
        *pos += 1;
    }
    return col;
}
static u64 validate(const MdState* s, const u8* bytes, u32 length) {
    CHECK(s->l0_count && s->l0_tags[s->l0_count - 1] == MD_L0_EOF);
    CHECK(s->l0_offsets[s->l0_count - 1] == length);
    u64 hash = 1469598103934665603ULL;
    u32 extra = 0;
    for (u32 i = 0; i < s->l0_count; ++i) {
        u8 tag = s->l0_tags[i];
        MdL0Common common = s->l0_common[i];
        u32 start = s->l0_offsets[i];
        HASH(tag); HASH(start); HASH(common.pre_indent); HASH(common.content_start);
        if (tag == MD_L0_EOF) break;
        u32 end = s->l0_offsets[i + 1];
        CHECK(start < end && end <= length);
        if (tag == MD_L0_BLANK) {
            u32 lines = 0;
            for (u32 j = start; j < end; ++j) {
                CHECK(bytes[j] == ' ' || bytes[j] == '\t' || bytes[j] == '\n');
                if (bytes[j] == '\n') lines += 1;
            }
            CHECK(common.pre_indent == 0 && common.content_start == 0);
            CHECK(s->l0_extra[extra].line_count == lines);
            HASH(lines); extra += 1;
            continue;
        }
        u32 pos = start;
        u32 col = whitespace(bytes, end, &pos, 0);
        CHECK(common.pre_indent == col && pos < end);
        u8 byte = bytes[pos];
        u32 marker_end = pos + 1;
        while (marker_end < end && bytes[marker_end] == byte) marker_end += 1;
        u32 n = marker_end - pos;
        u8 expected = MD_L0_PARAGRAPH;
        switch (byte) {
            case '#': if (n <= 6) expected = MD_L0_ATX_HEADING; break;
            case '>': expected = MD_L0_BLOCK_QUOTE; break;
            case '=': expected = MD_L0_SETEXT_HEADING; break;
            case '+': expected = MD_L0_UNORDERED_LIST; break;
            case '-': case '*':
                expected = MD_L0_UNORDERED_LIST;
                if (n >= 3) expected = MD_L0_THEMATIC_BREAK;
                break;
            case '_': if (n >= 3) expected = MD_L0_THEMATIC_BREAK; break;
            case '`': case '~': if (n >= 3) expected = MD_L0_FENCED_CODE_BLOCK; break;
        }
        CHECK(tag == expected);
        if (tag != MD_L0_PARAGRAPH) {
            col += n;
            pos = marker_end;
            col = whitespace(bytes, end, &pos, col);
            CHECK(s->l0_extra[extra].post_indent == col);
            HASH(col); extra += 1;
        }
        CHECK(common.content_start == pos - start);
    }
    u32 tail = length;
    while (tail && (bytes[tail - 1] == ' ' || bytes[tail - 1] == '\t')) tail -= 1;
    u32 tail_col = 0;
    if (!tail || bytes[tail - 1] == '\n') {
        u32 pos = tail;
        tail_col = whitespace(bytes, length, &pos, 0);
    }
    u32 blank = length;
    while (blank && (bytes[blank - 1] == ' ' || bytes[blank - 1] == '\t' || bytes[blank - 1] == '\n')) blank -= 1;
    u32 lines = 0;
    for (u32 j = blank; j < length; ++j) if (bytes[j] == '\n') lines += 1;
    if (blank && lines) lines -= 1;
    CHECK(s->l0_common[s->l0_count - 1].pre_indent == tail_col);
    CHECK(s->l0_common[s->l0_count - 1].content_start == 0);
    CHECK(s->l0_extra[extra].line_count == lines);
    HASH(lines);
    u32 l1 = 0, maps = 0;
    for (u32 i = 0; i < length;) {
        u8 byte = bytes[i];
        if (byte && !punctuation[byte]) { i += 1; continue; }
        u32 end = i + 1;
        while (end < length && bytes[end] == byte) end += 1;
        if (byte) {
            CHECK(l1 < s->l1_count && s->l1_tags[l1] == byte);
            CHECK(s->l1_offsets[l1] == i && s->l1_lengths[l1] == end - i);
            HASH(byte); HASH(i); HASH(end - i); l1 += 1;
        } else {
            CHECK(maps < s->mappings_count);
            const MdMap* m = &s->mappings[maps++];
            CHECK(m->offset == i && m->length == end - i && m->repeat == end - i);
            CHECK(m->kind == MD_NODE_REPLACEMENT && m->text_length == 3);
            CHECK(m->text[0] == 0xef && m->text[1] == 0xbf && m->text[2] == 0xbd);
            HASH(i); HASH(end - i);
        }
        i = end;
    }
    CHECK(l1 == s->l1_count && maps == s->mappings_count);
    return hash;
}
static void check(const u8* bytes, u32 length, Arena* arena, ScratchArena* scratch) {
    const u32 steps[] = {1, 31, 64, 4095, 4097};
    DocPiece one = {.bytes = (u8*)bytes, .length = length};
    DocView view = {.pieces = &one, .count = 1, .total_length = length};
    arena->len = scratch->len = 0;
    MdState state = md_create(&view, arena, scratch);
    u64 expected = validate(&state, bytes, length);
    for (u32 k = 0; k < sizeof(steps) / sizeof(*steps); ++k) {
        u32 step = steps[k];
        if (length > 65536 && step < 64) continue;
        DocPiece* pieces = calloc(2 + 2 * ((length + step - 1) / step), sizeof(*pieces));
        CHECK(pieces);
        u32 count = 1;
        pieces[0].bytes = (u8*)bytes;
        for (u32 pos = 0; pos < length;) {
            u32 n = min(step, length - pos);
            pieces[count++] = (DocPiece){.bytes = (u8*)bytes + pos, .length = n};
            pos += n;
            pieces[count++] = (DocPiece){.bytes = (u8*)bytes + pos};
        }
        view.pieces = pieces; view.count = count;
        arena->len = scratch->len = 0;
        state = md_create(&view, arena, scratch);
        CHECK(validate(&state, bytes, length) == expected);
        free(pieces);
    }
}
int main(int argc, char** argv) {
    // Direct windows, cross-piece batching, and the final partial SIMD group.
    u8 input[130];
    for (u32 i = 0; i < sizeof(input); ++i) input[i] = (u8)i;
    DocPiece fragments[] = {{0}, {.bytes = input, .length = 65}, {0},
                            {.bytes = input + 65, .length = 65}, {0}};
    DocView view = {.pieces = fragments, .count = 5, .total_length = sizeof(input)};
    MdCursor cursor = {.source = &view};
    const u8* window = NULL;
    u8 large_bridge[64];
    const u32 batched[] = {64, 64, 2};
    u32 offset = 0;
    for (u32 i = 0; i < 3; ++i) {
        u32 n = md_fill(&cursor, large_bridge, min(64, sizeof(input) - offset), &window);
        CHECK(n == batched[i] && memcmp(window, input + offset, n) == 0);
        if (i == 1) CHECK(window == large_bridge);
        else CHECK(window == input + offset);
        offset += n;
    }
    CHECK(md_fill(&cursor, large_bridge, 64, &window) == 0);
    DocView empty = {0};
    cursor = (MdCursor){.source = &empty};
    CHECK(md_fill(&cursor, large_bridge, 64, &window) == 0);
    const char* marks = "#>-+=`*_~|[]!<\\&";
    for (u32 i = 0; marks[i]; ++i) punctuation[(u8)marks[i]] = 1;
    Arena arena = arena_create(); ScratchArena scratch = scratch_create();
    const char* cases[] = {"", " \t", "\n\n", "p\n\n  ", "p\n \n\n\t", "a b \t\n",
        "\ttext\n# heading\nparagraph\n- item\n\n> quote\n", "####### text\n__\n~~\n",
        "    - x\n    -\tx\n", " \t###\ttext\n", "***\n___\n+ x\n= x\n~~~\n"};
    for (u32 i = 0; i < sizeof(cases) / sizeof(*cases); ++i)
        check((const u8*)cases[i], strlen(cases[i]), &arena, &scratch);
    u8 bytes[40000];
    memset(bytes, ' ', 12345); memcpy(bytes + 12345, "#\tx\n", 4);
    check(bytes, 12349, &arena, &scratch);
    memset(bytes, '`', 33000); bytes[33000] = '\n';
    check(bytes, 33001, &arena, &scratch);
    memset(bytes, 0, 12064); check(bytes, 12064, &arena, &scratch);
    for (u32 i = 0; i < 511; ++i) {
        bytes[i] = 0;
        if (i & 1) bytes[i] = 'x';
    }
    check(bytes, 511, &arena, &scratch); // Maximum NUL-run count: ceil(length / 2).
    memcpy(bytes, "p*x", 3); memset(bytes + 3, ' ', 12000); bytes[12003] = '\t';
    check(bytes, 12004, &arena, &scratch);
    u32 random = 12345;
    static const u8 alphabet[] = "abc  \t\n#>*-+_=~`|[]!<\\&\0\x80\xc3\xa9";
    for (u32 n = 0; n < 512; ++n) {
        for (u32 i = 0; i < n; ++i) {
            random = random * 1664525u + 1013904223u;
            bytes[i] = alphabet[(random >> 16) % (sizeof(alphabet) - 1)];
        }
        check(bytes, n, &arena, &scratch);
    }
    for (int i = 1; i < argc; ++i) {
        FILE* file = fopen(argv[i], "rb"); CHECK(file);
        CHECK(fseek(file, 0, SEEK_END) == 0);
        long n = ftell(file); CHECK(n >= 0 && n <= 16 * 1024 * 1024);
        rewind(file);
        u8* data = malloc((usize)n + 1); CHECK(data);
        CHECK(fread(data, 1, n, file) == (usize)n); fclose(file);
        check(data, n, &arena, &scratch); free(data);
    }
    arena_destroy(&arena); scratch_destroy(&scratch);
    puts("PASS: source oracle for Common/sparse Extra, candidate tags, EOF, raw L1/NUL maps, refills and fragmented/empty pieces");
}
''')
common = ["-g", "-march=x86-64-v3", "-I" + str(out), "-I" + str(ROOT / "src"),
          "-DWAYLAND", str(out / "check.c"), "-lwayland-client", "-lvulkan", "-lm"]
for name, flags in [("clang", ["-O3"]), ("gcc", ["-O3"]),
                    ("clang-asan", ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"])]:
    compiler = name.split("-")[0]
    run([compiler, *flags, *common, "-o", out / name], out / (name + "-build.txt"))
    inputs = [str(p.resolve()) for p in args.inputs]
    print(name + ": " + run([out / name, *inputs], out / (name + "-check.txt")).strip(), flush=True)
print("Sources and logs:", out)

/* Tests use the real allocation macros with caller-owned backing memory.
 * Disable only logger integration, whose transitive include order is unrelated
 * to the parser. No allocator substitutions or parser-generation scripts. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/mman.h>
#include <unistd.h>
#define LOGGER_H
#define CRITICAL_LOG(...) ((void)0)
#define WARNING_LOG(...) ((void)0)
#define DEBUG_LOG(...) ((void)0)
#include "parser_neo.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        abort(); \
    } \
} while (0)

void md_neo_test_oracle(unsigned kind, const char* text, uint32_t length,
                        uint32_t flags, uint32_t parameter, uint32_t out[8]);

static void neo_recognize(unsigned kind, MdReader* reader, u32 flags, u32 parameter, u32 out[8]) {
    u32 end = 0, begin = 0, value = 0, killer = 0;
    bool success = false;
    memset(out, 0, 8 * sizeof(*out));
    switch (kind) {
        case 0:
            success = md_is_hr_line(reader, 0, &end, &killer);
            if (!success)
                out[1] = killer;
            break;
        case 1:
            success = md_is_atxheader_line(reader, flags, 0, &begin, &end, &value);
            break;
        case 2:
            success = md_is_setext_underline(reader, 0, &end, &value);
            break;
        case 3:
            success = md_is_table_underline(reader, 0, &end, &value);
            break;
        case 4:
            success = md_is_opening_code_fence(reader, 0, &end, &value);
            break;
        case 5:
            success = md_is_closing_code_fence(reader, 0, flags, parameter, &end);
            out[1] = end;
            break;
        case 6: {
            MdContainerFrame container = {0};
            success = md_is_container_mark(reader, flags, parameter, 0, &end, &container);
            if (success) {
                out[4] = container.marker;
                out[5] = container.start;
                out[6] = container.marker_indent;
                out[7] = container.content_indent;
            }
            break;
        }
        case 7:
            success = md_is_entity(reader, 0, reader->length, &end);
            break;
        case 8: {
            MdRange content = {0};
            success = md_is_link_destination(reader, 0, reader->length, &end, &content);
            begin = content.begin;
            value = content.end;
            break;
        }
        case 9: {
            MdLinkPrefix prefix = MD_LINK_PREFIX_NONE;
            success = md_is_autolink(reader, 0, reader->length, &end, &prefix);
            value = prefix;
            break;
        }
    }
    out[0] = success;
    if (success) {
        out[1] = end;
        out[2] = begin;
        out[3] = value;
    }
}

static unsigned long comparisons;

static void compare(unsigned kind, const char* text, u32 length, u32 flags, u32 parameter) {
    u32 expected[8];
    md_neo_test_oracle(kind, text, length, flags, parameter, expected);
    for (u32 split = 0; split <= length; ++split) {
        _Alignas(16) u8 memory[4096];
        ScratchArena scratch = {.data = memory};
        MdPiece pieces[] = {
            {0}, {(u8*)text, split}, {0}, {(u8*)text + split, length - split}, {0},
        };
        MdView view = {.pieces = pieces, .count = sizeof(pieces) / sizeof(pieces[0])};
        MdReader reader;
        CHECK(md_reader_init(&reader, &view, &scratch));
        u32 actual[8];
        neo_recognize(kind, &reader, flags, parameter, actual);
        if (memcmp(actual, expected, sizeof(actual))) {
            fprintf(stderr, "mismatch: kind=%u split=%u flags=%u parameter=%u length=%u input=\"%.*s\"\n",
                    kind, split, flags, parameter, length, (int)length, text);
            for (unsigned i = 0; i < 8; ++i)
                fprintf(stderr, "  [%u] expected=%u actual=%u\n", i, expected[i], actual[i]);
            abort();
        }
        ++comparisons;
    }
}

static void reader_tests(void) {
    _Alignas(16) u8 memory[4096];
    ScratchArena scratch = {.data = memory};
    u8 a[] = {'a', '\r'}, b[] = {'\n', 0, 'b', '\n'};
    MdPiece pieces[] = {{0}, {a, 2}, {0}, {b, 4}, {0}};
    MdView view = {.pieces = pieces, .count = 5};
    MdReader reader;
    CHECK(md_reader_init(&reader, &view, &scratch));
    CHECK(reader.length == 6);
    CHECK(md_reader_byte(&reader, 6) == -1);
    CHECK(md_reader_byte(&reader, UINT32_MAX) == -1);
    CHECK(md_reader_byte(&reader, 3) == 0);
    CHECK(md_reader_byte(&reader, 0) == 'a');
    CHECK(md_reader_byte(&reader, 5) == '\n');
    CHECK(md_reader_match(&reader, 0, "A\r\n", 3, true));
    CHECK(!md_reader_match(&reader, 0, "A\r\n", 3, false));
    MdPhysicalLine line;
    CHECK(md_reader_line(&reader, 0, &line));
    CHECK(line.begin == 0 && line.content_end == 1 && line.end == 3);
    CHECK(md_reader_line(&reader, 3, &line));
    CHECK(line.begin == 3 && line.content_end == 5 && line.end == 6);
    CHECK(!md_reader_line(&reader, 6, &line));
    u8 copy[6];
    CHECK(md_reader_copy(&reader, (MdRange){0, 6}, copy));
    CHECK(memcmp(copy, "a\r\n\0b\n", 6) == 0);
    CHECK(md_reader_copy(&reader, (MdRange){6, 6}, NULL));
    CHECK(!md_reader_copy(&reader, (MdRange){1, 0}, copy));
    CHECK(!md_reader_copy(&reader, (MdRange){0, 7}, copy));
    CHECK(!md_reader_copy(&reader, (MdRange){0, 1}, NULL));
    MdView empty = {0};
    CHECK(md_reader_init(&reader, &empty, NULL));
    CHECK(md_reader_byte(&reader, 0) == -1);
    CHECK(!md_reader_init(&reader, NULL, &scratch));
    MdView invalid = {.count = 1};
    CHECK(!md_reader_init(&reader, &invalid, &scratch));
    MdPiece missing = {.length = 1};
    invalid.pieces = &missing;
    CHECK(!md_reader_init(&reader, &invalid, &scratch));
    MdPiece overflow[] = {{a, UINT32_MAX}, {b, 1}};
    invalid.pieces = overflow;
    invalid.count = 2;
    CHECK(!md_reader_init(&reader, &invalid, &scratch));
    scratch.len = ARENA_VCAP - 1;
    CHECK(!md_reader_init(&reader, &view, &scratch));
}

static void expected_tests(void) {
    _Alignas(16) u8 memory[4096];
    ScratchArena scratch = {.data = memory};
    MdPiece piece = {(u8*)"| :-- | --: | :-: |\r\n", 21};
    MdView view = {.pieces = &piece, .count = 1};
    MdReader reader;
    CHECK(md_reader_init(&reader, &view, &scratch));
    u32 end, count;
    CHECK(md_is_table_underline(&reader, 0, &end, &count));
    CHECK(count == 3);
    MdAlignment alignments[3];
    md_analyze_table_alignment(&reader, (MdRange){0, end}, alignments, 3);
    CHECK(alignments[0] == MD_ALIGN_LEFT);
    CHECK(alignments[1] == MD_ALIGN_RIGHT);
    CHECK(alignments[2] == MD_ALIGN_CENTER);

    piece = (MdPiece){(u8*)" \t  x", 5};
    CHECK(md_reader_init(&reader, &view, &scratch));
    CHECK(md_line_indentation(&reader, 0, 0, &end) == 6 && end == 4);
    CHECK(md_line_indentation(&reader, 2, 0, &end) == 4 && end == 4);

    piece = (MdPiece){(u8*)"9. text", 7};
    CHECK(md_reader_init(&reader, &view, &scratch));
    MdContainerFrame container;
    CHECK(md_is_container_mark(&reader, 0, 0, 0, &end, &container));
    CHECK(container.start == 9 && container.marker == '.' && end == 2);
    CHECK(!md_is_container_mark(&reader, 0, 4, 0, &end, &container));
    CHECK(md_is_container_mark(&reader, MD_FLAG_NOINDENTEDCODEBLOCKS, 4, 0, &end, &container));
}

static u32 random_state = 0x72574d44;
static u32 random_u32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static _Alignas(16) u8 durable_memory[32 * 1024 * 1024];
static _Alignas(16) u8 fresh_memory[32 * 1024 * 1024];
static _Alignas(16) u8 scratch_memory[32 * 1024 * 1024];
static _Alignas(16) u8 result_memory[4 * 1024 * 1024];

static MdView one_piece(char* source, MdPiece* piece) {
    *piece = (MdPiece){(u8*)source, (u32)strlen(source)};
    return (MdView){piece, 1};
}

static bool has_kind(MdResult result, MdNodeKind kind) {
    for (u32 i = 0; i < result.count; ++i)
        if (result.nodes[i].kind == kind)
            return true;
    return false;
}

static void validate_result(MdResult result, u32 length) {
    for (u32 i = 0; i < result.count; ++i) {
        const MdNode* node = &result.nodes[i];
        CHECK(node->start_idx <= node->end_idx && node->end_idx <= length);
        CHECK((u64)node->content_offset + node->content_length <= node->end_idx - node->start_idx);
        if (i) {
            const MdNode* previous = &result.nodes[i - 1];
            CHECK(previous->start_idx <= node->start_idx);
            if (previous->start_idx == node->start_idx)
                CHECK(previous->end_idx >= node->end_idx);
        }
        for (u32 j = 0; j < i; ++j)
            CHECK(!md_crosses((MdRange){node->start_idx, node->end_idx},
                             (MdRange){result.nodes[j].start_idx, result.nodes[j].end_idx}));
        if (node->kind == MD_NODE_REPLACEMENT) {
            CHECK(node->data.replacement.length >= 1 && node->data.replacement.length <= 6);
            CHECK(node->data.replacement.repeat >= 1);
        }
        if (node->kind == MD_NODE_LINK || node->kind == MD_NODE_IMAGE || node->kind == MD_NODE_WIKI_LINK) {
            CHECK(node->data.link.dest_start <= node->data.link.dest_end);
            CHECK(node->data.link.dest_end <= length);
        }
        if (node->kind != MD_NODE_HIDDEN && node->kind != MD_NODE_REPLACEMENT)
            continue;
        for (u32 j = i + 1; j < result.count; ++j) {
            const MdNode* other = &result.nodes[j];
            if (other->kind != MD_NODE_HIDDEN && other->kind != MD_NODE_REPLACEMENT)
                continue;
            if (node->start_idx == node->end_idx || other->start_idx == other->end_idx)
                continue;
            if (node->start_idx < other->end_idx && other->start_idx < node->end_idx) {
                fprintf(stderr, "overlapping mappings: %u [%u,%u) and %u [%u,%u)\n", node->kind,
                        node->start_idx, node->end_idx, other->kind, other->start_idx, other->end_idx);
                abort();
            }
        }
    }
}

static void integrated_tests(void) {
    const u32 extensions = MD_DEFAULT_FLAGS | MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS |
        MD_FLAG_INSERT | MD_FLAG_UNDERLINE | MD_FLAG_SPOILERS | MD_FLAG_SUPERSCRIPTS |
        MD_FLAG_SUBSCRIPTS | MD_FLAG_HIGHLIGHT | MD_FLAG_LATEXMATHSPANS;
    struct { const char* source; MdNodeKind kind; } cases[] = {
        {"", MD_NODE_PARAGRAPH},
        {"hello *world* and **strong**\n", MD_NODE_EMPHASIS},
        {"# title\n", MD_NODE_HEADING},
        {"title\n=====\n", MD_NODE_HEADING},
        {"> quote\n>\n> other\n", MD_NODE_QUOTE},
        {"- a\n- b\n  - nested\n", MD_NODE_LIST_ITEM},
        {"- [x] done\n- [ ] todo\n", MD_NODE_LIST_ITEM},
        {"```c\nx < &amp;\n```\n", MD_NODE_CODE_BLOCK},
        {"    indented\n    code\n", MD_NODE_CODE_BLOCK},
        {"---\n", MD_NODE_THEMATIC_BREAK},
        {"<div>\nhello\n</div>\n", MD_NODE_HTML_BLOCK},
        {"a <b title='x'>raw</b>\n", MD_NODE_HTML_INLINE},
        {"a ` x\ny ` code\n", MD_NODE_CODE_INLINE},
        {"[label](https://example.test \"title\")\n", MD_NODE_LINK},
        {"![image](image.png)\n", MD_NODE_IMAGE},
        {"[ref]\n\n[ref]: /target 'title'\n", MD_NODE_LINK},
        {"a &amp; &#x1f600; &NotEqualTilde;\n", MD_NODE_REPLACEMENT},
        {"a  \nb\\\nc\n", MD_NODE_HARD_BREAK},
        {"| a | b |\n|:-|--:|\n|x|y|\n", MD_NODE_TABLE_CELL},
        {"> [!NOTE]\n> body\n", MD_NODE_ADMONITION},
        {"~~gone~~\n", MD_NODE_STRIKETHROUGH},
        {"++added++\n", MD_NODE_INSERT},
        {"_under_\n", MD_NODE_UNDERLINE},
        {"||secret||\n", MD_NODE_SPOILER},
        {"^super^\n", MD_NODE_SUPERSCRIPT},
        {"~sub~\n", MD_NODE_SUBSCRIPT},
        {"==marked==\n", MD_NODE_HIGHLIGHT},
        {"$x+y$\n", MD_NODE_MATH_INLINE},
        {"$$x+y$$\n", MD_NODE_MATH_DISPLAY},
        {"[[target|label]]\n", MD_NODE_WIKI_LINK},
        {"a[^n]\n\n[^n]: note\n", MD_NODE_FOOTNOTE_REFERENCE},
        {"https://example.test/x and x@example.test\n", MD_NODE_LINK},
    };
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Arena durable = {.data = durable_memory}, output = {.data = result_memory};
        ScratchArena scratch = {.data = scratch_memory};
        MdPiece piece;
        MdView source = one_piece((char*)cases[i].source, &piece);
        MdState state = md_create(&source, &durable, &scratch, extensions);
        if (state.error) {
            fprintf(stderr, "create failed %u: error=%u source=%s\n", i, state.error, cases[i].source);
            abort();
        }
        MdResult result = md_get(&state, &output, &scratch, 0, piece.length);
        CHECK(state.error == MD_ERROR_NONE);
        validate_result(result, piece.length);
        if (piece.length && !has_kind(result, cases[i].kind)) {
            fprintf(stderr, "missing kind %u in case %u: %s\n", cases[i].kind, i, cases[i].source);
            abort();
        }
        /* The same bytes repartitioned at every position must produce the same
         * records, not merely the same counts. */
        for (u32 split = 0; split <= piece.length; ++split) {
            Arena other = {.data = fresh_memory};
            ScratchArena temp = {.data = scratch_memory};
            MdPiece parts[] = {{piece.bytes, split}, {0}, {piece.bytes + split, piece.length - split}};
            MdView partitioned = {.pieces = parts, .count = 3};
            MdState fresh = md_create(&partitioned, &other, &temp, extensions);
            CHECK(fresh.error == MD_ERROR_NONE);
            MdResult got = md_get(&fresh, &output, &temp, 0, piece.length);
            CHECK(fresh.error == MD_ERROR_NONE);
            CHECK(got.count == result.count);
            CHECK(!result.count || !memcmp(got.nodes, result.nodes, result.count * sizeof(MdNode)));
            md_destroy(&fresh);
        }
        md_destroy(&state);
    }
}

static void edit_tests(void) {
    char text[2048] = "# head\n\nfirst *paragraph*\n\n> quote\n\nlast [ref]\n\n[ref]: /target\n";
    u32 flags = MD_DEFAULT_FLAGS | MD_DIALECT_GITHUB | MD_FLAG_WIKILINKS | MD_FLAG_INSERT |
                MD_FLAG_SPOILERS | MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS | MD_FLAG_HIGHLIGHT | MD_FLAG_LATEXMATHSPANS;
    Arena durable = {.data = durable_memory};
    ScratchArena scratch = {.data = scratch_memory};
    MdPiece piece;
    MdView source = one_piece(text, &piece);
    MdState state = md_create(&source, &durable, &scratch, flags);
    CHECK(state.error == MD_ERROR_NONE);
    const char alphabet[] = "abc 012*_-`~[]():#>!|\n\r\t$=+^";
    for (u32 iteration = 0; iteration < 1200; ++iteration) {
        u32 length = (u32)strlen(text);
        u32 offset = random_u32() % (length + 1);
        u32 remove_request = random_u32() % 8;
        u32 removed = min(remove_request, length - offset);
        u32 inserted = random_u32() % 8;
        if (length + inserted - removed >= sizeof(text))
            inserted = 0;
        char addition[8];
        for (u32 i = 0; i < inserted; ++i)
            addition[i] = alphabet[random_u32() % (sizeof(alphabet) - 1)];
        memmove(text + offset + inserted, text + offset + removed, length - offset - removed + 1);
        memcpy(text + offset, addition, inserted);
        source = one_piece(text, &piece);
        scratch = (ScratchArena){.data = scratch_memory};
        md_update(&state, &scratch, &source, inserted, offset, removed);
        if (state.error) {
            fprintf(stderr, "update %u failed error %u offset %u removed %u inserted %u input=%s\n",
                    iteration, state.error, offset, removed, inserted, text);
            abort();
        }
        Arena fresh_arena = {.data = fresh_memory}, output = {.data = result_memory};
        scratch = (ScratchArena){.data = scratch_memory};
        MdState fresh = md_create(&source, &fresh_arena, &scratch, flags);
        CHECK(fresh.error == MD_ERROR_NONE);
        MdResult actual = md_get(&state, &output, &scratch, 0, piece.length);
        MdResult expected = md_get(&fresh, &output, &scratch, 0, piece.length);
        CHECK(state.error == MD_ERROR_NONE && fresh.error == MD_ERROR_NONE);
        validate_result(actual, piece.length);
        if (actual.count != expected.count || (actual.count && memcmp(actual.nodes, expected.nodes, actual.count * sizeof(MdNode)))) {
            fprintf(stderr, "incremental mismatch at iteration %u edit [%u,%u) +%u: actual %u expected %u\n%s\n",
                    iteration, offset, offset + removed, inserted, actual.count, expected.count, text);
            for (u32 i = 0; i < max(actual.count, expected.count); ++i) {
                if (i < actual.count)
                    fprintf(stderr, " A %u %u [%u,%u)", i, actual.nodes[i].kind, actual.nodes[i].start_idx, actual.nodes[i].end_idx);
                if (i < expected.count)
                    fprintf(stderr, " E %u %u [%u,%u)", i, expected.nodes[i].kind, expected.nodes[i].start_idx, expected.nodes[i].end_idx);
                fputc('\n', stderr);
            }
            abort();
        }
        md_destroy(&fresh);
        CHECK(durable.len < sizeof(durable_memory));
    }
    md_destroy(&state);
}

static void invariant_tests(void) {
    char text[] = "hello **world**!\n\n> ***nested*** &amp; ` code `\n\n| a | b |\n|---|---|\n|x|y|\n\n[ref]\n\n[ref]: /target\n";
    Arena durable = {.data = durable_memory}, output = {.data = result_memory};
    ScratchArena scratch = {.data = scratch_memory};
    MdPiece piece;
    MdView view = one_piece(text, &piece);
    u32 length = piece.length;
    MdState state = md_create(&view, &durable, &scratch, MD_DEFAULT_FLAGS | MD_FLAG_TABLES);
    CHECK(state.error == MD_ERROR_NONE);
    memset(text, '?', length);
    memset(scratch_memory, 0xa5, scratch.len);
    scratch.len = 0;
    MdResult full = md_get(&state, &output, &scratch, 0, length);
    CHECK(state.error == MD_ERROR_NONE);
    validate_result(full, length);
    bool strong = false;
    for (u32 i = 0; i < full.count; ++i)
        if (full.nodes[i].kind == MD_NODE_STRONG && full.nodes[i].start_idx == 6) {
            CHECK(full.nodes[i].end_idx == 15);
            CHECK(full.nodes[i].content_offset == 2 && full.nodes[i].content_length == 5);
            strong = true;
        }
    CHECK(strong);
    usize snapshot_end = output.len;
    for (u32 begin = 0; begin <= length; ++begin) {
        for (u32 end = begin; end <= length; ++end) {
            output.len = snapshot_end;
            scratch.len = 0;
            MdResult range = md_get(&state, &output, &scratch, begin, end);
            CHECK(state.error == MD_ERROR_NONE);
            u32 found = 0;
            if (begin != end)
                for (u32 i = 0; i < full.count; ++i) {
                    const MdNode* node = &full.nodes[i];
                    bool overlaps = node->start_idx == node->end_idx ?
                        node->start_idx >= begin && (node->start_idx < end || (end == length && node->start_idx == end)) :
                        node->start_idx < end && node->end_idx > begin;
                    if (overlaps) {
                        CHECK(found < range.count);
                        CHECK(!memcmp(node, &range.nodes[found++], sizeof(*node)));
                    }
                }
            CHECK(found == range.count);
        }
    }
    const u64 revision = state.revision;
    MdView invalid = {.count = 1};
    scratch.len = 0;
    md_update(&state, &scratch, &invalid, 0, 0, 0);
    CHECK(state.error != MD_ERROR_NONE && state.revision == revision);
    view = one_piece("replacement **document**\n", &piece);
    usize durable_end = durable.len;
    durable.len = ARENA_VCAP - 1;
    scratch.len = 0;
    md_update(&state, &scratch, &view, piece.length, 0, length);
    CHECK(state.error == MD_ERROR_OUT_OF_MEMORY && state.revision == revision);
    durable.len = durable_end;
    scratch.len = ARENA_VCAP - 1;
    md_update(&state, &scratch, &view, piece.length, 0, length);
    CHECK(state.error != MD_ERROR_NONE && state.revision == revision);
    scratch.len = 0;
    output.len = snapshot_end;
    MdResult unchanged = md_get(&state, &output, &scratch, 0, length);
    CHECK(state.error == MD_ERROR_NONE && unchanged.count == full.count);
    CHECK(!memcmp(full.nodes, unchanged.nodes, full.count * sizeof(MdNode)));
    scratch.len = 0;
    md_update(&state, &scratch, &view, piece.length, 0, length);
    CHECK(state.error == MD_ERROR_NONE && state.revision == revision + 1);
    CHECK(!memcmp(full.nodes, unchanged.nodes, full.count * sizeof(MdNode)));
    md_destroy(&state);

    /* Label/definition slot reuse must stay bounded across distinct labels. */
    durable = (Arena){.data = durable_memory};
    scratch.len = 0;
    view = one_piece("", &piece);
    state = md_create(&view, &durable, &scratch, MD_DEFAULT_FLAGS);
    char reference[128];
    length = 0;
    for (u32 i = 0; i < 500; ++i) {
        snprintf(reference, sizeof(reference), "[label%u]\n\n[label%u]: /destination%u\n", i, i, i);
        view = one_piece(reference, &piece);
        scratch.len = 0;
        md_update(&state, &scratch, &view, piece.length, 0, length);
        CHECK(state.error == MD_ERROR_NONE);
        CHECK(state.labels_count <= 2 && state.definitions_count <= 1);
        CHECK(durable.len < 128 * 1024);
        length = piece.length;
    }
    md_destroy(&state);

    /* Overflow fails at construction; it must never truncate content_offset. */
    char long_fence[65544];
    memset(long_fence, 'x', sizeof(long_fence));
    memcpy(long_fence, "```", 3);
    memcpy(long_fence + 65538, "\na\n", 3);
    long_fence[65541] = 0;
    view = one_piece(long_fence, &piece);
    durable = (Arena){.data = durable_memory};
    scratch.len = 0;
    state = md_create(&view, &durable, &scratch, MD_DEFAULT_FLAGS);
    CHECK(state.error == MD_ERROR_OVERFLOW);
    md_destroy(&state);
    char nested[MD_MAX_BLOCK_DEPTH + 8];
    memset(nested, '>', sizeof(nested) - 2);
    nested[sizeof(nested) - 2] = 'x';
    nested[sizeof(nested) - 1] = 0;
    view = one_piece(nested, &piece);
    durable = (Arena){.data = durable_memory};
    scratch.len = 0;
    state = md_create(&view, &durable, &scratch, MD_DEFAULT_FLAGS);
    CHECK(state.error == MD_ERROR_OVERFLOW);
    md_destroy(&state);
}

uint32_t md_neo_document_oracle(const char* source, uint32_t length, uint32_t flags,
                               uint32_t* records, uint32_t capacity);

uint32_t md_neo_commonmark_code(const char* source, uint32_t length, char* bytes,
                                uint32_t capacity, uint32_t* lengths, uint32_t slots);

static void document_compare(const char* text, u32 flags) {
    u32 records[8192 * 8];
    u32 size = (u32)strlen(text);
    u32 count = md_neo_document_oracle(text, size, flags, records, 8192);
    CHECK(count != UINT32_MAX);
    char expected[65536], actual[65536];
    u32 expected_count = 0, actual_count = 0;
    u32 spans[16] = {0}, got_spans[16] = {0};
    bool multiline_code = false;
    const MdNodeKind span_kind[] = {
        MD_NODE_EMPHASIS, MD_NODE_STRONG, MD_NODE_LINK, MD_NODE_IMAGE, MD_NODE_CODE_INLINE,
        MD_NODE_INSERT, MD_NODE_STRIKETHROUGH, MD_NODE_MATH_INLINE, MD_NODE_MATH_DISPLAY,
        MD_NODE_WIKI_LINK, MD_NODE_UNDERLINE, MD_NODE_SPOILER, MD_NODE_SUPERSCRIPT,
        MD_NODE_SUBSCRIPT, MD_NODE_FOOTNOTE_REFERENCE, MD_NODE_HIGHLIGHT,
    };
    for (u32 i = 0; i < count; ++i) {
        u32* record = records + i * 8;
        if (record[0] == 2) {
            ++spans[record[1]];
            if (record[1] == 4)
                for (u32 at = record[2]; at < record[3]; ++at)
                    multiline_code |= text[at] == '\n' || text[at] == '\r';
        }
        if (record[0] != 4)
            continue;
        if (record[4] & 2) {
            u8 bytes[4];
            u32 n = utf8proc_encode_char((i32)record[5], bytes);
            for (u32 repeat = 0; repeat < record[6]; ++repeat) {
                CHECK(expected_count + n < sizeof(expected));
                memcpy(expected + expected_count, bytes, n);
                expected_count += n;
            }
        } else {
            u32 n = record[3] - record[2];
            u8 decoded[8];
            u32 decoded_count = record[1] == 4 ? md_entity_decode((const u8*)text + record[2], n, decoded) : 0;
            CHECK(expected_count + (decoded_count ? decoded_count : n) < sizeof(expected));
            memcpy(expected + expected_count, decoded_count ? decoded : (const u8*)text + record[2], decoded_count ? decoded_count : n);
            expected_count += decoded_count ? decoded_count : n;
        }
    }
    Arena durable = {.data = durable_memory}, output = {.data = result_memory};
    ScratchArena scratch = {.data = scratch_memory};
    MdPiece piece;
    MdView source = one_piece((char*)text, &piece);
    MdState state = md_create(&source, &durable, &scratch, flags);
    if (state.error) {
        fprintf(stderr, "document create error %u flags %x: [%s]\n", state.error, flags, text);
        abort();
    }
    MdResult result = md_get(&state, &output, &scratch, 0, size);
    if (state.error) {
        fprintf(stderr, "document query error %u flags %x: [%s]\n", state.error, flags, text);
        abort();
    }
    validate_result(result, size);
    u32 cursor = 0;
    for (u32 i = 0; i < result.count; ++i) {
        const MdNode* node = &result.nodes[i];
        for (u32 k = 0; k < 16; ++k)
            got_spans[k] += node->kind == span_kind[k];
        if (node->kind != MD_NODE_HIDDEN && node->kind != MD_NODE_REPLACEMENT)
            continue;
        CHECK(node->start_idx >= cursor);
        u32 n = node->start_idx - cursor;
        CHECK(actual_count + n < sizeof(actual));
        memcpy(actual + actual_count, text + cursor, n);
        actual_count += n;
        if (node->kind == MD_NODE_REPLACEMENT) {
            for (u32 repeat = 0; repeat < node->data.replacement.repeat; ++repeat) {
                n = node->data.replacement.length;
                CHECK(actual_count + n < sizeof(actual));
                memcpy(actual + actual_count, node->data.replacement.text, n);
                actual_count += n;
            }
        }
        cursor = node->end_idx;
    }
    CHECK(actual_count + size - cursor < sizeof(actual));
    memcpy(actual + actual_count, text + cursor, size - cursor);
    actual_count += size - cursor;
    if ((!multiline_code && (actual_count != expected_count || memcmp(actual, expected, actual_count))) ||
        memcmp(spans, got_spans, sizeof(spans))) {
        fprintf(stderr, "document mismatch flags=%x source=[%s]\nexpected(%u)=[%.*s]\nactual(%u)=[%.*s]\n",
                flags, text, expected_count, expected_count, expected, actual_count, actual_count, actual);
        for (u32 i = 0; i < 16; ++i)
            if (spans[i] != got_spans[i])
                fprintf(stderr, "span %u expected %u actual %u\n", i, spans[i], got_spans[i]);
        abort();
    }
    if (multiline_code && flags == MD_DEFAULT_FLAGS) {
        char normalized[65536];
        u32 lengths[1024];
        /* Isolate each span: cmark-gfm 0.29 has an unmatched-backtick cache
         * bug ("`` a `b` c `d e`" loses the second span). Container prefixes
         * come from the owning leaf's line ranges, not the output mappings. */
        for (u32 i = 0; i < result.count; ++i) {
            const MdNode* code = &result.nodes[i];
            if (code->kind != MD_NODE_CODE_INLINE)
                continue;
            char isolated[65536];
            u32 isolated_count = 0;
            for (u32 b = 0; b < state.blocks_count; ++b) {
                const MdBlock* block = &state.blocks[b];
                if (!block->alive || !block->annotations_count)
                    continue;
                u32 base = md_block_base(&state, (MdBlockId){b, block->generation});
                bool owns = false;
                for (u32 a = 0; a < block->annotations_count; ++a)
                    owns |= block->annotations[a].node.kind == MD_NODE_CODE_INLINE &&
                            base + block->annotations[a].node.start_idx == code->start_idx;
                if (!owns)
                    continue;
                for (u32 l = 0; l < block->lines_count; ++l) {
                    MdLine line = block->lines[l];
                    u32 begin = max(base + line.content.begin, code->start_idx);
                    u32 end = min(base + line.content.end, code->end_idx);
                    if (begin < end) {
                        memcpy(isolated + isolated_count, text + begin, end - begin);
                        isolated_count += end - begin;
                    }
                    if (base + line.content.end >= code->start_idx && base + line.source.end < code->end_idx)
                        isolated[isolated_count++] = '\n';
                }
                break;
            }
            u32 code_count = md_neo_commonmark_code(isolated, isolated_count, normalized, sizeof(normalized), lengths, 1024);
            CHECK(code_count == 1);
            char rendered[65536];
            u32 rendered_count = 0, at = code->start_idx;
            for (u32 j = 0; j < result.count; ++j) {
                const MdNode* map = &result.nodes[j];
                if ((map->kind != MD_NODE_HIDDEN && map->kind != MD_NODE_REPLACEMENT) ||
                    map->start_idx < code->start_idx || map->end_idx > code->end_idx)
                    continue;
                CHECK(map->start_idx >= at);
                memcpy(rendered + rendered_count, text + at, map->start_idx - at);
                rendered_count += map->start_idx - at;
                if (map->kind == MD_NODE_REPLACEMENT)
                    for (u32 k = 0; k < map->data.replacement.repeat; ++k) {
                        memcpy(rendered + rendered_count, map->data.replacement.text, map->data.replacement.length);
                        rendered_count += map->data.replacement.length;
                    }
                at = map->end_idx;
            }
            memcpy(rendered + rendered_count, text + at, code->end_idx - at);
            rendered_count += code->end_idx - at;
            if (rendered_count != lengths[0] || memcmp(rendered, normalized, rendered_count)) {
                fprintf(stderr, "independent code mismatch source=[%s] expected=[%.*s] actual=[%.*s]\n", text,
                        lengths[0], normalized, rendered_count, rendered);
                abort();
            }
        }
    }
    md_destroy(&state);
}

static void document_tests(void) {
    const char* cases[] = {
        "", "hello **world**!", "***nested***", "**a *b* c**", "*a **b** c*", "a_b_c", "foo__bar__baz",
        "*a [b*](c)", "[a *b](c) d*", "[a *b*](c)", "*[a](b)*", "**[a](b)**", "[a [b](c)](d)",
        "[<https://example.test>](x)", "[a `b`](x)", "![a *b*](x)", "<https://example.test?a&b>",
        "[a](b \"title\")", "[a](<b> 'title')", "[a](b (title))", "[a](b\n\"title\")", "[a](b \"t\" extra)",
        "a [x] b\n\n[x]: /url\n", "[a][x]\n\n[x]: /url\n", "[a][]\n\n[a]: /url\n", "[x]\n\n[x]: /one\n[x]: /two\n",
        "[ß]\n\n[SS]: /url\n", "[x]\n\n[x]: /url\n \"title\"\n", "[x]\n\n[x]:\n /url\n",
        "a\nb\n", "a  \nb\n", "a\\\nb\n", "a &amp; &bogus; &#0; &NotEqualTilde;\n", "   space  \n next\n",
        "# hello **world** ###\n", "title\n-----\n", "- - -\n", "    code\n\n    more\n\nend\n",
        "```info\na * b\n```\n", "```\nno newline", "~~~\n~~~\n", "` a `", "`   `", "`a\nb`", "`` ` x ` ``",
        "> text\ncontinued\n", "> # h\nno quote\n", ">\n> x\n", "> > inner\n> outside\n",
        "- a\n- b\n", "- a\n\n- b\n", "- a\n\n  next\n", "1. a\n2. b\n", "- > a\n  > b\n",
        "-     code\n", "-\tcode\n", "-\n\n  x\n", "- a\n  - b\n    - c\n",
        "<div>\n*raw*\n</div>\n", "a <i title='x'>b</i>\n", "a <!--x--> b\n", "<?pi?>\n", "<![CDATA[x]]>\n",
        "a <x a=\"b\">c</x>\n", "a <x a='b'>c</x>\n", "a <x a=b>c</x>\n", "a <x a=>c</x>\n",
    };
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        document_compare(cases[i], MD_DEFAULT_FLAGS);
    const u32 extensions = MD_DEFAULT_FLAGS | MD_DIALECT_GITHUB | MD_FLAG_INSERT | MD_FLAG_WIKILINKS |
        MD_FLAG_LATEXMATHSPANS | MD_FLAG_SPOILERS | MD_FLAG_SUPERSCRIPTS | MD_FLAG_SUBSCRIPTS | MD_FLAG_HIGHLIGHT;
    const char* extended[] = {
        "~~a~~ ~b~", "++a++ ==b== ||c||", "^a^ ~b~", "$a$ $$b$$", "[[a|b]] [[c]]",
        "$a\nb$", "$a `b`$", "`a $b$`", "a <x title='`b`'> c", "<x:a`b> c`", "^a\\ b^ ~a\\ b~",
        "| a | b |\n|:-|--:|\n|x|y|\n", "|a|b|\n|-|-|\n|x|\n|y|z|extra|\n", "- [x] task\n- [ ] todo\n",
        "> [!NOTE]\n> x\n", "x[^a]\n\n[^a]: note\n", "https://example.test/path(foo). www.example.test a@example.test",
    };
    for (u32 i = 0; i < sizeof(extended) / sizeof(extended[0]); ++i)
        document_compare(extended[i], extensions);
    const char alphabet[] = "abc XYZ019*_`~[]():#!><|&;=+\\ \t\n";
    for (u32 trial = 0; trial < 3000; ++trial) {
        char input[96];
        u32 n = random_u32() % (sizeof(input) - 1);
        for (u32 i = 0; i < n; ++i)
            input[i] = alphabet[random_u32() % (sizeof(alphabet) - 1)];
        input[n] = 0;
        document_compare(input, MD_DEFAULT_FLAGS);
        document_compare(input, extensions);
    }
}

int main(void) {
    reader_tests();
    invariant_tests();
    document_tests();
    integrated_tests();
    edit_tests();
    expected_tests();
    const char* cases[] = {
        "", "-", "--", "---", "- -\t-", "***\n", "___x", "-*-", "#", "# title\r\n",
        "###### text", "####### text", "##nospace", "=\n", "===\t", "-- text", "|---|",
        "| :--- | --: | :-: |", "-- | --", ":-:", "|- x|", "```", "``` c\n", "``` a`b\n",
        "~~~ a`b", "``", "```` \t\r\n", "``` nope", ">", "> text", "*\ttext", "*text",
        "1. a", "123456789) x", "1234567890. x", "0.", "1.a", "&amp;", "&a;", "&#;",
        "&#1234567;", "&#12345678;", "&#x10ffff;", "&#x1234567;", "&#X0;", "&notKnown;",
        "<>", "<a b>", "<a\\>b>", "<a\nb>", "a(b(c))", "a(b", "a\\(b", "url)",
        "<https://example.org/a>", "<a:b>", "<ab:c>", "<me@example.org>", "<me@-x>",
        "<me@x->", "<me@x..y>", "<me@x.y>", "<me@x>", "<1a:xyz>", "|--|\n|--|",
    };
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        u32 length = strlen(cases[i]);
        for (unsigned kind = 0; kind < 10; ++kind) {
            compare(kind, cases[i], length, kind == 5 ? '`' : 0, kind == 5 ? 3 : 0);
            if (kind == 1)
                compare(kind, cases[i], length, MD_FLAG_PERMISSIVEATXHEADERS, 0);
            if (kind == 5)
                compare(kind, cases[i], length, '~', 3);
            if (kind == 6)
                compare(kind, cases[i], length, MD_FLAG_NOINDENTEDCODEBLOCKS, 4);
        }
    }
    const char alphabet[] = "abcXYZ019-_*+=:#&;\\()<>.!~`| \t\r\n";
    const char starts[] = "-#=|`~>*&<";
    for (unsigned iteration = 0; iteration < 1500; ++iteration) {
        char text[64];
        u32 length = random_u32() % sizeof(text);
        for (u32 i = 0; i < length; ++i)
            text[i] = alphabet[random_u32() % (sizeof(alphabet) - 1)];
        if (length)
            text[0] = starts[random_u32() % (sizeof(starts) - 1)];
        for (unsigned kind = 0; kind < 10; ++kind)
            compare(kind, text, length, kind == 5 ? '`' : 0, kind == 5 ? 3 : 0);
    }
    printf("parser_neo: %lu recognizer comparisons passed across piece splits\n", comparisons);
    return 0;
}

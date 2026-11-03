#pragma once

#include <document.h>
#include <lib/grim/bp.h>
#include <lib/grim/intrinsics.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/text/strutils.h>

typedef enum MdNodeKind {
    MD_NODE_PARAGRAPH,
    MD_NODE_HEADING,
    MD_NODE_QUOTE,
    MD_NODE_LIST,
    MD_NODE_LIST_ITEM,
    MD_NODE_CODE_BLOCK,
    MD_NODE_HTML_BLOCK,
    MD_NODE_THEMATIC_BREAK,
    MD_NODE_BLANK,

    MD_NODE_EMPHASIS,
    MD_NODE_STRONG,
    MD_NODE_LINK,
    MD_NODE_IMAGE,
    MD_NODE_CODE_INLINE,
    MD_NODE_HTML_INLINE,

    MD_NODE_HIDDEN,
    MD_NODE_REPLACEMENT,
    MD_NODE_HARD_BREAK,
    MD_NODE_SOFT_BREAK,

    MD_NODE_TABLE,
    MD_NODE_TABLE_HEAD,
    MD_NODE_TABLE_BODY,
    MD_NODE_TABLE_ROW,
    MD_NODE_TABLE_CELL,
    MD_NODE_ADMONITION,
    MD_NODE_STRIKETHROUGH,
    MD_NODE_INSERT,
    MD_NODE_UNDERLINE,
    MD_NODE_SPOILER,
    MD_NODE_SUPERSCRIPT,
    MD_NODE_SUBSCRIPT,
    MD_NODE_HIGHLIGHT,
    MD_NODE_MATH_INLINE,
    MD_NODE_MATH_DISPLAY,
    MD_NODE_WIKI_LINK,
    MD_NODE_FOOTNOTE_REFERENCE,
    MD_NODE_FOOTNOTE_DEFINITION,
    MD_NODE_FOOTNOTE_SECTION,
} MdNodeKind;

typedef enum MdAlignment {
    MD_ALIGN_DEFAULT,
    MD_ALIGN_LEFT,
    MD_ALIGN_CENTER,
    MD_ALIGN_RIGHT,
} MdAlignment;

typedef enum MdAdmonition {
    MD_ADMONITION_NOTE,
    MD_ADMONITION_TIP,
    MD_ADMONITION_IMPORTANT,
    MD_ADMONITION_WARNING,
    MD_ADMONITION_CAUTION,
} MdAdmonition;

typedef enum MdLinkPrefix {
    MD_LINK_PREFIX_NONE,
    MD_LINK_PREFIX_MAILTO,
    MD_LINK_PREFIX_HTTP,
} MdLinkPrefix;

typedef struct MdNode {
    u32 start_idx;
    u32 end_idx;

    struct {
        MdNodeKind kind           : 16;
        u32        content_offset : 16; // from start_idx
    };
    u32 content_length;

    union {
        u32 heading_level; /* HEADING: 1 through 6. */
        u32 quote_depth;   /* QUOTE: effective depth, starting at 1. */
        u32 line_count;    /* BLANK: physical blank-line count. */

        // ==== LIST ====
        struct {
            u32 depth;
            u32 start;

            struct {
                u32 ordered : 1;
                u32 tight   : 1;
            };

            u32 _padding;
        } list;

        // ==== LIST_ITEM ====
        struct {
            u16 depth;
            u16 number;

            struct {
                u32 ordered : 1;
                u32 task    : 1;
                u32 checked : 1;
            };
        } item;

        /* Task-list metadata supplements LIST_ITEM; an absent task has
         * task_mark_start == task_mark_end. */

        // ==== CODE_BLOCK ====
        struct {
            struct {
                u32 fenced : 1;
            };
        } code;

        // ==== LINK / IMAGE ====
        // Raw source attributes; reference links point into their definition.
        struct {
            // title is start, end
            u32 dest_start;
            u32 dest_end;

            struct {
                u32          has_title : 1;
                MdLinkPrefix prefix    : 8;
            };
        } link;

        struct {
            u32 row;
            u32 column;

            struct {
                u32         header    : 1;
                MdAlignment alignment : 8;
            };
        } cell;

        struct {
            u32 columns;
        } table;

        MdAdmonition admonition;

        /* Definition number follows first-reference order, not source order.
         * occurrence is 1-based for references; count is total uses. */
        struct {
            u32 number;
            u32 occurrence;
            u32 count;
        } footnote;

        // ==== REPLACEMENT ====
        // Render text[0..length) repeat times. UTF-8, not NUL-terminated.
        // Covers entities, normalized whitespace and U+FFFD substitution.
        struct {
            u8  text[6];
            u8  _padding[2];
            u32 length; // 1 through 6 bytes.
            u32 repeat; // At least 1; repetition does not expand the payload.
        } replacement;
    } data;
} MdNode;
STATIC_ASSERT(sizeof(MdNode) == 32);

typedef struct MdResult {
    rop(ro MdNode) nodes;
    u32 count;
} MdResult;

// Semantic tags stored as u8; source delimiter bytes and lengths live in L1.
typedef enum MdL0Tag {
    MD_L0_NONE = 0,
    MD_L0_PARAGRAPH,
    MD_L0_FENCED_CODE_BLOCK,
    MD_L0_THEMATIC_BREAK,
    MD_L0_UNORDERED_LIST,
    MD_L0_ATX_HEADING,
    MD_L0_SETEXT_HEADING,
    MD_L0_BLOCK_QUOTE,
    MD_L0_BLANK,
    MD_L0_EOF,
} MdL0Tag;

typedef struct MdL0Common {
    u16 pre_indent;    // Leading indentation columns.
    u16 content_start; // Byte offset from the L0 start, past the raw prefix.
} MdL0Common;
STATIC_ASSERT(sizeof(MdL0Common) == 4);

// One entry per non-paragraph L0; the L0 tag selects the member.
typedef union MdL0Extra {
    u16 post_indent; // Column after marker and following whitespace.
    u32 line_count;  // Physical blank-line count; pending blank count for EOF.
} MdL0Extra;
STATIC_ASSERT(sizeof(MdL0Extra) == 4);

typedef struct MdL1Metadata MdL1Metadata;

typedef struct MdMap {
    u32        offset;
    u8         text[6];
    u32        length      : 16;
    u32        repeat      : 16;
    MdNodeKind kind        : 8;
    u32        text_length : 8;
} MdMap;
STATIC_ASSERT(sizeof(MdMap) == 16);

typedef struct MdState {
    // Parallel arrays: index i identifies one delimiter occurrence within
    // its layer. Tags are encoded byte-sized kinds; offsets are absolute
    // source-byte positions. Metadata holds length, role, and semantic data.
    // Each layer's arrays share a count and are ordered by source position.

    // L0: potential block boundaries, ending in MD_L0_EOF. Count includes EOF.
    // Source lengths are the differences between adjacent offsets.
    rwp(rw u8) l0_tags;
    rwp(rw u32) l0_offsets;
    rwp(rw MdL0Common) l0_common;
    rwp(rw MdL0Extra) l0_extra;
    u32 l0_count;

    // L1: raw delimiter runs, independent of provisional L0 candidates.
    // Tags can be searched without loading offsets or metadata.
    rwp(rw u8) l1_tags;
    rwp(rw u32) l1_offsets;
    rwp(rw u32) l1_lengths;
    rwp(rw MdL1Metadata) l1_metadata;
    u32 l1_count;

    // HIDDEN / REPLACEMENT records are independent of the inline layer:
    // code/HTML blocks skip L1 but still need source-to-display mappings.
    // Mappings do not overlap each other or cross syntax boundaries.
    rwp(rw MdMap) mappings;
    u32 mappings_count;
} MdState;

// Tracks the next source byte to scan, directly or through the SIMD bridge.
typedef struct MdCursor {
    const DocView* source;
    u32            piece;
    u32            piece_offset;
} MdCursor;

// bridge must hold capacity bytes; copy only when a full window crosses pieces.
internal u32 md_fill(MdCursor* cursor, u8* bridge, u32 capacity, const u8** bytes) {
    *bytes = bridge;
    while (cursor->piece < cursor->source->count &&
           cursor->piece_offset == cursor->source->pieces[cursor->piece].length) {
        cursor->piece += 1;
        cursor->piece_offset = 0;
    }
    if (cursor->piece == cursor->source->count)
        return 0;

    const DocPiece* piece     = &cursor->source->pieces[cursor->piece];
    u32             available = piece->length - cursor->piece_offset;

    // Most common case
    if (likely(available >= capacity)) {
        *bytes = piece->bytes + cursor->piece_offset;
        cursor->piece_offset += capacity;
        return capacity;
    }

    u32 filled = 0;
    while (filled < capacity && cursor->piece < cursor->source->count) {
        piece     = &cursor->source->pieces[cursor->piece];
        available = piece->length - cursor->piece_offset;

        if (available > 0) {
            u32 take = min(capacity - filled, available);
            memcpy(bridge + filled, piece->bytes + cursor->piece_offset, take);
            filled += take;
            cursor->piece_offset += take;
        }

        if (cursor->piece_offset == piece->length) {
            cursor->piece += 1;
            cursor->piece_offset = 0;
        }
    }
    return filled;
}

MdState md_create(rop(ro DocView) source, rop(rw Arena) arena, rop(rw ScratchArena) scratch) {
    MdCursor         cursor       = {.source = source};
    static const u8  candidates[] = {'\n', 0,   '\t', '\r', ' ', '#', '>', '-', '+',  '=', '`',
                                     '*',  '_', '~',  '|',  '[', ']', '!', '<', '\\', '&'};
    // TODO: Precompute the fixed scanner table instead of rebuilding it per parse.
    SimdByteScanMany table        = simd_byte_scan_many_init(candidates, sizeof(candidates));
    SimdByteRunCarry run_carry    = {0};

    // Unspecified bytes have no block candidate (MD_L0_NONE).
    static const u8 l0_tag_table[256] = {
        ['\n'] = MD_L0_BLANK,
        ['\t'] = MD_L0_BLANK,
        [' ']  = MD_L0_BLANK,
        ['#']  = MD_L0_ATX_HEADING,
        ['*']  = MD_L0_UNORDERED_LIST,
        ['+']  = MD_L0_UNORDERED_LIST,
        ['-']  = MD_L0_UNORDERED_LIST,
        ['=']  = MD_L0_SETEXT_HEADING,
        ['>']  = MD_L0_BLOCK_QUOTE,
        ['_']  = MD_L0_THEMATIC_BREAK,
        ['`']  = MD_L0_FENCED_CODE_BLOCK,
        ['~']  = MD_L0_FENCED_CODE_BLOCK,
    };

    const u32 buffer_capacity = 4096;
    u8*       bridge             = scratch_alloc_aligned(scratch, u8, buffer_capacity);
    const u8* buffer             = NULL;
    u8*       lexical_tags       = scratch_alloc_aligned(scratch, u8, buffer_capacity);
    u32*      run_offsets        = scratch_alloc_aligned(scratch, u32, buffer_capacity);
    u32       buffer_file_offset = 0;
    u32       buffer_length      = 0;
    u32       run_count          = 0;
    u32       run_index          = 0;
    u32       run_offset         = 0;
    u8        run_tag            = 0;

    usize       l0_capacity = (usize)source->total_length + 1;
    u8*         l0_tags     = scratch_alloc_aligned(scratch, u8, l0_capacity);
    u32*        l0_offsets  = scratch_alloc_aligned(scratch, u32, l0_capacity);
    MdL0Common* l0_common   = scratch_alloc_aligned(scratch, MdL0Common, l0_capacity);
    MdL0Extra*  l0_extra    = scratch_alloc_aligned(scratch, MdL0Extra, l0_capacity);
    u32         l0_count    = 0;
    u32         extra_count = 0;

    u32  l1_capacity = source->total_length;
    u8*  l1_tags     = scratch_alloc_aligned(scratch, u8, l1_capacity);
    u32* l1_offsets  = scratch_alloc_aligned(scratch, u32, l1_capacity);
    u32* l1_lengths  = scratch_alloc_aligned(scratch, u32, l1_capacity);
    u32  l1_count    = 0;

    usize  mappings_capacity = ((usize)source->total_length + 1) / 2;
    MdMap* mappings          = scratch_alloc_aligned(scratch, MdMap, mappings_capacity);
    u32    mappings_count    = 0;

    u32        line_offset      = 0;
    u32        line_indent      = 0;
    u32        blank_line_count = 0;
    u32        at_line_start    = true;
    u8         line_tag;
    MdL0Common line_common;
    MdL0Extra  line_extra;

    l0_offsets[0] = 0;
    if (likely(source->total_length > 0)) {
        buffer_length = md_fill(&cursor, bridge, min(buffer_capacity, source->total_length), &buffer);
        run_count     = simd_byte_scan_runs(buffer, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
        run_tag       = lexical_tags[0];
        run_index     = 1;
    }

    // Every iteration, we process the previous run because then we know it's length.
    while (run_offset < source->total_length) {
        // Continuations may emit no starts; refill until a boundary or EOF.
        while (unlikely(run_index == run_count) && buffer_file_offset + buffer_length < source->total_length) {
            buffer_file_offset += buffer_length;
            u32 remaining = source->total_length - buffer_file_offset;
            buffer_length = md_fill(&cursor, bridge, min(buffer_capacity, remaining), &buffer);
            // continued runs are NOT output again.
            // "abc     " run_count = 2
            // "  xyzlmn" run_count = 1 // only the text run is output.
            run_count     = simd_byte_scan_runs(buffer, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
            run_index     = 0;
        }

        u32 run_end;
        // No boundary after refilling means EOF.
        if (run_index < run_count)
            run_end = buffer_file_offset + run_offsets[run_index];
        else
            run_end = source->total_length;

        if (run_tag == 0 && !at_line_start) {
            goto next_run;
        }

        u32 run_length = run_end - run_offset;
        u32 indent;
        u8  l0_tag = MD_L0_PARAGRAPH;
        if (run_tag > 32) {
            if (run_tag == 128) {
                assert(SCOPE_MARKDOWN, run_length <= UINT16_MAX);
                mappings[mappings_count++] = (MdMap){
                    .offset      = run_offset,
                    .length      = run_length,
                    .repeat      = run_length,
                    .kind        = MD_NODE_REPLACEMENT,
                    .text        = {0xef, 0xbf, 0xbd},
                    .text_length = 3,
                };
            } else {
                l1_tags[l1_count]    = run_tag;
                l1_offsets[l1_count] = run_offset;
                l1_lengths[l1_count] = run_length;
                l1_count += 1;
            }
        } else {
            if (run_tag == '\n') {
                if (at_line_start) {
                    blank_line_count += run_length;
                } else {
                    if (blank_line_count > 0) {
                        l0_tags[l0_count]       = MD_L0_BLANK;
                        l0_common[l0_count]     = (MdL0Common){0};
                        l0_extra[extra_count++] = (MdL0Extra){.line_count = blank_line_count};
                        l0_count += 1;
                        l0_offsets[l0_count] = line_offset;
                    }
                    l0_tags[l0_count]   = line_tag;
                    l0_common[l0_count] = line_common;
                    if (line_tag != MD_L0_PARAGRAPH) {
                        l0_extra[extra_count++] = line_extra;
                    }
                    l0_count += 1;
                    l0_offsets[l0_count] = run_offset + 1;
                    blank_line_count     = run_length - 1;
                }
                line_offset   = run_end;
                line_indent   = 0;
                at_line_start = true;
                goto next_run;
            } else if (run_tag == ' ' || run_tag == '\t') {
                if (!at_line_start) {
                    goto next_run;
                } else {
                    l0_tag = MD_L0_BLANK;
                    indent = line_indent;
                    goto consume_whitespace;
                }
            }
        }

        if (!at_line_start) {
            goto next_run;
        }

        if (run_tag > 32 && run_tag != 128) {
            u8 l0_tag_lookup = l0_tag_table[run_tag];

            switch (l0_tag_lookup) {
                case MD_L0_THEMATIC_BREAK:
                case MD_L0_FENCED_CODE_BLOCK: {
                    if (run_length < 3) {
                        l0_tag_lookup = MD_L0_NONE;
                    }
                    break;
                }
                case MD_L0_ATX_HEADING: {
                    if (run_length > 6) {
                        l0_tag_lookup = MD_L0_NONE;
                    }
                    break;
                }
                case MD_L0_UNORDERED_LIST: {
                    if (run_tag == '-' || run_tag == '*') {
                        if (run_length >= 3) {
                            l0_tag_lookup = MD_L0_THEMATIC_BREAK;
                        }
                    }
                    break;
                }
                case MD_L0_NONE:
                    break;
                default:
                    break;
            }

            if (l0_tag_lookup != MD_L0_NONE) {
                l0_tag = l0_tag_lookup;
            }
        }

        line_tag               = l0_tag;
        line_common.pre_indent = line_indent;
        if (l0_tag == MD_L0_PARAGRAPH) {
            line_common.content_start = run_offset - line_offset;
            at_line_start             = false;
            goto next_run;
        }
        indent = line_indent + run_length;

        run_offset = run_end;
        if (run_index < run_count) {
            run_tag = lexical_tags[run_index];
            run_index += 1;
        }

        while (run_offset < source->total_length && (run_tag == ' ' || run_tag == '\t')) {
            while (unlikely(run_index == run_count) && buffer_file_offset + buffer_length < source->total_length) {
                buffer_file_offset += buffer_length;
                u32 remaining = source->total_length - buffer_file_offset;
                buffer_length = md_fill(&cursor, bridge, min(buffer_capacity, remaining), &buffer);
                run_count = simd_byte_scan_runs(buffer, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
                run_index = 0;
            }
            if (run_index < run_count)
                run_end = buffer_file_offset + run_offsets[run_index];
            else
                run_end = source->total_length;
            run_length = run_end - run_offset;

        consume_whitespace:
            if (run_tag == ' ')
                indent += run_length;
            else
                indent = ((indent >> 2) + run_length) << 2;

            run_offset = run_end;
            if (run_index < run_count) {
                run_tag = lexical_tags[run_index];
                run_index += 1;
            }
        }

        assert(SCOPE_MARKDOWN, indent <= UINT16_MAX);
        line_indent   = indent;
        at_line_start = l0_tag == MD_L0_BLANK;
        if (!at_line_start) {
            assert(SCOPE_MARKDOWN, run_offset - line_offset <= UINT16_MAX);
            line_common.content_start = run_offset - line_offset;
            line_extra                = (MdL0Extra){.post_indent = indent};
        }
        continue;

    next_run:
        run_offset = run_end;
        if (run_index < run_count) {
            run_tag = lexical_tags[run_index];
            run_index += 1;
        }
    }

    // flush pending line
    if (!at_line_start) {
        if (blank_line_count > 0) {
            l0_tags[l0_count]       = MD_L0_BLANK;
            l0_common[l0_count]     = (MdL0Common){0};
            l0_extra[extra_count++] = (MdL0Extra){.line_count = blank_line_count};
            l0_count += 1;
            l0_offsets[l0_count] = line_offset;
            blank_line_count     = 0;
        }
        l0_tags[l0_count]   = line_tag;
        l0_common[l0_count] = line_common;
        if (line_tag != MD_L0_PARAGRAPH) {
            l0_extra[extra_count++] = line_extra;
        }
        l0_count += 1;
        line_indent = 0;
    }

    // write EOF sentinel with pending trailing whitespace
    l0_offsets[l0_count]    = source->total_length;
    l0_tags[l0_count]       = MD_L0_EOF;
    l0_common[l0_count]     = (MdL0Common){.pre_indent = line_indent};
    l0_extra[extra_count++] = (MdL0Extra){.line_count = blank_line_count};
    l0_count += 1;

    MdState result = {
        .l0_count       = l0_count,
        .l1_count       = l1_count,
        .mappings_count = mappings_count,
    };

    result.l0_tags = arena_alloc_aligned(arena, u8, l0_count);
    memcpy(result.l0_tags, l0_tags, l0_count * sizeof(u8));
    result.l0_offsets = arena_alloc_aligned(arena, u32, l0_count);
    memcpy(result.l0_offsets, l0_offsets, l0_count * sizeof(u32));
    result.l0_common = arena_alloc_aligned(arena, MdL0Common, l0_count);
    memcpy(result.l0_common, l0_common, l0_count * sizeof(MdL0Common));
    result.l0_extra = arena_alloc_aligned(arena, MdL0Extra, extra_count);
    memcpy(result.l0_extra, l0_extra, extra_count * sizeof(MdL0Extra));

    if (likely(result.l1_count > 0)) {
        u32 count      = result.l1_count;
        result.l1_tags = arena_alloc_aligned(arena, u8, count);
        memcpy(result.l1_tags, l1_tags, count * sizeof(u8));
        result.l1_offsets = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l1_offsets, l1_offsets, count * sizeof(u32));
        result.l1_lengths = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l1_lengths, l1_lengths, count * sizeof(u32));
    }

    if (result.mappings_count > 0) {
        u32 count       = result.mappings_count;
        result.mappings = arena_alloc_aligned(arena, MdMap, count);
        memcpy(result.mappings, mappings, count * sizeof(MdMap));
    }

    return result;
}

// TODO internal store should be the same gap buffer as document. then updates are cheap and minimal memmoves.
void md_update(rop(rw MdState) state,
               rop(rw ScratchArena) scratch,
               rop(ro DocView) source,
               u32 length,
               u32 offset,
               u32 removed_length);

MdResult md_get(rop(rw MdState) state, rop(rw Arena) arena, rop(rw ScratchArena) scratch, u32 begin, u32 end) {
    assert(SCOPE_MARKDOWN, end > begin);

    static const u8 context_tags[] = {
        MD_L0_FENCED_CODE_BLOCK, MD_L0_BLOCK_QUOTE, MD_L0_THEMATIC_BREAK, MD_L0_UNORDERED_LIST};
    SimdByteScanMany context_table = simd_byte_scan_many_init(context_tags, sizeof(context_tags));

    static const u8  query_tags[] = {MD_L0_PARAGRAPH,
                                     MD_L0_BLANK,
                                     MD_L0_ATX_HEADING,
                                     MD_L0_BLOCK_QUOTE,
                                     MD_L0_THEMATIC_BREAK,
                                     MD_L0_UNORDERED_LIST,
                                     MD_L0_SETEXT_HEADING,
                                     MD_L0_FENCED_CODE_BLOCK};
    SimdByteScanMany query_table  = simd_byte_scan_many_init(query_tags, sizeof(query_tags));

    static const u8  fence_tag[] = {MD_L0_FENCED_CODE_BLOCK};
    SimdByteScanMany fence_table = simd_byte_scan_many_init(fence_tag, sizeof(fence_tag));

    static const u8  inline_tags[] = {'`', '*', '_', '~', '|', '[', ']', '!', '<', '\\', '&'};
    SimdByteScanMany inline_table  = simd_byte_scan_many_init(inline_tags, sizeof(inline_tags));

    usize   capacity    = (usize)state->l0_count + state->l1_count + state->mappings_count;
    MdNode* nodes       = scratch_alloc_aligned(scratch, MdNode, capacity);
    u32     nodes_count = 0;

    u32* l0_stack       = scratch_alloc_aligned(scratch, u32, state->l0_count);
    u32  l0_stack_count = 0;
    u32  fence_open     = false;
    u32  fence_idx      = 0;

    u32 l1_idx      = 0;
    u32 mapping_idx = 0;

    rwp(ro SimdByteScanMany) current_mask = &context_table;

    u8  buffer[64];
    u32 buffer_l0_idx = 0;

    while (buffer_l0_idx + 1 < state->l0_count) {
        u32 buffer_bytes_read = min(64, state->l0_count - buffer_l0_idx - 1);
        u32 last              = buffer_l0_idx + buffer_bytes_read - 1;
        if (!fence_open && state->l0_offsets[last + 1] >= begin)
            current_mask = &query_table;

        memset(buffer, 0, sizeof(buffer));
        memcpy(buffer, state->l0_tags + buffer_l0_idx, buffer_bytes_read);
        u64 hits = 0;
        simd_byte_scan_many(buffer, 64, current_mask, &hits);
        hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);

        MdNodeKind node_kind = MD_NODE_PARAGRAPH;

        while (hits) {
            u32 index = (u32)__tzcnt_u64(hits);
            hits &= hits - 1;

            u32 l0_idx    = buffer_l0_idx + index;
            u32 l0_offset = state->l0_offsets[l0_idx];
            if (l0_offset >= end) // TODO need to set a flag then only stop on next l0 because of setext header
                goto get_end;

            u8           l0_tag      = buffer[index];
            u32          l0_end      = state->l0_offsets[l0_idx + 1];
            u32          l0_length   = l0_end - l0_offset;
            MdL0Metadata l0_metadata = state->l0_metadata[l0_idx];

            u32 base_indent = 0;
            if (l0_stack_count > 0) {
                u32 container_idx = l0_stack[l0_stack_count - 1];
                base_indent       = state->l0_metadata[container_idx].post_indent;
            }

            node_kind          = MD_NODE_PARAGRAPH;
            u32 content_offset = 0;

            switch (l0_tag) {
                case MD_L0_FENCED_CODE_BLOCK: {
                    if (l0_metadata.pre_indent > base_indent + 3)
                        continue;
                    if (!fence_open) {
                        // TODO validate info-string restrictions.
                        fence_idx    = l0_idx;
                        fence_open   = true;
                        current_mask = &fence_table;
                        simd_byte_scan_many(buffer, 64, current_mask, &hits);
                        hits &= (UINT64_MAX << index) & ~(1ULL << index);
                        hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);
                    } else {
                        MdL0Metadata opening = state->l0_metadata[fence_idx];
                        if (l0_metadata.marker_byte != opening.marker_byte ||
                            l0_metadata.marker_length < opening.marker_length)
                            continue;
                        // TODO validate trailing whitespace, then close the fence.
                    }
                    continue;
                }
                case MD_L0_BLOCK_QUOTE:
                    node_kind = MD_NODE_QUOTE;
                    // TODO strip quote markers and update quote containers in the L0 stack.
                    break;
                case MD_L0_THEMATIC_BREAK:
                    if (l0_metadata.pre_indent > base_indent + 3)
                        break;
                    node_kind = MD_NODE_THEMATIC_BREAK;
                    // TODO validate trailing whitespace and resolve setext headings.
                    break;
                case MD_L0_UNORDERED_LIST:
                    // TODO populate post_indent from marker width and padding, then update the L0 stack.
                    break;
                case MD_L0_BLANK:
                    node_kind = MD_NODE_BLANK;
                    break;
                case MD_L0_PARAGRAPH:
                    // TODO match active container continuations.
                    break;
                case MD_L0_ATX_HEADING: {
                    if (l0_metadata.pre_indent > base_indent + 3)
                        break;
                    u32 heading_offset =
                        l0_metadata.marker_offset + l0_metadata.marker_length + l0_metadata.marker_padding;
                    // paragraph fallback above 16-bit offsets; widen MdNode to support these headings.
                    if (heading_offset > UINT16_MAX)
                        break;
                    node_kind      = MD_NODE_HEADING;
                    content_offset = heading_offset;
                    // TODO strip optional closing hashes and trailing whitespace.
                    break;
                }
                case MD_L0_SETEXT_HEADING:
                    // TODO match active container continuations.
                    break;
            }
            // TODO examine skipped lines where container continuation requires it.
            if (l0_end <= begin && l0_offset < begin)
                continue;

            MdNode node = {
                .start_idx      = l0_offset,
                .end_idx        = l0_end,
                .content_offset = content_offset,
                .content_length = l0_length - content_offset,
                .kind           = node_kind,
            };
            if (node_kind == MD_NODE_QUOTE)
                node.data.quote_depth = l0_metadata.marker_length;
            if (node_kind == MD_NODE_HEADING)
                node.data.heading_level = l0_metadata.marker_length;
            if (node_kind == MD_NODE_BLANK)
                node.data.line_count = l0_metadata.line_count;
            nodes[nodes_count] = node;
            nodes_count += 1;

            // TODO binary search or better, checkpoint list
            while (l1_idx < state->l1_count && state->l1_offsets[l1_idx] < l0_offset)
                l1_idx += 1;
            while (l1_idx < state->l1_count && state->l1_offsets[l1_idx] < l0_end) {
                u32 l1_offset = state->l1_offsets[l1_idx];
                u8  l1_tag    = state->l1_tags[l1_idx];
                u32 l1_length = state->l1_lengths[l1_idx];
                // TODO filter L1 tags with the active inline mask and resolve runs.
                l1_idx += 1;
            }

            while (mapping_idx < state->mappings_count && state->mappings[mapping_idx].offset < l0_offset)
                mapping_idx += 1;

            while (mapping_idx < state->mappings_count && state->mappings[mapping_idx].offset < l0_end) {
                rop(ro MdMap) mapping = &state->mappings[mapping_idx];
                MdNode mapping_node   = {
                    .start_idx      = mapping->offset,
                    .end_idx        = mapping->offset + mapping->length,
                    .kind           = mapping->kind,
                    .content_length = mapping->length,
                };
                if (mapping->kind == MD_NODE_REPLACEMENT) {
                    memcpy(mapping_node.data.replacement.text, mapping->text, mapping->text_length);
                    mapping_node.data.replacement.length = mapping->text_length;
                    mapping_node.data.replacement.repeat = 1;
                }
                nodes[nodes_count] = mapping_node;
                nodes_count += 1;
                mapping_idx += 1;
            }
        }
        buffer_l0_idx += buffer_bytes_read;
    }

get_end:
    {
        rop(rw MdNode) final_nodes = arena_alloc_aligned(arena, MdNode, nodes_count);
        memcpy(final_nodes, nodes, sizeof(MdNode) * nodes_count);
        return (MdResult){
            .nodes = final_nodes,
            .count = nodes_count,
        };
    }
}

void md_destroy(rop(rw MdState) state);

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
            char text[6];
            u8   _padding[2];
            u32  length; // 1 through 6 bytes.
            u32  repeat; // At least 1; repetition does not expand the payload.
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
    MD_L0_EOF,
    MD_L0_BLANK,
    MD_L0_PARAGRAPH,
    MD_L0_THEMATIC_BREAK,
    MD_L0_UNORDERED_LIST,
    // TODO MD_L0_ORDERED_LIST,
    // TODO MD_L0_HTML,
    // TODO MD_L0_LINK_REF,
    // TODO MD_L0_TABLE, // NOT IN COMMONMARK SPEC
    MD_L0_ATX_HEADING,
    MD_L0_SETEXT_HEADING,
    MD_L0_BLOCK_QUOTE,
    MD_L0_FENCED_CODE_BLOCK,
    MD_L0_FENCE_NO_TRAILING_TEXT,
} MdL0Tag;

typedef union MdL0Common {
    struct {
        u16 pre_indent;    // Leading indentation columns.
        u16 content_start; // Byte offset from the L0 start, past the raw prefix.
    };
    u32 line_count; // BLANK: physical blank-line count.
} MdL0Common;
STATIC_ASSERT(sizeof(MdL0Common) == 4);

// One entry per quote/list candidate, in L0 order.
typedef struct MdL0Extra {
    u16 post_indent; // Width in columns of the whitespace after the marker.
} MdL0Extra;
STATIC_ASSERT(sizeof(MdL0Extra) == 2);

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
    u32 trailing_blank_count; // Pending physical blank-line count at EOF.

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
        ['`']  = MD_L0_FENCE_NO_TRAILING_TEXT,
        ['~']  = MD_L0_FENCE_NO_TRAILING_TEXT,
    };

    const u32 buffer_capacity    = 4096;
    u8*       bridge             = scratch_alloc_aligned(scratch, u8, buffer_capacity);
    const u8* buffer_ptr         = NULL;
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
        buffer_length = md_fill(&cursor, bridge, min(buffer_capacity, source->total_length), &buffer_ptr);
        run_count     = simd_byte_scan_runs(buffer_ptr, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
        run_tag       = lexical_tags[0];
        run_index     = 1;
    }

    // Every iteration, we process the previous run because then we know it's length.
    while (run_offset < source->total_length) {
        // Continuations may emit no starts; refill until a boundary or EOF.
        while (unlikely(run_index == run_count) && buffer_file_offset + buffer_length < source->total_length) {
            buffer_file_offset += buffer_length;
            u32 remaining = source->total_length - buffer_file_offset;
            buffer_length = md_fill(&cursor, bridge, min(buffer_capacity, remaining), &buffer_ptr);
            // continued runs are NOT output again.
            // "abc     " run_count = 2
            // "  xyzlmn" run_count = 1 // only the text run is output.
            run_count = simd_byte_scan_runs(buffer_ptr, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
            run_index = 0;
        }

        u32 run_end;
        // No boundary after refilling means EOF.
        if (run_index < run_count)
            run_end = buffer_file_offset + run_offsets[run_index];
        else
            run_end = source->total_length;

        if (run_tag == 0 && !at_line_start) {
            goto consume_body;
        }

        u32 run_length = run_end - run_offset;
        u32 indent;
        u8  l0_tag = MD_L0_PARAGRAPH;
        if (run_tag > 32) {
            if (run_tag == 128) {
                assert(SCOPE_MARKDOWN, run_length <= UINT16_MAX);
                mappings[mappings_count] = (MdMap){
                    .offset      = run_offset,
                    .length      = run_length,
                    .repeat      = run_length,
                    .kind        = MD_NODE_REPLACEMENT,
                    .text        = {0xef, 0xbf, 0xbd},
                    .text_length = 3,
                };
                mappings_count += 1;
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
                        l0_tags[l0_count]   = MD_L0_BLANK;
                        l0_common[l0_count] = (MdL0Common){.line_count = blank_line_count};
                        l0_count += 1;
                        l0_offsets[l0_count] = line_offset;
                    }
                    l0_tags[l0_count]   = line_tag;
                    l0_common[l0_count] = line_common;
                    if (line_tag == MD_L0_BLOCK_QUOTE || line_tag == MD_L0_UNORDERED_LIST) {
                        l0_extra[extra_count] = line_extra;
                        extra_count += 1;
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
        consume_body:
            line_tag -=
                (MD_L0_FENCE_NO_TRAILING_TEXT - MD_L0_FENCED_CODE_BLOCK) * (line_tag == MD_L0_FENCE_NO_TRAILING_TEXT);
            goto next_run;
        }

        if (run_tag > 32 && run_tag != 128) {
            u8 l0_tag_lookup = l0_tag_table[run_tag];

            switch (l0_tag_lookup) {
                case MD_L0_THEMATIC_BREAK:
                case MD_L0_FENCE_NO_TRAILING_TEXT: {
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
        line_common.pre_indent = (u16)line_indent;
        if (l0_tag == MD_L0_PARAGRAPH) {
            line_common.content_start = (u16)(run_offset - line_offset);
            at_line_start             = false;
            goto next_run;
        }
        line_indent += run_length;
        indent = line_indent;

        run_offset = run_end;
        if (run_index < run_count) {
            run_tag = lexical_tags[run_index];
            run_index += 1;
        }

        while (run_offset < source->total_length && (run_tag == ' ' || run_tag == '\t')) {
            while (unlikely(run_index == run_count) && buffer_file_offset + buffer_length < source->total_length) {
                buffer_file_offset += buffer_length;
                u32 remaining = source->total_length - buffer_file_offset;
                buffer_length = md_fill(&cursor, bridge, min(buffer_capacity, remaining), &buffer_ptr);
                run_count =
                    simd_byte_scan_runs(buffer_ptr, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
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
        at_line_start = (l0_tag == MD_L0_BLANK);
        if (!at_line_start) {
            assert(SCOPE_MARKDOWN, run_offset - line_offset <= UINT16_MAX);
            line_common.content_start = (u16)(run_offset - line_offset);
            line_extra                = (MdL0Extra){.post_indent = (u16)(indent - line_indent)};
        }
        line_indent = indent;
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
            l0_tags[l0_count]   = MD_L0_BLANK;
            l0_common[l0_count] = (MdL0Common){.line_count = blank_line_count};
            l0_count += 1;
            l0_offsets[l0_count] = line_offset;
            blank_line_count     = 0;
        }
        l0_tags[l0_count]   = line_tag;
        l0_common[l0_count] = line_common;
        if (line_tag == MD_L0_BLOCK_QUOTE || line_tag == MD_L0_UNORDERED_LIST) {
            l0_extra[extra_count] = line_extra;
            extra_count += 1;
        }
        l0_count += 1;
        line_indent = 0;
    }

    // write EOF sentinel with pending trailing whitespace
    l0_offsets[l0_count] = source->total_length;
    l0_tags[l0_count]    = MD_L0_EOF;
    l0_common[l0_count]  = (MdL0Common){.pre_indent = line_indent};
    l0_count += 1;

    MdState result = {
        .l0_count             = l0_count,
        .trailing_blank_count = blank_line_count,
        .l1_count             = l1_count,
        .mappings_count       = mappings_count,
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

// TODO internal store should be the same gap buffer_ptr as document. then updates are cheap and minimal memmoves.
void md_update(rop(rw MdState) state,
               rop(rw ScratchArena) scratch,
               rop(ro DocView) source,
               u32 length,
               u32 offset,
               u32 removed_length);

MdResult md_get(rop(rw MdState) state, rop(rw Arena) arena, rop(rw ScratchArena) scratch, u32 begin, u32 end) {
    assert(SCOPE_MARKDOWN, end > begin);

    // Setup: allocations, tables and state shared by both passes.
    static const u8  context_tags[] = {MD_L0_FENCED_CODE_BLOCK,
                                       MD_L0_FENCE_NO_TRAILING_TEXT,
                                       MD_L0_BLOCK_QUOTE,
                                       MD_L0_THEMATIC_BREAK,
                                       MD_L0_UNORDERED_LIST};
    SimdByteScanMany context_table  = simd_byte_scan_many_init(context_tags, sizeof(context_tags));

    // Include L1 output capacity when inline resolution is implemented.
    usize   capacity    = (usize)state->l0_count + state->mappings_count;
    MdNode* nodes       = scratch_alloc_aligned(scratch, MdNode, capacity);
    u32     nodes_count = 0;

    u8   fence_marker_byte   = 0;
    u32  fence_marker_length = 0; // Zero means no fence is open.
    u16* list_columns        = scratch_alloc_aligned(scratch, u16, state->l0_count);
    u32  list_stack_count    = 0;

    list_columns[list_stack_count] = 0; // Top-level content column.

    // TODO Revisit L1 seeks once all lookup sites are known; compare lower-bound search with a sparse L0-to-L1 index.
    u32 extra_idx   = 0;
    u32 l1_idx      = 0;
    u32 mapping_idx = 0;
    u32 chunk_start = 0;

    u32       chunk_idx   = 0;
    u32       buffer_size = 0;
    const u8* buffer_ptr  = NULL;

    u8* tail_buffer = scratch_alloc_aligned(scratch, u8, 4096);

    u64 hit_masks[64]; // One mask per 64 tags.
    u32 hit_mask_count = 0;
    u32 hit_mask_idx   = 0;

    u64 active_hit_mask = 0;

    // Loop 1: accumulate context before begin; never emit nodes.
    while (chunk_start < state->l0_count) {
        if (chunk_idx == buffer_size) {
            chunk_start += buffer_size;
            if (chunk_start >= state->l0_count)
                break;

            u32 remaining = state->l0_count - chunk_start;
            chunk_idx     = 0;
            buffer_ptr    = state->l0_tags + chunk_start;
            if (likely(remaining >= 4096)) {
                buffer_size    = 4096;
                hit_mask_count = 64;
                simd_byte_scan_many(buffer_ptr, 4096, &context_table, hit_masks);
            } else {
                buffer_size    = remaining;
                u32 scan_count = (buffer_size + 63) & ~63u;
                hit_mask_count = scan_count / 64;
                memcpy(tail_buffer, buffer_ptr, buffer_size);
                memset(tail_buffer + buffer_size, 0, scan_count - buffer_size);
                buffer_ptr = tail_buffer;
                simd_byte_scan_many(buffer_ptr, scan_count, &context_table, hit_masks);
                hit_masks[hit_mask_count - 1] &= UINT64_MAX >> (scan_count - buffer_size);
            }

            hit_mask_idx    = 0;
            active_hit_mask = hit_masks[0];
        }

        while (active_hit_mask == 0 && hit_mask_idx + 1 < hit_mask_count) {
            hit_mask_idx += 1;
            active_hit_mask = hit_masks[hit_mask_idx];
        }

        u32 next_idx = buffer_size;
        if (active_hit_mask != 0)
            next_idx = hit_mask_idx * 64 + (u32)__tzcnt_u64(active_hit_mask);

        u32 parent_idx = list_stack_count;
        if (list_stack_count > 0) {
            // Check continuation through the next hit, or through the end of the window.
            u32 check_end = min(next_idx + (active_hit_mask != 0), state->l0_count - 1 - chunk_start);
            while (chunk_idx < check_end && list_stack_count > 0) {
                u32 line_idx = chunk_start + chunk_idx;
                if (state->l0_offsets[line_idx + 1] > begin)
                    goto rewind_context;

                u8 tag = buffer_ptr[chunk_idx];
                if (tag != MD_L0_BLANK) {
                    // TODO Allow lazy continuation of an open paragraph.
                    u32 pre_indent = state->l0_common[line_idx].pre_indent;
                    while (pre_indent < list_columns[parent_idx])
                        parent_idx -= 1;

                    // Outside a fence, commit bullet dedent only after validating the marker.
                    if (parent_idx < list_stack_count && (fence_marker_length != 0 || tag != MD_L0_UNORDERED_LIST)) {
                        list_stack_count    = parent_idx;
                        fence_marker_length = 0;
                    }
                }

                if (chunk_idx == next_idx)
                    break;

                chunk_idx += 1;
            }
        }

        if (active_hit_mask == 0) { // on the last mask AND no hits
            // Clamp the offset lookup to the last valid index.
            u32 window_end_idx = min(chunk_start + buffer_size, state->l0_count - 1);
            if (state->l0_offsets[window_end_idx] > begin) {
                chunk_idx = buffer_size - 1;
                break;
            }
            chunk_idx = buffer_size;
            continue;
        }

        chunk_idx  = next_idx;
        u32 l0_idx = chunk_start + chunk_idx;
        u32 l0_end = state->l0_offsets[l0_idx + 1];
        if (l0_end > begin)
            break;

        u8 l0_tag = buffer_ptr[chunk_idx];
        chunk_idx += 1;
        active_hit_mask &= active_hit_mask - 1;

        if (fence_marker_length != 0) {
            if (l0_tag == MD_L0_FENCE_NO_TRAILING_TEXT &&
                state->l0_common[l0_idx].pre_indent < (u32)list_columns[list_stack_count] + 4) {
                u32 l0_start = state->l0_offsets[l0_idx];

                // find l1
                while (state->l1_offsets[l1_idx] < l0_start)
                    l1_idx += 1;

                // close the fence if the lengths are appropriate
                if (state->l1_tags[l1_idx] == fence_marker_byte && state->l1_lengths[l1_idx] >= fence_marker_length)
                    fence_marker_length = 0;
            }

            goto state_accumulate_loop_end;
        }

        // clang-format off
        switch (l0_tag) {
            case MD_L0_UNORDERED_LIST: {
                // clang-format on

                u32 pre_indent = state->l0_common[l0_idx].pre_indent;

                // cheapest early exit
                if (pre_indent >= ((u32)list_columns[parent_idx] + 4))
                    break;

                // second cheapest early exit
                u32 post_indent = state->l0_extra[extra_idx].post_indent;
                if (post_indent == 0)
                    break;

                // most expensive early exit/validation
                u32 l0_start = state->l0_offsets[l0_idx];
                while (state->l1_offsets[l1_idx] < l0_start)
                    l1_idx += 1;
                if (state->l1_lengths[l1_idx] != 1)
                    break;

                // at this point, this is a valid list continuation

                if (post_indent > 4)
                    post_indent = 1;

                list_stack_count               = parent_idx + 1;
                list_columns[list_stack_count] = (u16)(pre_indent + 1 + post_indent);
                break;

                // clang-format off
            }
            case MD_L0_THEMATIC_BREAK: break;
            case MD_L0_BLOCK_QUOTE: break;
            case MD_L0_FENCED_CODE_BLOCK:
            case MD_L0_FENCE_NO_TRAILING_TEXT: {
                // clang-format on

                if (state->l0_common[l0_idx].pre_indent >= (u32)list_columns[list_stack_count] + 4)
                    break;

                // A fence candidate's leading marker is its first L1 run.
                u32 l0_start = state->l0_offsets[l0_idx];
                while (state->l1_offsets[l1_idx] < l0_start)
                    l1_idx += 1;

                u8  marker = state->l1_tags[l1_idx];
                u32 length = state->l1_lengths[l1_idx];

                if (marker == '`' && l0_tag == MD_L0_FENCED_CODE_BLOCK) {
                    // Backticks are not allowed in a backtick fence's info string.
                    u32 info_idx = l1_idx + 1;
                    while (unlikely(info_idx < state->l1_count) && state->l1_offsets[info_idx] < l0_end) {
                        if (state->l1_tags[info_idx] == '`') {
                            goto state_accumulate_loop_end;
                        }

                        info_idx += 1;
                    }
                }

                fence_marker_byte   = marker;
                fence_marker_length = length;

                break;
                // clang-format off
            }

            // ---

            case MD_L0_BLANK: 
            case MD_L0_PARAGRAPH: 
            // TODO MD_L0_ORDERED_LIST,
            // TODO MD_L0_HTML,
            // TODO MD_L0_LINK_REF,
            // TODO MD_L0_TABLE, // NOT IN COMMONMARK SPEC
            case MD_L0_ATX_HEADING:
            case MD_L0_SETEXT_HEADING: 
            case MD_L0_EOF: 
            case MD_L0_NONE: // TODO should this go to the end of the enum for perf?
            default: unreachable(); break;
        }
        // clang-format on

    state_accumulate_loop_end:
        extra_idx += (l0_tag == MD_L0_BLOCK_QUOTE || l0_tag == MD_L0_UNORDERED_LIST);
    }

rewind_context:
    // Rewind only the skipped entries in this boundary window.
    while (state->l0_offsets[chunk_start + chunk_idx] > begin && chunk_idx > 0) {
        chunk_idx -= 1;
    }

    // AT THIS POINT: the current block is set, simd buffer_ptr is filled, cursor is set
    // WE KNOW: the current block index overlaps begin/we care about it.

    // Loop 2: create blocks overlapping [begin, end), using the accumulated state.
    while (chunk_start < state->l0_count) {
        if (chunk_idx == buffer_size) {
            chunk_start += buffer_size;
            if (chunk_start >= state->l0_count)
                break;

            u32 remaining = state->l0_count - chunk_start;
            chunk_idx     = 0;
            buffer_ptr    = state->l0_tags + chunk_start;
            if (likely(remaining >= 4096)) {
                buffer_size    = 4096;
                hit_mask_count = 64;
                memset(hit_masks, 0xff, sizeof(hit_masks));
            } else {
                buffer_size    = remaining;
                u32 scan_count = (buffer_size + 63) & ~63u;
                hit_mask_count = scan_count / 64;
                memcpy(tail_buffer, buffer_ptr, buffer_size);
                memset(tail_buffer + buffer_size, 0, scan_count - buffer_size);
                buffer_ptr = tail_buffer;
                memset(hit_masks, 0xff, hit_mask_count * sizeof(*hit_masks));
                hit_masks[hit_mask_count - 1] &= UINT64_MAX >> (scan_count - buffer_size);
            }
        }

        // scan through l0 tags from the current cursor position/index to end or EOF

        // for each entry:
        //     - update state (fence, indent, blocks)
        //     - emit/write/stream out an md node
        //     - if span end > end, exit

        // advance
    }

    // Cleanup and output.
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

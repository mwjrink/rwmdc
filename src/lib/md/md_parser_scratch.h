#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/intrinsics.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/text/strutils.h>

typedef struct MdPiece {
    u8* bytes;
    u32 length;
} MdPiece;

typedef struct MdView {
    MdPiece* pieces;
    u32      count;
    u32      total_length;
} MdView;

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

// Delimiter-specific payload layouts are specified separately.
typedef struct MdL0Metadata {
    u32 indent;
    u32 body_indent;
    u32 marker_offset;
    u32 marker_length;
    u32 trailing_whitespace : 1;
} MdL0Metadata;
typedef struct MdL1Metadata MdL1Metadata;

typedef struct MdMap {
    u32        offset;
    u32        length;
    MdNodeKind kind;
    u8         text[6];
    u32        text_length;
} MdMap;

typedef struct MdState {
    // Parallel arrays: index i identifies one delimiter occurrence within
    // its layer. Tags are encoded byte-sized kinds; offsets are absolute
    // source-byte positions. Metadata holds length, role, and semantic data.
    // Each layer's arrays share a count and are ordered by source position.

    // L0: block/container delimiter occurrences and implicit boundaries.
    // Nested constructs share the same ordered stream; no tree pointers.
    rwp(rw u8) l0_tags;
    rwp(rw u32) l0_offsets;
    rwp(rw u32) l0_lengths;
    rwp(rw MdL0Metadata) l0_metadata;
    u32 l0_count;

    // L1: inline delimiter occurrences with resolved roles inside L0 blocks.
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

// Only tracks the next source byte to copy. Scanning uses the filled buffer.
typedef struct MdCursor {
    const MdView* source;
    u32           piece;
    u32           piece_offset;
} MdCursor;

internal u32 md_fill(MdCursor* cursor, u8 buffer[64]) {
    u32 filled = 0;
    while (filled < 64 && cursor->piece < cursor->source->count) {
        const MdPiece* piece     = &cursor->source->pieces[cursor->piece];
        u32            available = piece->length - cursor->piece_offset;
        u32            take      = min(available, 64 - filled);

        memcpy(buffer + filled, piece->bytes + cursor->piece_offset, take);

        filled += take;
        cursor->piece_offset += take;

        if (cursor->piece_offset >= piece->length) {
            ++cursor->piece;
            cursor->piece_offset = 0;
        }
    }
    return filled;
}

MdState md_create(rop(ro MdView) source, rop(rw Arena) arena, rop(rw ScratchArena) scratch) {
    /* Fixed dialect: CommonMark 0.31.2 plus tables, strikethrough, task lists,
     * wikilinks, spoilers, and admonitions. No feature-selection flags.
     *
     * Strikethrough requires ~~ delimiters; a single ~ is not strikethrough.
     * Spoilers require || delimiters and follow the inline emphasis/strong
     * model (like **). Spoiler syntax is disabled inside tables.
     *
     * No bare URL/www/email autolinks or footnotes. Standard CommonMark
     * angle-bracket autolinks remain supported.
     * No math, underline, superscript/subscript, highlight, or insert syntax.
     * Keep CommonMark heading and whitespace rules; no permissive headings
     * or whitespace-collapsing mode.
     * Line endings: scan for LF; recognize CRLF by checking the preceding
     * byte. CR-only line endings are intentionally unsupported for now.
     */

    MdCursor cursor = {
        .source = source,
    };

    static const u8 candidates[] = {
        '\n', 0, '#', '>', '-', '+', '=', '`', '*', '_', '~', '|', '[', ']', '!', '<', '\\', '&'};
    SimdByteScanMany table = simd_byte_scan_many_init(candidates, sizeof(candidates));
    u8               buffer[64];
    u32              buffer_file_offset = 0;

    u32           l0_capacity = source->total_length;
    u8*           l0_tags     = scratch_alloc_aligned(scratch, u8, l0_capacity);
    u32*          l0_offsets  = scratch_alloc_aligned(scratch, u32, l0_capacity);
    u32*          l0_lengths  = scratch_alloc_aligned(scratch, u32, l0_capacity);
    MdL0Metadata* l0_metadata = scratch_alloc_aligned(scratch, MdL0Metadata, l0_capacity);
    u32           l0_count    = 0;

    u32  l1_capacity = source->total_length;
    u8*  l1_tags     = scratch_alloc_aligned(scratch, u8, l1_capacity);
    u32* l1_offsets  = scratch_alloc_aligned(scratch, u32, l1_capacity);
    u32* l1_lengths  = scratch_alloc_aligned(scratch, u32, l1_capacity);
    u32  l1_count    = 0;

    MdMap* mappings       = scratch_alloc_aligned(scratch, MdMap, source->total_length);
    u32    mappings_count = 0;

    u32 line_begin = 0;
    u32 line_idx   = 0;
    u32 indent     = 0;
    u8  previous   = 0;

    u8           line_tag      = 0;
    MdL0Metadata line_metadata = {0};

    while (buffer_file_offset < source->total_length) {
        memset(buffer, 0, 64);

        u32 buffer_bytes_read = md_fill(&cursor, buffer);
        u64 hits              = 0;

        simd_byte_scan_many(buffer, 64, &table, &hits);

        hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);

        u32 whitespace_index = 0;
        if (buffer_file_offset == 0)
            goto line_indent;

        while (hits) {
            u32 index = (u32)__builtin_ctzll(hits);
            hits &= hits - 1;
            u8  byte   = buffer[index];
            u32 offset = buffer_file_offset + index;

            // TODO handle code blocks in block comments, list items
            if (line_metadata.marker_length > 0 &&
                line_idx == line_metadata.marker_offset + line_metadata.marker_length &&
                (offset != line_idx || byte != line_tag)) {
                if ((line_tag == '`' || line_tag == '~') && line_metadata.marker_length < 3) {
                    l1_tags[l1_count]    = line_tag;
                    l1_offsets[l1_count] = line_metadata.marker_offset;
                    l1_lengths[l1_count] = line_metadata.marker_length;
                    l1_count += 1;
                    line_tag      = 0;
                    line_metadata = (MdL0Metadata){0};
                }
            }

            // Can apply anywhere
            if (byte == '\n') {
                u32 content_end = offset;
                content_end -= (index ? buffer[index - 1] : previous) == '\r';
                l0_tags[l0_count]     = line_tag;
                l0_offsets[l0_count]  = line_begin;
                l0_lengths[l0_count]  = content_end - line_begin;
                line_metadata.indent  = indent;
                l0_metadata[l0_count] = line_metadata;
                l0_count += 1;

                line_begin       = offset + 1;
                whitespace_index = index + 1;

            line_indent:
                line_idx      = line_begin;
                indent        = 0;
                line_tag      = 0; // line_tag == 0 is paragraph
                line_metadata = (MdL0Metadata){0};
                while (line_idx < source->total_length) {
                    while (whitespace_index < buffer_bytes_read) {
                        u8 indent_byte = buffer[whitespace_index];
                        if (indent_byte == ' ')
                            indent += 1;
                        else if (indent_byte == '\t')
                            indent += 4 - (indent & 3);
                        else
                            goto newline_end;

                        whitespace_index += 1;
                        line_idx += 1;
                    }
                    if (line_idx == source->total_length)
                        goto newline_end;

                    previous = buffer[buffer_bytes_read - 1];
                    buffer_file_offset += buffer_bytes_read;
                    memset(buffer, 0, 64);
                    buffer_bytes_read = md_fill(&cursor, buffer);
                    simd_byte_scan_many(buffer, 64, &table, &hits);
                    hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);
                    whitespace_index = 0;
                }

            newline_end:
                line_metadata.marker_offset = line_idx;
                continue;
            }

            if (line_idx == offset && line_idx == line_metadata.marker_offset + line_metadata.marker_length &&
                (line_metadata.marker_length == 0 || byte == line_tag)) {
                // Start of line, we only care about these at the start of a line
                switch (byte) {
                    case '-':
                    case '+':
                    case '*': {
                        if (line_metadata.marker_length > 0)
                            goto record_l0_marker;

                        line_tag                          = byte;
                        line_metadata.marker_offset       = offset;
                        line_metadata.marker_length       = 1;
                        line_metadata.trailing_whitespace = true;
                        line_metadata.body_indent         = indent + 1;
                        whitespace_index                  = index + 1;
                        u32 padding_offset                = offset + 1;

                        while (padding_offset < source->total_length) {
                            while (whitespace_index < buffer_bytes_read) {
                                u8 padding_byte = buffer[whitespace_index];
                                if (padding_byte == ' ')
                                    line_metadata.body_indent += 1;
                                else if (padding_byte == '\t')
                                    line_metadata.body_indent += 4 - (line_metadata.body_indent & 3);
                                else
                                    goto body_indent_end;
                                whitespace_index += 1;
                                padding_offset += 1;
                            }
                            if (padding_offset == source->total_length)
                                goto body_indent_end;

                            previous = buffer[buffer_bytes_read - 1];
                            buffer_file_offset += buffer_bytes_read;
                            memset(buffer, 0, 64);
                            buffer_bytes_read = md_fill(&cursor, buffer);
                            simd_byte_scan_many(buffer, 64, &table, &hits);
                            hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);
                            whitespace_index = 0;
                        }

                    body_indent_end:
                        if (line_metadata.body_indent - (indent + 1) < 1 ||
                            line_metadata.body_indent - (indent + 1) > 4)
                            line_metadata.body_indent = indent + 2;
                        // TODO validate list-marker padding and distinguish thematic breaks.
                        goto hit_end;
                    }
                    case '#':
                    case '>':
                    case '=':
                    case '_':
                    case '~':
                    case '`': {
                    record_l0_marker:
                        if (!line_metadata.marker_length) {
                            line_tag                    = byte;
                            line_metadata.marker_offset = offset;
                            line_metadata.marker_length = 1;
                        } else if (line_tag == byte) {
                            line_metadata.marker_length += 1;
                        }
                        line_metadata.trailing_whitespace = true;
                        // TODO clear trailing_whitespace for non-whitespace content after the marker.
                        goto hit_end;
                    }
                }
            }

            // l1 we always care about these
            switch (byte) {
                case 0:
                    mappings[mappings_count] = (MdMap){
                        .offset      = offset,
                        .length      = 1,
                        .kind        = MD_NODE_REPLACEMENT,
                        .text        = {0xef, 0xbf, 0xbd},
                        .text_length = 3,
                    };
                    mappings_count += 1;
                    break;
                case '`':
                case '*':
                case '_':
                case '~':
                case '|':
                case '[':
                case ']':
                case '!':
                case '<':
                case '\\':
                case '&': {
                    if (l1_count > 0 && l1_tags[l1_count - 1] == byte &&
                        l1_offsets[l1_count - 1] + l1_lengths[l1_count - 1] == offset) {
                        l1_lengths[l1_count - 1] += 1;
                    } else {
                        l1_tags[l1_count]    = byte;
                        l1_offsets[l1_count] = offset;
                        l1_lengths[l1_count] = 1;
                        l1_count += 1;
                    }
                    break;
                }
            }
        hit_end:
            line_idx = offset + 1;
        }

        if (buffer_bytes_read > 0)
            previous = buffer[buffer_bytes_read - 1];
        buffer_file_offset += buffer_bytes_read;
    }

    if (line_begin < source->total_length) {
        if ((line_tag == '`' || line_tag == '~') && line_metadata.marker_length < 3) {
            l1_tags[l1_count]    = line_tag;
            l1_offsets[l1_count] = line_metadata.marker_offset;
            l1_lengths[l1_count] = line_metadata.marker_length;
            l1_count += 1;
            line_tag      = 0;
            line_metadata = (MdL0Metadata){0};
        }
        l0_tags[l0_count]     = line_tag;
        l0_offsets[l0_count]  = line_begin;
        l0_lengths[l0_count]  = source->total_length - line_begin;
        line_metadata.indent  = indent;
        l0_metadata[l0_count] = line_metadata;
        l0_count += 1;
    }

    MdState result = {
        .l0_count       = l0_count,
        .l1_count       = l1_count,
        .mappings_count = mappings_count,
    };

    // TODO classify remaining L0 markers and resolve L1 candidates.
    if (result.l0_count) {
        u32 count = result.l0_count;

        result.l0_tags = arena_alloc_aligned(arena, u8, count);
        memcpy(result.l0_tags, l0_tags, count * sizeof(u8));

        result.l0_offsets = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l0_offsets, l0_offsets, count * sizeof(u32));

        result.l0_lengths = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l0_lengths, l0_lengths, count * sizeof(u32));

        result.l0_metadata = arena_alloc_aligned(arena, MdL0Metadata, count);
        memcpy(result.l0_metadata, l0_metadata, count * sizeof(MdL0Metadata));
    }

    if (result.l1_count) {
        u32 count = result.l1_count;

        result.l1_tags = arena_alloc_aligned(arena, u8, count);
        memcpy(result.l1_tags, l1_tags, count * sizeof(u8));

        result.l1_offsets = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l1_offsets, l1_offsets, count * sizeof(u32));

        result.l1_lengths = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l1_lengths, l1_lengths, count * sizeof(u32));
    }

    if (result.mappings_count) {
        u32 count       = result.mappings_count;
        result.mappings = arena_alloc_aligned(arena, MdMap, count);
        memcpy(result.mappings, mappings, count * sizeof(MdMap));
    }

    return result;
}

void md_update(rop(rw MdState) state,
               rop(rw ScratchArena) scratch,
               rop(ro MdView) source,
               u32 length,
               u32 offset,
               u32 removed_length);

MdResult md_get(rop(rw MdState) state, rop(rw Arena) arena, rop(rw ScratchArena) scratch, u32 begin, u32 end) {
    assert(SCOPE_MARKDOWN, end > begin);

    static const u8  context_tags[] = {'`', '~', '>', '-', '+', '*'};
    SimdByteScanMany context_table  = simd_byte_scan_many_init(context_tags, sizeof(context_tags));

    static const u8  query_tags[] = {0, '#', '>', '-', '+', '=', '`', '*', '_', '~'};
    SimdByteScanMany query_table  = simd_byte_scan_many_init(query_tags, sizeof(query_tags));

    static const u8  backtick_tag[] = {'`'};
    SimdByteScanMany backtick_table = simd_byte_scan_many_init(backtick_tag, sizeof(backtick_tag));

    static const u8  tilde_tag[] = {'~'};
    SimdByteScanMany tilde_table = simd_byte_scan_many_init(tilde_tag, sizeof(tilde_tag));

    static const u8  inline_tags[] = {'`', '*', '_', '~', '|', '[', ']', '!', '<', '\\', '&'};
    SimdByteScanMany inline_table  = simd_byte_scan_many_init(inline_tags, sizeof(inline_tags));

    usize   capacity    = (usize)state->l0_count + state->l1_count + state->mappings_count;
    MdNode* nodes       = scratch_alloc_aligned(scratch, MdNode, capacity);
    u32     nodes_count = 0;

    u32* l0_stack       = scratch_alloc_aligned(scratch, u32, state->l0_count);
    u32  l0_stack_count = 0;
    u32  fence_open     = false;
    u32  fence_idx      = 0;

    u32 l1_idx = 0;

    rwp(ro SimdByteScanMany) current_mask = &context_table;

    u8  buffer[64];
    u32 buffer_l0_idx = 0;

    while (buffer_l0_idx < state->l0_count) {
        u32 buffer_bytes_read = min(64, state->l0_count - buffer_l0_idx);
        u32 last              = buffer_l0_idx + buffer_bytes_read - 1;
        if (!fence_open && state->l0_offsets[last] + state->l0_lengths[last] >= begin)
            current_mask = &query_table;

        memset(buffer, 0, sizeof(buffer));
        memcpy(buffer, state->l0_tags + buffer_l0_idx, buffer_bytes_read);
        u64 hits = 0;
        simd_byte_scan_many(buffer, 64, current_mask, &hits);
        hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);

        while (hits) {
            u32 index = (u32)__builtin_ctzll(hits);
            hits &= hits - 1;

            u32 l0_idx    = buffer_l0_idx + index;
            u32 l0_offset = state->l0_offsets[l0_idx];
            if (l0_offset >= end) // TODO need to set a flag then only stop on next l0 because of setext header
                goto get_end;

            u8           l0_tag      = buffer[index];
            u32          l0_length   = state->l0_lengths[l0_idx];
            MdL0Metadata l0_metadata = state->l0_metadata[l0_idx];
            u32          l0_end      = l0_offset + l0_length;

            u32 base_indent = 0;
            if (l0_stack_count > 0) {
                u32 container_idx = l0_stack[l0_stack_count - 1];
                base_indent       = state->l0_metadata[container_idx].body_indent;
            }

            switch (l0_tag) {
                case '`':
                    if (!fence_open) {
                        // TODO validate indentation and info-string restrictions.
                        fence_idx    = l0_idx;
                        fence_open   = true;
                        current_mask = &backtick_table;
                        simd_byte_scan_many(buffer, 64, current_mask, &hits);
                        hits &= (UINT64_MAX << index) & ~(1ULL << index);
                        hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);
                    } else {
                        MdL0Metadata opening = state->l0_metadata[fence_idx];
                        if (l0_metadata.marker_length < opening.marker_length)
                            continue;
                        // TODO validate closing indentation and trailing whitespace, then close the fence.
                    }
                    continue;
                case '~':
                    if (!fence_open) {
                        // TODO validate indentation and info-string restrictions.
                        fence_idx    = l0_idx;
                        fence_open   = true;
                        current_mask = &tilde_table;
                        simd_byte_scan_many(buffer, 64, current_mask, &hits);
                        hits &= (UINT64_MAX << index) & ~(1ULL << index);
                        hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);
                    } else {
                        MdL0Metadata opening = state->l0_metadata[fence_idx];
                        if (l0_metadata.marker_length < opening.marker_length)
                            continue;
                        // TODO validate closing indentation and trailing whitespace, then close the fence.
                    }
                    continue;
                case '>':
                    // TODO update quote containers in the L0 stack.
                    break;
                case '-':
                case '+':
                case '*':
                    // TODO populate body_indent from marker width and padding, then update the L0 stack.
                    break;
                case 0:
                case '#':
                case '=':
                case '_':
                    // TODO match active container continuations.
                    break;
            }
            // TODO examine skipped lines where container continuation requires it.
            if (l0_end <= begin)
                continue;

            MdNode node = {
                .start_idx      = l0_offset,
                .end_idx        = l0_end,
                .content_length = l0_length,
            };
            // TODO interpret and store intersecting L0 nodes.

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
            // TODO process this line's mappings.
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

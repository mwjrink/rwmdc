#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/intrinsics.h>
#include <lib/grim/mem/arena.h>

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
typedef struct MdL0Metadata MdL0Metadata;
typedef struct MdL1Metadata MdL1Metadata;

typedef struct MdMap MdMap;

typedef struct MdState {
    // Parallel arrays: index i identifies one delimiter occurrence within
    // its layer. Tags are encoded byte-sized kinds; offsets are absolute
    // source-byte positions. Metadata holds length, role, and semantic data.
    // Each layer's arrays share a count and are ordered by source position.

    // L0: block/container delimiter occurrences and implicit boundaries.
    // Nested constructs share the same ordered stream; no tree pointers.
    rwp(rw u8) l0_tags;
    rwp(rw u32) l0_offsets;
    rwp(rw MdL0Metadata) l0_metadata;
    u32 l0_count;

    // L1: inline delimiter occurrences with resolved roles inside L0 blocks.
    // Tags can be searched without loading offsets or metadata.
    rwp(rw u8) l1_tags;
    rwp(rw u32) l1_offsets;
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

    static const u8  candidates[] = {'\n', 0, '`', '*', '_', '~', '|', '[', ']', '!', '<', '\\', '&'};
    SimdByteScanMany table        = simd_byte_scan_many_init(candidates, sizeof(candidates));
    u8               buffer[64];
    u32              buffer_file_offset = 0;

    enum {
        MD_STATE_LINE_START = 1 << 0,
        MD_STATE_CODE_BLOCK = 1 << 1,
    };
    u32 state = MD_STATE_LINE_START;

    u32 line_begin = 0;
    u32 indent     = 0;
    u8  previous   = 0;

    while (buffer_file_offset < source->total_length) {
        memset(buffer, 0, 64);

        u32 buffer_bytes_read = md_fill(&cursor, buffer);
        u64 hits              = 0;

        simd_byte_scan_many(buffer, 64, &table, &hits);

        hits &= UINT64_MAX >> ((64 - buffer_bytes_read) & 63);

        while (hits) {
            u32 index = (u32)__builtin_ctzll(hits);
            hits &= hits - 1;
            u8  byte   = buffer[index];
            u32 offset = buffer_file_offset + index;

            switch (byte) {
                case '\n': {
                    u32 content_end = offset;
                    content_end -= (index ? buffer[index - 1] : previous) == '\r';
                    // TODO: finish L0 line [line_begin, content_end).
                    (void)content_end;
                    line_begin = offset + 1;
                    state |= MD_STATE_LINE_START;
                    break;
                }
                case 0:
                    // TODO: record NUL replacement mapping.
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
                case '&':
                    // TODO: handle L0 markers or record L1 candidate.
                    break;
            }
        }

        if (buffer_bytes_read)
            previous = buffer[buffer_bytes_read - 1];
        buffer_file_offset += buffer_bytes_read;
    }

    // TODO: finish final unterminated line and close L0 blocks.
    // TODO: resolve L1 candidates and publish durable state.
    return (MdState){0};
}

void md_update(rop(rw MdState) state,
               rop(rw ScratchArena) scratch,
               rop(ro MdView) source,
               u32 length,
               u32 offset,
               u32 removed_length);

MdResult md_get(rop(rw MdState) state, rop(rw Arena) arena, rop(rw ScratchArena) scratch, u32 begin, u32 end);

void md_destroy(rop(rw MdState) state);

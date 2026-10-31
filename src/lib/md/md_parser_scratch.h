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

// Semantic tags stored as u8; source delimiters live in marker_byte.
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

// Delimiter-specific payload layouts are specified separately.
typedef struct MdL0Metadata {
    u32 pre_indent;
    u32 post_indent;

    u32 marker_offset       : 16; // Source bytes from the L0 boundary.
    u32 marker_length       : 15;
    u32 trailing_whitespace : 1;

    union {
        u32 marker_padding; // Bytes of space/tab after the marker.
        u32 line_count;     // Physical lines in a blank run.
    };

    u8 marker_byte; // Source delimiter, independent of the semantic L0 tag.

} MdL0Metadata;
STATIC_ASSERT(sizeof(MdL0Metadata) == 20);

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
    rwp(rw MdL0Metadata) l0_metadata;
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

// Only tracks the next source byte to copy. Scanning uses the filled buffer.
typedef struct MdCursor {
    const DocView* source;
    u32            piece;
    u32            piece_offset;
} MdCursor;

internal u32 md_fill(MdCursor* cursor, u8* buffer, u32 capacity) {
    u32 filled = 0;
    while (filled < capacity && cursor->piece < cursor->source->count) {
        const DocPiece* piece     = &cursor->source->pieces[cursor->piece];
        u32             available = piece->length - cursor->piece_offset;
        u32             take      = min(available, capacity - filled);

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

// MdState md_create(rop(ro DocView) source, rop(rw Arena) arena, rop(rw ScratchArena) scratch) {
//     /* Fixed dialect: CommonMark 0.31.2 plus tables, strikethrough, task lists,
//      * wikilinks, spoilers, and admonitions. No feature-selection flags.
//      *
//      * Strikethrough requires ~~ delimiters; a single ~ is not strikethrough.
//      * Spoilers require || delimiters and follow the inline emphasis/strong
//      * model (like **). Spoiler syntax is disabled inside tables.
//      *
//      * No bare URL/www/email autolinks or footnotes. Standard CommonMark
//      * angle-bracket autolinks remain supported.
//      * No math, underline, superscript/subscript, highlight, or insert syntax.
//      * Keep CommonMark heading and whitespace rules; no permissive headings
//      * or whitespace-collapsing mode.
//      * Line endings: scan for LF; recognize CRLF by checking the preceding
//      * byte. CR-only line endings are intentionally unsupported for now.
//      */
//
//     MdCursor cursor = {
//         .source = source,
//     };
//
//     static const u8  candidates[]    = {'\n', 0,   '\t', '\r', ' ', '#', '>', '-', '+',  '=', '`',
//                                         '*',  '_', '~',  '|',  '[', ']', '!', '<', '\\', '&'};
//     SimdByteScanMany table           = simd_byte_scan_many_init(candidates, sizeof(candidates));
//     SimdByteRunCarry run_carry       = {0};
//     u32              run_start       = 0;
//     u8               run_tag         = 0;
//     u8               run_left_useful = 1;
//
//     const u32 buffer_capacity    = 4096;
//     u8*       buffer             = scratch_alloc_aligned(scratch, u8, buffer_capacity);
//     u8*       classified         = scratch_alloc_aligned(scratch, u8, buffer_capacity);
//     u32*      starts             = scratch_alloc_aligned(scratch, u32, buffer_capacity);
//     u32       buffer_file_offset = 0;
//
//     u32           l0_capacity = source->total_length;
//     u8*           l0_tags     = scratch_alloc_aligned(scratch, u8, l0_capacity);
//     u32*          l0_offsets  = scratch_alloc_aligned(scratch, u32, l0_capacity);
//     u32*          l0_lengths  = scratch_alloc_aligned(scratch, u32, l0_capacity);
//     MdL0Metadata* l0_metadata = scratch_alloc_aligned(scratch, MdL0Metadata, l0_capacity);
//     u32           l0_count    = 0;
//
//     u32  l1_capacity = source->total_length;
//     u8*  l1_tags     = scratch_alloc_aligned(scratch, u8, l1_capacity);
//     u32* l1_offsets  = scratch_alloc_aligned(scratch, u32, l1_capacity);
//     u32* l1_lengths  = scratch_alloc_aligned(scratch, u32, l1_capacity);
//     u32  l1_count    = 0;
//
//     MdMap* mappings       = scratch_alloc_aligned(scratch, MdMap, source->total_length);
//     u32    mappings_count = 0;
//
//     u32 line_begin = 0;
//     u32 line_idx   = 0;
//     u32 indent     = 0;
//     u8  previous   = 0;
//
//     u8           line_tag      = MD_L0_PARAGRAPH;
//     MdL0Metadata line_metadata = {0};
//
//     while (buffer_file_offset < source->total_length) {
//         u32 buffer_bytes_read = md_fill(&cursor, buffer, buffer_capacity);
//         u32 start_count       = simd_byte_scan_runs(buffer, buffer_bytes_read, &table, &run_carry, classified,
//         starts);
//
//         u32 start_index      = 0;
//         u32 whitespace_index = 0;
//         if (buffer_file_offset == 0)
//             goto line_indent;
//
//         while (start_index < start_count) {
//             u32 index  = starts[start_index++];
//             u8  byte   = buffer[index];
//             u32 offset = buffer_file_offset + index;
//
//             // TODO handle code blocks in block comments, list items
//             if (line_metadata.marker_length > 0 &&
//                 line_idx == line_metadata.marker_offset + line_metadata.marker_length &&
//                 (offset != line_idx || byte != line_metadata.marker_byte)) {
//                 if ((line_metadata.marker_byte == '`' || line_metadata.marker_byte == '~' ||
//                      line_metadata.marker_byte == '_') &&
//                     line_metadata.marker_length < 3) {
//                     l1_tags[l1_count]    = line_metadata.marker_byte;
//                     l1_offsets[l1_count] = line_metadata.marker_offset;
//                     l1_lengths[l1_count] = line_metadata.marker_length;
//                     l1_count += 1;
//                     line_tag      = MD_L0_PARAGRAPH;
//                     line_metadata = (MdL0Metadata){0};
//                 }
//             }
//
//             // Can apply anywhere
//             if (byte == '\n') {
//                 u32 content_end = offset;
//                 content_end -= ((index > 0 && buffer[index - 1] == '\r') || (index == 0 && previous == '\r'));
//
//                 if (line_tag == MD_L0_PARAGRAPH && line_metadata.marker_offset >= content_end)
//                     line_tag = MD_L0_BLANK;
//
//                 if (line_tag == MD_L0_BLANK && l0_count > 0 && l0_tags[l0_count - 1] == MD_L0_BLANK) {
//                     l0_lengths[l0_count - 1] = content_end - l0_offsets[l0_count - 1];
//                     l0_metadata[l0_count - 1].line_count += 1;
//                 } else {
//                     l0_tags[l0_count]        = line_tag;
//                     l0_offsets[l0_count]     = line_begin;
//                     l0_lengths[l0_count]     = content_end - line_begin;
//                     line_metadata.indent     = indent;
//                     line_metadata.line_count = 1;
//                     l0_metadata[l0_count]    = line_metadata;
//                     l0_count += 1;
//                 }
//
//                 line_begin       = offset + 1;
//                 whitespace_index = index + 1;
//
//             line_indent:
//                 line_idx      = line_begin;
//                 indent        = 0;
//                 line_tag      = MD_L0_PARAGRAPH;
//                 line_metadata = (MdL0Metadata){0};
//                 while (line_idx < source->total_length) {
//                     while (whitespace_index < buffer_bytes_read) {
//                         u8 indent_byte = buffer[whitespace_index];
//                         if (indent_byte != ' ' && indent_byte != '\t')
//                             goto newline_end;
//
//                         while (start_index < start_count && starts[start_index] <= whitespace_index)
//                             start_index += 1;
//                         u32 whitespace_end = buffer_bytes_read;
//                         if (start_index < start_count)
//                             whitespace_end = starts[start_index];
//                         u32 length = whitespace_end - whitespace_index;
//
//                         if (indent_byte == ' ')
//                             indent += length;
//                         else
//                             indent = ((indent >> 2) + length) << 2;
//
//                         whitespace_index = whitespace_end;
//                         line_idx += length;
//                     }
//                     if (line_idx == source->total_length)
//                         goto newline_end;
//
//                     previous = buffer[buffer_bytes_read - 1];
//                     buffer_file_offset += buffer_bytes_read;
//                     buffer_bytes_read = md_fill(&cursor, buffer, buffer_capacity);
//                     start_count =
//                         simd_byte_scan_runs(buffer, buffer_bytes_read, &table, &run_carry, classified, starts);
//                     start_index      = 0;
//                     whitespace_index = 0;
//                 }
//
//             newline_end:
//                 line_metadata.marker_offset = line_idx;
//                 continue;
//             }
//
//             if (line_idx == offset && line_idx == line_metadata.marker_offset + line_metadata.marker_length &&
//                 (line_metadata.marker_length == 0 || byte == line_metadata.marker_byte)) {
//                 // Start-of-line dispatch; handlers below share marker recording.
//                 if (byte <= '-') {
//                     if (byte == '#')
//                         goto heading_marker;
//                     if (byte == '-' || byte == '+' || byte == '*')
//                         goto list_marker;
//                     goto scan_l1;
//                 }
//                 if (byte <= '`') {
//                     if (byte >= '_') {
//                         if (!line_metadata.marker_length) {
//                             if (byte == '_')
//                                 line_tag = MD_L0_THEMATIC_BREAK;
//                             else
//                                 line_tag = MD_L0_FENCED_CODE_BLOCK;
//                         }
//                         goto record_l0_marker;
//                     }
//                     if (byte == '=' || byte == '>') {
//                         if (!line_metadata.marker_length) {
//                             if (byte == '>')
//                                 line_tag = MD_L0_BLOCK_QUOTE;
//                             else
//                                 line_tag = MD_L0_SETEXT_HEADING;
//                         }
//                         goto record_l0_marker;
//                     }
//                     goto scan_l1;
//                 }
//                 if (byte != '~')
//                     goto scan_l1;
//                 if (!line_metadata.marker_length)
//                     line_tag = MD_L0_FENCED_CODE_BLOCK;
//                 goto record_l0_marker;
//
//             list_marker:
//                 {
//                     if (line_metadata.marker_length > 0) {
//                         setif_u8(line_metadata.marker_length >= 2 && byte != '+', line_tag, MD_L0_THEMATIC_BREAK);
//                         goto record_l0_marker;
//                     }
//
//                     line_tag                          = MD_L0_UNORDERED_LIST;
//                     line_metadata.marker_byte         = byte;
//                     line_metadata.marker_offset       = offset;
//                     line_metadata.marker_length       = 1;
//                     line_metadata.trailing_whitespace = true;
//                     line_metadata.post_indent         = indent + 1;
//                     whitespace_index                  = index + 1;
//                     u32 padding_offset                = offset + 1;
//
//                     while (padding_offset < source->total_length) {
//                         while (whitespace_index < buffer_bytes_read) {
//                             u8 padding_byte = buffer[whitespace_index];
//                             if (!line_metadata.marker_padding && padding_byte == byte && byte != '+')
//                                 goto hit_end;         // Continue counting a dash/star run.
//                             if (padding_byte == '\n' || padding_byte == '\r')
//                                 goto body_indent_end; // Empty items are valid.
//                             if (padding_byte == ' ')
//                                 line_metadata.post_indent += 1;
//                             else if (padding_byte == '\t')
//                                 line_metadata.post_indent += 4 - (line_metadata.post_indent & 3);
//                             else if (!line_metadata.marker_padding) {
//                                 line_tag      = MD_L0_PARAGRAPH;
//                                 line_metadata = (MdL0Metadata){0};
//                                 goto scan_l1;
//                             } else {
//                                 goto body_indent_end;
//                             }
//                             line_metadata.marker_padding += 1;
//                             whitespace_index += 1;
//                             padding_offset += 1;
//                         }
//                         if (padding_offset == source->total_length)
//                             goto body_indent_end;
//
//                         previous = buffer[buffer_bytes_read - 1];
//                         buffer_file_offset += buffer_bytes_read;
//                         buffer_bytes_read = md_fill(&cursor, buffer, buffer_capacity);
//                         start_count =
//                             simd_byte_scan_runs(buffer, buffer_bytes_read, &table, &run_carry, classified, starts);
//                         start_index      = 0;
//                         whitespace_index = 0;
//                     }
//
//                 body_indent_end:
//                     if (line_metadata.post_indent - (indent + 1) < 1 || line_metadata.post_indent - (indent + 1) > 4)
//                         line_metadata.post_indent = indent + 2;
//                     // TODO validate list-marker padding and distinguish thematic breaks.
//                     goto hit_end;
//                 }
//             heading_marker:
//                 {
//                     if (line_metadata.marker_length == 6) {
//                         line_tag = MD_L0_PARAGRAPH;
//                         goto hit_end;
//                     }
//                     line_tag                  = MD_L0_ATX_HEADING;
//                     line_metadata.marker_byte = byte;
//
//                     setif(line_metadata.marker_length == 0, line_metadata.marker_offset, offset);
//
//                     line_metadata.marker_length += 1;
//                     line_metadata.trailing_whitespace = true;
//                     line_metadata.post_indent         = indent + line_metadata.marker_length;
//                     whitespace_index                  = index + 1;
//                     u32  padding_offset               = offset + 1;
//                     bool heading_cr                   = false;
//
//                     while (padding_offset < source->total_length) {
//                         while (whitespace_index < buffer_bytes_read) {
//                             u8 padding_byte = buffer[whitespace_index];
//                             if (heading_cr) {
//                                 if (padding_byte != '\n')
//                                     line_tag = MD_L0_PARAGRAPH;
//                                 goto heading_padding_end;
//                             }
//                             if (padding_byte == ' ')
//                                 line_metadata.post_indent += 1;
//                             else if (padding_byte == '\t')
//                                 line_metadata.post_indent += 4 - (line_metadata.post_indent & 3);
//                             else if (padding_byte == '\r' && !line_metadata.marker_padding)
//                                 heading_cr = true;
//                             else {
//                                 if (!line_metadata.marker_padding && padding_byte != '#' && padding_byte != '\n')
//                                     line_tag = MD_L0_PARAGRAPH;
//                                 goto heading_padding_end;
//                             }
//                             if (!heading_cr)
//                                 line_metadata.marker_padding += 1;
//                             whitespace_index += 1;
//                             padding_offset += 1;
//                         }
//                         if (padding_offset == source->total_length)
//                             goto heading_padding_end;
//
//                         previous = buffer[buffer_bytes_read - 1];
//                         buffer_file_offset += buffer_bytes_read;
//                         buffer_bytes_read = md_fill(&cursor, buffer, buffer_capacity);
//                         start_count =
//                             simd_byte_scan_runs(buffer, buffer_bytes_read, &table, &run_carry, classified, starts);
//                         start_index      = 0;
//                         whitespace_index = 0;
//                     }
//
//                 heading_padding_end:
//                     // setif(cond, line_tag, 0)
//                     line_tag *= !(heading_cr && padding_offset == source->total_length);
//
//                     while (start_index < start_count && starts[start_index] < whitespace_index)
//                         start_index += 1;
//
//                     line_idx = padding_offset;
//                     continue;
//                 }
//             record_l0_marker:
//                 line_metadata.marker_byte = byte;
//                 setif(line_metadata.marker_length == 0, line_metadata.marker_offset, offset);
//                 line_metadata.marker_length += 1;
//                 line_metadata.trailing_whitespace = true;
//                 // TODO clear trailing_whitespace for non-whitespace content after the marker.
//                 goto hit_end;
//             }
//
//         scan_l1:
//             // l1 we always care about these
//             switch (byte) {
//                 case 0:
//                     mappings[mappings_count] = (MdMap){
//                         .offset      = offset,
//                         .length      = 1,
//                         .kind        = MD_NODE_REPLACEMENT,
//                         .text        = {0xef, 0xbf, 0xbd},
//                         .text_length = 3,
//                     };
//                     mappings_count += 1;
//                     break;
//                 case '`':
//                 case '*':
//                 case '_':
//                 case '~':
//                 case '|':
//                 case '[':
//                 case ']':
//                 case '!':
//                 case '<':
//                 case '\\':
//                 case '&': {
//                     if (l1_count > 0 && l1_tags[l1_count - 1] == byte &&
//                         l1_offsets[l1_count - 1] + l1_lengths[l1_count - 1] == offset) {
//                         l1_lengths[l1_count - 1] += 1;
//                     } else {
//                         l1_tags[l1_count]    = byte;
//                         l1_offsets[l1_count] = offset;
//                         l1_lengths[l1_count] = 1;
//                         l1_count += 1;
//                     }
//                     break;
//                 }
//             }
//         hit_end:
//             line_idx = offset + 1;
//         }
//
//         if (buffer_bytes_read > 0)
//             previous = buffer[buffer_bytes_read - 1];
//         buffer_file_offset += buffer_bytes_read;
//     }
//
//     if (line_begin < source->total_length) {
//         if ((line_metadata.marker_byte == '`' || line_metadata.marker_byte == '~' ||
//              line_metadata.marker_byte == '_') &&
//             line_metadata.marker_length < 3) {
//             l1_tags[l1_count]    = line_metadata.marker_byte;
//             l1_offsets[l1_count] = line_metadata.marker_offset;
//             l1_lengths[l1_count] = line_metadata.marker_length;
//             l1_count += 1;
//             line_tag      = MD_L0_PARAGRAPH;
//             line_metadata = (MdL0Metadata){0};
//         }
//         if (line_tag == MD_L0_PARAGRAPH && line_metadata.marker_offset >= source->total_length)
//             line_tag = MD_L0_BLANK;
//         if (line_tag == MD_L0_BLANK && l0_count > 0 && l0_tags[l0_count - 1] == MD_L0_BLANK) {
//             l0_lengths[l0_count - 1] = source->total_length - l0_offsets[l0_count - 1];
//             l0_metadata[l0_count - 1].line_count += 1;
//         } else {
//             l0_tags[l0_count]        = line_tag;
//             l0_offsets[l0_count]     = line_begin;
//             l0_lengths[l0_count]     = source->total_length - line_begin;
//             line_metadata.pre_indent = indent;
//             line_metadata.line_count = 1;
//             l0_metadata[l0_count]    = line_metadata;
//             l0_count += 1;
//         }
//     }
//
//     MdState result = {
//         .l0_count       = l0_count,
//         .l1_count       = l1_count,
//         .mappings_count = mappings_count,
//     };
//
//     // TODO classify remaining L0 markers and resolve L1 candidates.
//     if (result.l0_count) {
//         u32 count = result.l0_count;
//
//         result.l0_tags = arena_alloc_aligned(arena, u8, count);
//         memcpy(result.l0_tags, l0_tags, count * sizeof(u8));
//
//         result.l0_offsets = arena_alloc_aligned(arena, u32, count);
//         memcpy(result.l0_offsets, l0_offsets, count * sizeof(u32));
//
//         result.l0_lengths = arena_alloc_aligned(arena, u32, count);
//         memcpy(result.l0_lengths, l0_lengths, count * sizeof(u32));
//
//         result.l0_metadata = arena_alloc_aligned(arena, MdL0Metadata, count);
//         memcpy(result.l0_metadata, l0_metadata, count * sizeof(MdL0Metadata));
//     }
//
//     if (result.l1_count) {
//         u32 count = result.l1_count;
//
//         result.l1_tags = arena_alloc_aligned(arena, u8, count);
//         memcpy(result.l1_tags, l1_tags, count * sizeof(u8));
//
//         result.l1_offsets = arena_alloc_aligned(arena, u32, count);
//         memcpy(result.l1_offsets, l1_offsets, count * sizeof(u32));
//
//         result.l1_lengths = arena_alloc_aligned(arena, u32, count);
//         memcpy(result.l1_lengths, l1_lengths, count * sizeof(u32));
//     }
//
//     if (result.mappings_count) {
//         u32 count       = result.mappings_count;
//         result.mappings = arena_alloc_aligned(arena, MdMap, count);
//         memcpy(result.mappings, mappings, count * sizeof(MdMap));
//     }
//
//     return result;
// }

MdState md_create_v2(rop(ro DocView) source, rop(rw Arena) arena, rop(rw ScratchArena) scratch) {
    MdCursor         cursor       = {.source = source};
    static const u8  candidates[] = {'\n', 0,   '\t', '\r', ' ', '#', '>', '-', '+',  '=', '`',
                                     '*',  '_', '~',  '|',  '[', ']', '!', '<', '\\', '&'};
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

    const u32 buffer_capacity    = 4096;
    u8*       buffer             = scratch_alloc_aligned(scratch, u8, buffer_capacity);
    u8*       lexical_tags       = scratch_alloc_aligned(scratch, u8, buffer_capacity);
    u32*      run_offsets        = scratch_alloc_aligned(scratch, u32, buffer_capacity);
    u32       buffer_file_offset = 0;
    u32       buffer_length      = 0;
    u32       run_count          = 0;
    u32       run_index          = 0;
    u32       run_offset         = 0;
    u8        run_tag            = 0;

    usize         l0_capacity = (usize)source->total_length + 1;
    u8*           l0_tags     = scratch_alloc_aligned(scratch, u8, l0_capacity);
    u32*          l0_offsets  = scratch_alloc_aligned(scratch, u32, l0_capacity);
    MdL0Metadata* l0_metadata = scratch_alloc_aligned(scratch, MdL0Metadata, l0_capacity);
    u32           l0_count    = 0;

    u32  l1_capacity = source->total_length;
    u8*  l1_tags     = scratch_alloc_aligned(scratch, u8, l1_capacity);
    u32* l1_offsets  = scratch_alloc_aligned(scratch, u32, l1_capacity);
    u32* l1_lengths  = scratch_alloc_aligned(scratch, u32, l1_capacity);
    u32  l1_count    = 0;

    MdMap* mappings       = scratch_alloc_aligned(scratch, MdMap, source->total_length);
    u32    mappings_count = 0;

    u32          line_offset      = 0;
    u32          line_indent      = 0;
    u32          blank_line_count = 0;
    u32          at_line_start    = true;
    u8           line_tag;
    MdL0Metadata line_metadata;

    l0_offsets[0] = 0;
    if (likely(source->total_length > 0)) {
        buffer_length = md_fill(&cursor, buffer, min(buffer_capacity, source->total_length));
        run_count     = simd_byte_scan_runs(buffer, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
        run_tag       = lexical_tags[0];
        run_index     = 1;
    }

    // Every iteration, we process the previous run because then we know it's length.
    while (run_offset < source->total_length) {
        // Continuations may emit no starts; refill until a boundary or EOF.
        while (run_index == run_count && buffer_file_offset + buffer_length < source->total_length) {
            buffer_file_offset += buffer_length;
            u32 remaining = source->total_length - buffer_file_offset;
            buffer_length = md_fill(&cursor, buffer, min(buffer_capacity, remaining));
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

        u32 run_length            = run_end - run_offset;
        u32 indent                = line_indent + run_length;
        u32 whitespace_run_length = 0;
        u8  l0_tag                = MD_L0_PARAGRAPH;
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

                if (!at_line_start) {
                    goto set_l0_metadata;
                }

                u8 l0_tag_lookup = l0_tag_table[run_tag];

                if (l0_tag_lookup == MD_L0_THEMATIC_BREAK || l0_tag_lookup == MD_L0_FENCED_CODE_BLOCK) {
                    if (run_length < 3) {
                        l0_tag_lookup = MD_L0_NONE;
                    }
                }

                if (l0_tag_lookup == MD_L0_ATX_HEADING) {
                    if (run_length > 6) {
                        l0_tag_lookup = MD_L0_NONE;
                    }
                }

                if (run_tag == '-' || run_tag == '*') {
                    if (run_length >= 3)
                        l0_tag_lookup = MD_L0_THEMATIC_BREAK;
                }

                if (l0_tag_lookup != MD_L0_NONE) {
                    l0_tag = l0_tag_lookup;
                }
            }
        } else {
            if (run_tag == '\n') {
                if (at_line_start) {
                    blank_line_count += run_length;
                } else {
                    if (blank_line_count > 0) {
                        l0_tags[l0_count]     = MD_L0_BLANK;
                        l0_metadata[l0_count] = (MdL0Metadata){.line_count = blank_line_count};
                        l0_count += 1;
                        l0_offsets[l0_count] = line_offset;
                    }
                    l0_tags[l0_count]     = line_tag;
                    l0_metadata[l0_count] = line_metadata;
                    l0_count += 1;
                    l0_offsets[l0_count] = run_offset + 1;
                    blank_line_count     = run_length - 1;
                }
                line_offset   = run_end;
                l0_tag        = MD_L0_BLANK;
                line_indent   = 0;
                at_line_start = true;
                indent        = 0;
                goto next_run;
            } else if (run_tag == ' ' || run_tag == '\t') {
                l0_tag = MD_L0_BLANK;
                indent = line_indent;
                goto whitespace;
            }
        }

    set_l0_metadata:
        if (at_line_start) {
            assert(SCOPE_MARKDOWN, run_offset - line_offset <= UINT16_MAX);
            assert(SCOPE_MARKDOWN, run_length < (1u << 15) || l0_tag == MD_L0_PARAGRAPH);
            line_tag      = l0_tag;
            line_metadata = (MdL0Metadata){
                .marker_offset = run_offset - line_offset,
                .marker_byte   = run_tag,
                .marker_length = run_length * (l0_tag != MD_L0_PARAGRAPH),
                .pre_indent    = line_indent,
            };
        }

    next_run:
        run_offset = run_end;
        if (run_index < run_count) {
            run_tag = lexical_tags[run_index];
            run_index += 1;
        }

    whitespace:
        while (run_offset < source->total_length && (run_tag == ' ' || run_tag == '\t')) {
            while (run_index == run_count && buffer_file_offset + buffer_length < source->total_length) {
                buffer_file_offset += buffer_length;
                u32 remaining = source->total_length - buffer_file_offset;
                buffer_length = md_fill(&cursor, buffer, min(buffer_capacity, remaining));
                run_count = simd_byte_scan_runs(buffer, buffer_length, &table, &run_carry, lexical_tags, run_offsets);
                run_index = 0;
            }
            if (run_index < run_count)
                run_end = buffer_file_offset + run_offsets[run_index];
            else
                run_end = source->total_length;
            run_length = run_end - run_offset;

            whitespace_run_length += run_length;
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

        if (l0_tag == MD_L0_BLANK) {
            line_indent = indent;
            continue;
        }

        if (at_line_start) {
            line_metadata.marker_padding = whitespace_run_length;
            line_metadata.post_indent    = indent;
            at_line_start                = false;
        }
        line_metadata.trailing_whitespace = whitespace_run_length > 0;
    }

    // flush pending line
    if (!at_line_start) {
        if (blank_line_count > 0) {
            l0_tags[l0_count]     = MD_L0_BLANK;
            l0_metadata[l0_count] = (MdL0Metadata){.line_count = blank_line_count};
            l0_count += 1;
            l0_offsets[l0_count] = line_offset;
            blank_line_count     = 0;
        }
        l0_tags[l0_count]     = line_tag;
        l0_metadata[l0_count] = line_metadata;
        l0_count += 1;
        line_indent = 0;
    }

    // write EOF sentinel with pending trailing whitespace
    l0_offsets[l0_count]  = source->total_length;
    l0_tags[l0_count]     = MD_L0_EOF;
    l0_metadata[l0_count] = (MdL0Metadata){.pre_indent = line_indent, .line_count = blank_line_count};
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
    result.l0_metadata = arena_alloc_aligned(arena, MdL0Metadata, l0_count);
    memcpy(result.l0_metadata, l0_metadata, l0_count * sizeof(MdL0Metadata));

    if (likely(result.l1_count > 0)) {
        u32 count      = result.l1_count;
        result.l1_tags = arena_alloc_aligned(arena, u8, count);
        memcpy(result.l1_tags, l1_tags, count * sizeof(u8));
        result.l1_offsets = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l1_offsets, l1_offsets, count * sizeof(u32));
        result.l1_lengths = arena_alloc_aligned(arena, u32, count);
        memcpy(result.l1_lengths, l1_lengths, count * sizeof(u32));
    }

    if (likely(result.mappings_count > 0)) {
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

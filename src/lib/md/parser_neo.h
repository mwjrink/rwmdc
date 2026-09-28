#pragma once

/*
 * rwmd Markdown parser.
 *
 * Grammar routines are ported individually from the owned MD4C fork in
 * parser.h (upstream 00788b157b2b6ef3efd8659b66f6521229f382d7).
 *
 * Copyright (c) 2016-2026 Martin Mitáš
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <utf8proc.h>
#include "entities.h"

/* The authoritative public interface. Do not duplicate or redefine its types. */
#include "md_interface.h"

// =====================================================================================================================
// Persistent state
// =====================================================================================================================

typedef enum MdError {
    MD_ERROR_NONE,
    MD_ERROR_INVALID_VIEW,
    MD_ERROR_INVALID_EDIT,
    MD_ERROR_INVALID_RANGE,
    MD_ERROR_INVALID_OPTIONS,
    MD_ERROR_OVERFLOW,
    MD_ERROR_OUT_OF_MEMORY,
} MdError;

typedef struct MdId {
    u32 index;
    u32 generation;
} MdId;

typedef MdId MdBlockId;
typedef MdId MdLabelId;
typedef MdId MdDefinitionId;

#define MD_ID_NONE ((MdId){.index = UINT32_MAX})

typedef struct MdRange {
    u32 begin;
    u32 end;
} MdRange;

typedef struct MdLocation {
    MdBlockId owner;
    u32       offset;
} MdLocation;

typedef enum MdBlockKind {
    MD_BLOCK_ROOT,
    MD_BLOCK_TRIVIA,
    MD_BLOCK_DEFINITION,
    MD_BLOCK_PARAGRAPH,
    MD_BLOCK_HEADING,
    MD_BLOCK_QUOTE,
    MD_BLOCK_LIST,
    MD_BLOCK_ITEM,
    MD_BLOCK_CODE,
    MD_BLOCK_HTML,
    MD_BLOCK_THEMATIC_BREAK,
    MD_BLOCK_BLANK,
    MD_BLOCK_TABLE,
    MD_BLOCK_TABLE_HEAD,
    MD_BLOCK_TABLE_BODY,
    MD_BLOCK_TABLE_ROW,
    MD_BLOCK_TABLE_CELL,
    MD_BLOCK_ADMONITION,
    MD_BLOCK_FOOTNOTE,
} MdBlockKind;

/* All line ranges are relative to their owning leaf. No source pointers. */
typedef struct MdLine {
    MdRange source;
    MdRange content;
    u32     indent_columns;
} MdLine;

/* node ranges are block-local. Reference attributes resolve through the
 * label's winning definition; direct-link attributes are block-local too. */
typedef struct MdAnnotation {
    MdNode    node;
    MdLabelId reference;
} MdAnnotation;

typedef struct MdBlock {
    u32 generation;
    u32 next_free;
    u32 alive;

    MdBlockKind kind;
    MdBlockId   parent;
    u32         offset; /* Relative to parent. */
    u32         length;
    MdRange     content;
    u32         marker;
    u32         marker_indent;
    u32         content_indent;
    u32         depth;
    u32         flags;

    union {
        struct {
            u32 level;
        } heading;
        struct {
            u32 start;
            u32 loose_children;
            u32 ordered;
        } list;
        struct {
            u32 number;
            struct {
                u32 task    : 1;
                u32 checked : 1;
            };
        } item;
        struct {
            MdRange content;
            u32     fenced;
        } code;
        struct {
            u32 mode;
        } html;
        struct {
            u32 line_count;
        } blank;
        struct {
            u32 columns;
            u32 rows;
        } table;
        struct {
            u32         row;
            u32         column;
            MdAlignment alignment;
            u32         header;
        } cell;
        struct {
            MdAdmonition kind;
        } admonition;
        struct {
            MdDefinitionId definition;
        } definition;
    } data;

    MdBlockId* children;
    u32        children_count;

    MdLine* lines;
    u32     lines_count;

    MdAnnotation* annotations;
    u32           annotations_count;

    u32* annotation_max_ends;
    u32  annotation_max_ends_count;

    MdLabelId* reference_uses;
    u32        reference_uses_count;
} MdBlock;

typedef enum MdDefinitionKind {
    MD_DEFINITION_LINK,
    MD_DEFINITION_FOOTNOTE,
} MdDefinitionKind;

typedef struct MdDefinition {
    u32 generation;
    u32 next_free;
    u32 alive;

    MdDefinitionKind kind;
    MdBlockId        owner;
    MdLabelId        label;
    MdRange          source;
    MdRange          destination;
    MdRange          title;
    u32              has_title;
    u32              display_number;
    u32              reference_count;
} MdDefinition;

typedef struct MdReferenceUse {
    MdBlockId        owner;
    u32              offset;
    MdDefinitionKind kind;
} MdReferenceUse;

typedef struct MdLabel {
    u32 generation;
    u32 alive;

    MdDefinitionKind kind;
    u64              hash;
    u8*              key;
    u32              key_count;

    MdDefinitionId* definitions;
    u32             definitions_count;

    /* Includes failed lookups. A newly added definition can activate them. */
    MdReferenceUse* users;
    u32             users_count;

    MdDefinitionId winner;
} MdLabel;

typedef struct MdFootnoteOccurrence {
    MdDefinitionId definition;
    MdLocation     source;
    u32            occurrence;
} MdFootnoteOccurrence;

/* Retired durable allocations can be reused without rewinding the caller's
 * Arena. The syntax arrays themselves have only pointers and element counts. */
typedef struct MdAllocation {
    struct MdAllocation* next;
    usize                bytes;
    usize                alignment;
} MdAllocation;

struct MdState {
    Arena*  arena;
    MdError error;
    u32     flags;
    u32     source_length;
    u64     revision;

    MdBlock* blocks;
    u32      blocks_count;
    u32      first_free_block;
    MdBlockId root;

    MdDefinition* definitions;
    u32           definitions_count;
    u32           first_free_definition;

    MdLabel* labels;
    u32      labels_count;

    MdLabelId* label_buckets;
    u32        label_buckets_count;

    MdFootnoteOccurrence* footnote_occurrences;
    u32                   footnote_occurrences_count;

    MdDefinitionId* footnote_order;
    u32             footnote_order_count;

    MdAllocation* free_allocations;
};

// =====================================================================================================================
// Operation-local state. ScratchArena is passed to functions, never stored.
// =====================================================================================================================

typedef struct MdEdit {
    u32 offset;
    u32 removed;
    u32 inserted;
} MdEdit;

typedef struct MdReader {
    const MdView* source;
    u32           length;

    u32* piece_starts;
    u32  piece_starts_count;
    u32  piece;
} MdReader;

typedef struct MdPhysicalLine {
    u32 begin;
    u32 content_end;
    u32 end;
} MdPhysicalLine;

typedef struct MdContainerFrame {
    u32       marker;
    u32       start;
    u32       marker_indent;
    u32       content_indent;
} MdContainerFrame;

typedef struct MdMark {
    u32 begin;
    u32 end;
    u32 auxiliary;
    u8  character;
    u8  flags;
} MdMark;

typedef struct MdLinkWork {
    MdRange      destination;
    MdRange      title;
    MdLabelId    reference;
    MdLinkPrefix prefix;
    u32          has_title;
} MdLinkWork;

typedef struct MdUpdateWork {
    MdState* state;
    MdState  next;
    MdReader reader;
    MdEdit   edit;
    MdError  error;
    MdAllocation* prepared_allocations;
} MdUpdateWork;

/* Inline parsing works on one leaf's logical text. Maps retain the exact
 * source extent of each byte, including CRLF and stripped container prefixes.
 * None of these arrays, or their source bytes, survive the operation. */
typedef struct MdInlineWork {
    MdUpdateWork* update;
    MdBlockId owner;
    u32 base;
    u8* text;
    u32 text_count;
    MdRange* sources;
    u32 sources_count;
    u8* claimed;
    u32 claimed_count;
    MdMark* marks;
    u32 marks_count;
} MdInlineWork;

// =====================================================================================================================
// Piece access. EOF is -1, distinct from a source NUL byte.
// =====================================================================================================================

internal bool md_reader_init(MdReader* reader, const MdView* source, ScratchArena* scratch) {
    *reader = (MdReader){0};
    if (!source || (source->count && !source->pieces))
        return false;

    u64 length = 0;
    for (u32 i = 0; i < source->count; ++i) {
        if (source->pieces[i].length && !source->pieces[i].bytes)
            return false;
        length += source->pieces[i].length;
        if (length > UINT32_MAX)
            return false;
    }

    u32* starts = NULL;
    if (source->count) {
        if (!scratch || !scratch->data)
            return false;
        usize bytes = (usize)source->count * sizeof(u32);
        usize padding = (alignof(u32) - (scratch->len & (alignof(u32) - 1))) & (alignof(u32) - 1);
        if (bytes >= PAGE_SIZE || scratch->len > ARENA_VCAP ||
            padding > ARENA_VCAP - scratch->len || bytes > ARENA_VCAP - scratch->len - padding)
            return false;
        starts = scratch_alloc_aligned(scratch, u32, source->count);
        if (!starts)
            return false;
        u32 at = 0;
        for (u32 i = 0; i < source->count; ++i) {
            starts[i] = at;
            at += source->pieces[i].length;
        }
    }

    *reader = (MdReader){
        .source = source,
        .length = (u32)length,
        .piece_starts = starts,
        .piece_starts_count = source->count,
    };
    return true;
}

internal u32 md_reader_piece(MdReader* reader, u32 offset) {
    u32 piece = reader->piece;
    if (piece < reader->piece_starts_count) {
        u32 start = reader->piece_starts[piece];
        if (offset >= start && offset - start < reader->source->pieces[piece].length)
            return piece;
    }

    /* Upper bound selects the last duplicate start, skipping empty pieces. */
    u32 lo = 0, hi = reader->piece_starts_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (reader->piece_starts[mid] <= offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    reader->piece = lo - 1;
    return reader->piece;
}

internal i32 md_reader_byte(MdReader* reader, u32 offset) {
    if (offset >= reader->length)
        return -1;
    u32 piece = md_reader_piece(reader, offset);
    return reader->source->pieces[piece].bytes[offset - reader->piece_starts[piece]];
}

internal bool md_reader_copy(MdReader* reader, MdRange range, u8* destination) {
    if (range.begin > range.end || range.end > reader->length || (!destination && range.begin != range.end))
        return false;
    u32 at = range.begin;
    while (at < range.end) {
        u32 piece = md_reader_piece(reader, at);
        u32 local = at - reader->piece_starts[piece];
        u32 count = min(range.end - at, reader->source->pieces[piece].length - local);
        memcpy(destination, reader->source->pieces[piece].bytes + local, count);
        destination += count;
        at += count;
    }
    return true;
}

/* ASCII predicates from MD4C's IS* macros, with an explicit EOF-safe domain. */
internal bool md_ascii_blank(i32 ch) {
    return ch == ' ' || ch == '\t';
}

internal bool md_ascii_newline(i32 ch) {
    return ch == '\r' || ch == '\n';
}

/* MD4C's ISWHITESPACE deliberately excludes CR/LF. Line transitions are
 * handled separately by the grammar, not consumed as horizontal space. */
internal bool md_ascii_whitespace(i32 ch) {
    return md_ascii_blank(ch) || ch == '\v' || ch == '\f';
}

internal bool md_ascii_digit(i32 ch) {
    return ch >= '0' && ch <= '9';
}

internal bool md_ascii_alpha(i32 ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

internal bool md_ascii_alnum(i32 ch) {
    return md_ascii_alpha(ch) || md_ascii_digit(ch);
}

internal bool md_ascii_punctuation(i32 ch) {
    return (ch >= 33 && ch <= 47) || (ch >= 58 && ch <= 64) ||
           (ch >= 91 && ch <= 96) || (ch >= 123 && ch <= 126);
}

internal bool md_ascii_control(i32 ch) {
    return (ch >= 0 && ch <= 31) || ch == 127;
}

internal bool md_ascii_any(i32 ch, const char* characters) {
    if (ch <= 0 || ch > 127)
        return false;
    return strchr(characters, ch) != NULL;
}

/* Port of md_ascii_case_eq(), operating directly across source pieces. */
internal bool md_reader_match(MdReader* reader, u32 offset, const char* text, u32 length, bool fold_case) {
    if (offset > reader->length || length > reader->length - offset || (!text && length))
        return false;
    for (u32 i = 0; i < length; ++i) {
        i32 left = md_reader_byte(reader, offset + i);
        i32 right = (u8)text[i];
        if (fold_case) {
            if (left >= 'a' && left <= 'z')
                left += 'A' - 'a';
            if (right >= 'a' && right <= 'z')
                right += 'A' - 'a';
        }
        if (left != right)
            return false;
    }
    return true;
}

/* Port of md_newline_end(). CRLF can straddle pieces. */
internal u32 md_newline_end(MdReader* reader, u32 offset) {
    if (md_reader_byte(reader, offset) == '\r')
        ++offset;
    if (md_reader_byte(reader, offset) == '\n')
        ++offset;
    return offset;
}

internal bool md_reader_line(MdReader* reader, u32 offset, MdPhysicalLine* line) {
    if (offset >= reader->length)
        return false;
    u32 end = offset;
    while (end < reader->length && !md_ascii_newline(md_reader_byte(reader, end)))
        ++end;
    *line = (MdPhysicalLine){.begin = offset, .content_end = end, .end = md_newline_end(reader, end)};
    return true;
}

// =====================================================================================================================
// Block recognition, ported individually from parser.h.
// These recognize syntax; the block parser decides contextual precedence.
// =====================================================================================================================

/* md_line_indentation(): tab stops include the enclosing container's columns.
 * u64 avoids wrapping on a valid u32-length line containing only tabs. */
internal u64 md_line_indentation(MdReader* reader, u64 total_indent, u32 begin, u32* end) {
    u32 at = begin;
    u64 indent = total_indent;
    while (at < reader->length && md_ascii_blank(md_reader_byte(reader, at))) {
        if (md_reader_byte(reader, at) == '\t')
            indent = (indent + 4) & ~UINT64_C(3);
        else
            ++indent;
        ++at;
    }
    *end = at;
    return indent - total_indent;
}

/* md_is_hr_line(): killer is the first position which rules out this marker. */
internal bool md_is_hr_line(MdReader* reader, u32 begin, u32* end, u32* killer) {
    i32 marker = md_reader_byte(reader, begin);
    if (marker != '-' && marker != '_' && marker != '*') {
        *killer = begin;
        return false;
    }
    u32 at = begin + 1, count = 1;
    while (at < reader->length) {
        i32 ch = md_reader_byte(reader, at);
        if (ch == marker)
            ++count;
        else if (!md_ascii_blank(ch))
            break;
        ++at;
    }
    if (count < 3 || (at < reader->length && !md_ascii_newline(md_reader_byte(reader, at)))) {
        *killer = at;
        return false;
    }
    *end = at;
    return true;
}

internal bool md_is_atxheader_line(MdReader* reader, u32 flags, u32 begin,
                                   u32* content_begin, u32* end, u32* level) {
    if (md_reader_byte(reader, begin) != '#')
        return false;
    u32 at = begin + 1;
    while (at < reader->length && md_reader_byte(reader, at) == '#' && at - begin < 7)
        ++at;
    if (at - begin > 6)
        return false;
    if (!(flags & MD_FLAG_PERMISSIVEATXHEADERS) && at < reader->length &&
        !md_ascii_blank(md_reader_byte(reader, at)) && !md_ascii_newline(md_reader_byte(reader, at)))
        return false;
    *level = at - begin;
    while (at < reader->length && md_ascii_blank(md_reader_byte(reader, at)))
        ++at;
    *content_begin = at;
    *end = at;
    return true;
}

internal bool md_is_setext_underline(MdReader* reader, u32 begin, u32* end, u32* level) {
    i32 marker = md_reader_byte(reader, begin);
    if (marker != '=' && marker != '-')
        return false;
    u32 at = begin + 1;
    while (at < reader->length && md_reader_byte(reader, at) == marker)
        ++at;
    while (at < reader->length && md_ascii_blank(md_reader_byte(reader, at)))
        ++at;
    if (at < reader->length && !md_ascii_newline(md_reader_byte(reader, at)))
        return false;
    *level = marker == '=' ? 1 : 2;
    *end = at;
    return true;
}

internal bool md_is_table_underline(MdReader* reader, u32 begin, u32* end, u32* columns) {
    u32 at = begin, count = 0;
    bool found_pipe = false;
    if (md_reader_byte(reader, at) == '|') {
        found_pipe = true;
        ++at;
        while (at < reader->length && md_ascii_whitespace(md_reader_byte(reader, at)))
            ++at;
    }
    for (;;) {
        bool delimited = false;
        if (md_reader_byte(reader, at) == ':')
            ++at;
        if (md_reader_byte(reader, at) != '-')
            return false;
        while (at < reader->length && md_reader_byte(reader, at) == '-')
            ++at;
        if (md_reader_byte(reader, at) == ':')
            ++at;
        ++count;
        while (at < reader->length && md_ascii_whitespace(md_reader_byte(reader, at)))
            ++at;
        if (md_reader_byte(reader, at) == '|') {
            found_pipe = true;
            delimited = true;
            ++at;
            while (at < reader->length && md_ascii_whitespace(md_reader_byte(reader, at)))
                ++at;
        }
        if (at >= reader->length || md_ascii_newline(md_reader_byte(reader, at)))
            break;
        if (!delimited)
            return false;
    }
    if (!found_pipe)
        return false;
    *end = at;
    *columns = count;
    return true;
}

internal bool md_is_opening_code_fence(MdReader* reader, u32 begin, u32* end, u32* fence_length) {
    i32 marker = md_reader_byte(reader, begin);
    if (marker != '`' && marker != '~')
        return false;
    u32 at = begin;
    while (at < reader->length && md_reader_byte(reader, at) == marker)
        ++at;
    u32 length = at - begin;
    if (length < 3)
        return false;
    while (at < reader->length && md_reader_byte(reader, at) == ' ')
        ++at;
    while (at < reader->length && !md_ascii_newline(md_reader_byte(reader, at))) {
        if (marker == '`' && md_reader_byte(reader, at) == '`')
            return false;
        ++at;
    }
    *end = at;
    *fence_length = length;
    return true;
}

internal bool md_is_closing_code_fence(MdReader* reader, u32 begin, u32 marker, u32 fence_length, u32* end) {
    u32 at = begin;
    while (at < reader->length && md_reader_byte(reader, at) == (i32)marker)
        ++at;
    bool closes = at - begin >= fence_length;
    if (closes) {
        while (at < reader->length && md_ascii_blank(md_reader_byte(reader, at)))
            ++at;
        closes = at >= reader->length || md_ascii_newline(md_reader_byte(reader, at));
    }
    /* MD4C returns the scanned position on failure too. */
    *end = at;
    return closes;
}

internal bool md_is_container_mark(MdReader* reader, u32 flags, u32 indent, u32 begin,
                                   u32* end, MdContainerFrame* frame) {
    if (begin >= reader->length || (!(flags & MD_FLAG_NOINDENTEDCODEBLOCKS) && indent >= 4))
        return false;
    i32 marker = md_reader_byte(reader, begin);
    u32 at = begin;
    MdContainerFrame result = {.marker_indent = indent};
    if (marker == '>') {
        result.marker = (u32)marker;
        if (indent == UINT32_MAX)
            return false;
        result.content_indent = indent + 1;
        *end = begin + 1;
        *frame = result;
        return true;
    }
    i32 next = md_reader_byte(reader, begin + 1);
    if (md_ascii_any(marker, "-+*") && (next == -1 || md_ascii_blank(next) || md_ascii_newline(next))) {
        if (indent == UINT32_MAX)
            return false;
        result.marker = (u32)marker;
        result.content_indent = indent + 1;
        *end = begin + 1;
        *frame = result;
        return true;
    }
    while (at < reader->length && at - begin < 9 && md_ascii_digit(md_reader_byte(reader, at))) {
        result.start = result.start * 10 + (u32)(md_reader_byte(reader, at) - '0');
        ++at;
    }
    marker = md_reader_byte(reader, at);
    if (at == begin || (marker != '.' && marker != ')'))
        return false;
    next = md_reader_byte(reader, at + 1);
    if (next != -1 && !md_ascii_blank(next) && !md_ascii_newline(next))
        return false;
    u32 width = at - begin + 1;
    if (indent > UINT32_MAX - width)
        return false;
    result.marker = (u32)marker;
    result.content_indent = indent + width;
    *end = at + 1;
    *frame = result;
    return true;
}

internal bool md_is_container_compatible(const MdContainerFrame* previous, const MdContainerFrame* current) {
    return current->marker != '>' && current->marker == previous->marker &&
           current->marker_indent <= previous->content_indent;
}

internal void md_analyze_table_alignment(MdReader* reader, MdRange range, MdAlignment* alignments, u32 count) {
    const MdAlignment map[] = {MD_ALIGN_DEFAULT, MD_ALIGN_LEFT, MD_ALIGN_RIGHT, MD_ALIGN_CENTER};
    u32 at = range.begin;
    for (u32 i = 0; i < count; ++i) {
        u32 index = 0;
        while (at < range.end && md_reader_byte(reader, at) != '-')
            ++at;
        if (at > range.begin && md_reader_byte(reader, at - 1) == ':')
            index |= 1;
        while (at < range.end && md_reader_byte(reader, at) == '-')
            ++at;
        if (at < range.end && md_reader_byte(reader, at) == ':')
            index |= 2;
        alignments[i] = map[index];
    }
}

// =====================================================================================================================
// Inline recognizers. Input limits are explicit and can cross source pieces.
// =====================================================================================================================

/* md_is_entity_str() and its three content scanners, without a contiguous
 * string assumption. Recognition deliberately accepts unknown named entities;
 * decoding decides whether a replacement actually exists. */
internal bool md_is_entity(MdReader* reader, u32 begin, u32 limit, u32* end) {
    if (limit > reader->length || begin >= limit || md_reader_byte(reader, begin) != '&')
        return false;
    u32 at = begin + 1;
    if (at < limit && md_reader_byte(reader, at) == '#') {
        ++at;
        bool hex = at < limit && (md_reader_byte(reader, at) == 'x' || md_reader_byte(reader, at) == 'X');
        if (hex)
            ++at;
        u32 digits = 0, maximum = hex ? 6 : 7;
        while (at < limit) {
            i32 ch = md_reader_byte(reader, at);
            if (!md_ascii_digit(ch) && !(hex && ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))))
                break;
            if (++digits > maximum)
                return false;
            ++at;
        }
        if (!digits)
            return false;
    } else {
        if (at >= limit || !md_ascii_alpha(md_reader_byte(reader, at)))
            return false;
        u32 first = at++;
        while (at < limit && md_ascii_alnum(md_reader_byte(reader, at))) {
            ++at;
            if (at - first > 48)
                return false;
        }
        if (at - first < 2)
            return false;
    }
    if (at >= limit || md_reader_byte(reader, at) != ';')
        return false;
    *end = at + 1;
    return true;
}

/* md_is_link_destination_A/B(): raw ranges, no normalized string allocation. */
internal bool md_is_link_destination(MdReader* reader, u32 begin, u32 limit,
                                     u32* end, MdRange* content) {
    if (limit > reader->length || begin >= limit)
        return false;
    u32 at = begin;
    if (md_reader_byte(reader, at) == '<') {
        ++at;
        while (at < limit) {
            i32 ch = md_reader_byte(reader, at);
            if (ch == '\\' && limit - at > 1 && md_ascii_punctuation(md_reader_byte(reader, at + 1))) {
                at += 2;
                continue;
            }
            if (md_ascii_newline(ch) || ch == '<')
                return false;
            if (ch == '>') {
                *content = (MdRange){begin + 1, at};
                *end = at + 1;
                return true;
            }
            ++at;
        }
        return false;
    }
    u32 depth = 0;
    while (at < limit) {
        i32 ch = md_reader_byte(reader, at);
        if (ch == '\\' && limit - at > 1 && md_ascii_punctuation(md_reader_byte(reader, at + 1))) {
            at += 2;
            continue;
        }
        if (md_ascii_whitespace(ch) || md_ascii_control(ch))
            break;
        if (ch == '(') {
            if (++depth > 32)
                return false;
        } else if (ch == ')') {
            if (!depth)
                break;
            --depth;
        }
        ++at;
    }
    if (depth || at == begin)
        return false;
    *content = (MdRange){begin, at};
    *end = at;
    return true;
}

internal bool md_is_autolink_uri(MdReader* reader, u32 begin, u32 limit, u32* end) {
    if (limit > reader->length || begin >= limit || md_reader_byte(reader, begin) != '<')
        return false;
    u32 at = begin + 1;
    if (at >= limit || !md_ascii_alnum(md_reader_byte(reader, at)))
        return false;
    ++at;
    for (;;) {
        if (at >= limit || at - begin > 32)
            return false;
        i32 ch = md_reader_byte(reader, at);
        if (ch == ':' && at - begin >= 3)
            break;
        if (!md_ascii_alnum(ch) && !md_ascii_any(ch, "+-."))
            return false;
        ++at;
    }
    while (at < limit && md_reader_byte(reader, at) != '>') {
        i32 ch = md_reader_byte(reader, at);
        if (md_ascii_whitespace(ch) || md_ascii_control(ch) || ch == '<')
            return false;
        ++at;
    }
    if (at >= limit)
        return false;
    *end = at + 1;
    return true;
}

internal bool md_is_autolink_email(MdReader* reader, u32 begin, u32 limit, u32* end) {
    if (limit > reader->length || begin >= limit || md_reader_byte(reader, begin) != '<')
        return false;
    u32 at = begin + 1;
    while (at < limit && (md_ascii_alnum(md_reader_byte(reader, at)) ||
                         md_ascii_any(md_reader_byte(reader, at), ".!#$%&'*+/=?^_`{|}~-")))
        ++at;
    if (at == begin + 1 || at >= limit || md_reader_byte(reader, at) != '@')
        return false;
    ++at;
    u32 label_length = 0;
    while (at < limit) {
        i32 ch = md_reader_byte(reader, at);
        if (md_ascii_alnum(ch) || (ch == '-' && label_length))
            ++label_length;
        else if (ch == '.' && label_length && md_reader_byte(reader, at - 1) != '-')
            label_length = 0;
        else
            break;
        if (label_length > 63)
            return false;
        ++at;
    }
    if (!label_length || at >= limit || md_reader_byte(reader, at) != '>' || md_reader_byte(reader, at - 1) == '-')
        return false;
    *end = at + 1;
    return true;
}

internal bool md_is_autolink(MdReader* reader, u32 begin, u32 limit, u32* end, MdLinkPrefix* prefix) {
    if (md_is_autolink_uri(reader, begin, limit, end)) {
        *prefix = MD_LINK_PREFIX_NONE;
        return true;
    }
    if (md_is_autolink_email(reader, begin, limit, end)) {
        *prefix = MD_LINK_PREFIX_MAILTO;
        return true;
    }
    return false;
}

// =====================================================================================================================
// Transaction storage. Only the caller's aligned arena allocation APIs are used.
// =====================================================================================================================

#define MD_BLOCK_DIRTY 0x80000000u

internal bool md_id_equal(MdId a, MdId b) {
    return a.index == b.index && a.generation == b.generation;
}

internal usize md_array_slots(u32 count) {
    usize slots = 16;
    while (slots < count)
        slots *= 2;
    return slots;
}

internal void* md_work_alloc(MdUpdateWork* work, ScratchArena* scratch, usize bytes) {
    usize words = bytes / sizeof(u64) + (bytes % sizeof(u64) != 0);
    usize padding = (8 - (scratch->len & 7)) & 7;
    if (words >= PAGE_SIZE / sizeof(u64) || scratch->len > ARENA_VCAP ||
        padding > ARENA_VCAP - scratch->len || words * sizeof(u64) > ARENA_VCAP - scratch->len - padding) {
        work->error = MD_ERROR_OUT_OF_MEMORY;
        return NULL;
    }
    if (!words)
        words = 1;
    void* result = scratch_alloc_aligned(scratch, u64, words);
    if (!result)
        work->error = MD_ERROR_OUT_OF_MEMORY;
    return result;
}

internal void* md_work_clone(MdUpdateWork* work, ScratchArena* scratch,
                             const void* source, u32 count, usize size) {
    usize slots = md_array_slots(count);
    if (size && slots > SIZE_MAX / size) {
        work->error = MD_ERROR_OVERFLOW;
        return NULL;
    }
    void* result = md_work_alloc(work, scratch, slots * size);
    if (result && count && source)
        memcpy(result, source, (usize)count * size);
    return result;
}

internal bool md_work_push(MdUpdateWork* work, ScratchArena* scratch,
                            void** elements, u32* count, usize size, const void* value) {
    if (work->error)
        return false;
    if (*count == UINT32_MAX) {
        work->error = MD_ERROR_OVERFLOW;
        return false;
    }
    if (!*elements || (*count >= 16 && (*count & (*count - 1)) == 0)) {
        usize slots = md_array_slots(*count + 1);
        if (size && slots > SIZE_MAX / size) {
            work->error = MD_ERROR_OVERFLOW;
            return false;
        }
        void* grown = md_work_alloc(work, scratch, slots * size);
        if (!grown)
            return false;
        if (*count)
            memcpy(grown, *elements, (usize)*count * size);
        *elements = grown;
    }
    memcpy((u8*)*elements + (usize)*count * size, value, size);
    ++*count;
    return true;
}

internal MdBlock* md_block_at(MdState* state, MdBlockId id) {
    if (id.index >= state->blocks_count)
        return NULL;
    MdBlock* block = &state->blocks[id.index];
    return block->alive && block->generation == id.generation ? block : NULL;
}

internal u32 md_block_base(const MdState* state, MdBlockId id) {
    u64 offset = 0;
    while (id.index != UINT32_MAX) {
        const MdBlock* block = &state->blocks[id.index];
        offset += block->offset;
        id = block->parent;
    }
    return (u32)offset;
}

/* Bound recursive parser/query traversal before descending. Hosts with a
 * larger stack may raise this build-time resource limit. */
#ifndef MD_MAX_BLOCK_DEPTH
#define MD_MAX_BLOCK_DEPTH 256u
#endif

internal MdBlockId md_new_block(MdUpdateWork* work, ScratchArena* scratch,
                                MdBlockId parent, MdBlockKind kind, u32 begin, u32 end) {
    MdBlock block = {.alive = true, .generation = 1, .next_free = UINT32_MAX,
                     .parent = parent, .kind = kind, .length = end - begin,
                     .flags = MD_BLOCK_DIRTY};
    if (parent.index != UINT32_MAX) {
        block.depth = md_block_at(&work->next, parent)->depth + 1;
        if (block.depth > MD_MAX_BLOCK_DEPTH) {
            work->error = MD_ERROR_OVERFLOW;
            return MD_ID_NONE;
        }
    }
    block.offset = begin - (parent.index == UINT32_MAX ? 0 : md_block_base(&work->next, parent));
    u32 index;
    if (work->next.first_free_block != UINT32_MAX) {
        index = work->next.first_free_block;
        MdBlock* old = &work->next.blocks[index];
        work->next.first_free_block = old->next_free;
        block.generation = old->generation + 1;
        if (!block.generation) {
            work->error = MD_ERROR_OVERFLOW;
            return MD_ID_NONE;
        }
        *old = block;
    } else {
        index = work->next.blocks_count;
        if (!md_work_push(work, scratch, (void**)&work->next.blocks, &work->next.blocks_count,
                          sizeof(block), &block))
            return MD_ID_NONE;
    }
    return (MdBlockId){index, block.generation};
}

internal void md_remove_block(MdUpdateWork* work, MdBlockId id) {
    MdBlock* block = md_block_at(&work->next, id);
    if (!block)
        return;
    for (u32 i = 0; i < block->children_count; ++i)
        md_remove_block(work, block->children[i]);
    if (block->kind == MD_BLOCK_DEFINITION || block->kind == MD_BLOCK_FOOTNOTE) {
        MdDefinitionId def = block->data.definition.definition;
        if (def.index < work->next.definitions_count) {
            MdDefinition* definition = &work->next.definitions[def.index];
            definition->alive = false;
            definition->next_free = work->next.first_free_definition;
            work->next.first_free_definition = def.index;
        }
    }
    block->alive = false;
    block->flags |= MD_BLOCK_DIRTY;
    block->next_free = work->next.first_free_block;
    work->next.first_free_block = id.index;
}

internal bool md_add_child(MdUpdateWork* work, ScratchArena* scratch, MdBlockId parent, MdBlockId child) {
    MdBlock* block = md_block_at(&work->next, parent);
    if (!block || child.index == UINT32_MAX)
        return false;
    return md_work_push(work, scratch, (void**)&block->children, &block->children_count, sizeof(child), &child);
}

internal bool md_add_node(MdUpdateWork* work, ScratchArena* scratch, MdBlockId owner,
                          MdNode node, MdLabelId reference) {
    MdBlock* block = md_block_at(&work->next, owner);
    MdAnnotation annotation = {.node = node, .reference = reference};
    return block && md_work_push(work, scratch, (void**)&block->annotations, &block->annotations_count,
                                 sizeof(annotation), &annotation);
}

internal bool md_store_lines(MdUpdateWork* work, ScratchArena* scratch, MdBlockId owner,
                             const MdLine* lines, u32 count) {
    MdBlock* block = md_block_at(&work->next, owner);
    u32 base = md_block_base(&work->next, owner);
    block->lines = md_work_clone(work, scratch, lines, count, sizeof(*lines));
    if (!block->lines)
        return false;
    block->lines_count = count;
    for (u32 i = 0; i < count; ++i) {
        block->lines[i].source.begin -= base;
        block->lines[i].source.end -= base;
        block->lines[i].content.begin -= base;
        block->lines[i].content.end -= base;
    }
    return true;
}

internal bool md_inline_text(MdUpdateWork* work, ScratchArena* scratch, MdBlockId owner,
                             const MdLine* lines, u32 count, u32 base, MdInlineWork* text) {
    *text = (MdInlineWork){.update = work, .owner = owner, .base = base};
    u64 total = 0;
    for (u32 i = 0; i < count; ++i)
        total += (u64)lines[i].content.end - lines[i].content.begin + (i + 1 < count);
    if (total > UINT32_MAX) {
        work->error = MD_ERROR_OVERFLOW;
        return false;
    }
    text->text = md_work_alloc(work, scratch, (usize)total + 1);
    text->sources = md_work_alloc(work, scratch, ((usize)total + 1) * sizeof(MdRange));
    text->claimed = md_work_alloc(work, scratch, (usize)total + 1);
    if (work->error)
        return false;
    for (u32 i = 0; i < count; ++i) {
        for (u32 at = lines[i].content.begin; at < lines[i].content.end; ++at) {
            u32 n = text->text_count++;
            text->text[n] = (u8)md_reader_byte(&work->reader, base + at);
            text->sources[n] = (MdRange){at, at + 1};
        }
        if (i + 1 < count) {
            u32 n = text->text_count++;
            text->text[n] = '\n';
            text->sources[n] = (MdRange){lines[i].content.end, lines[i].source.end};
        }
    }
    text->sources_count = text->claimed_count = text->text_count;
    return true;
}

/* Temporary logical-text readers borrow arrays in the caller's frame. */
internal MdReader md_inline_reader(MdInlineWork* text, MdPiece* piece, MdView* view, u32* start) {
    *piece = (MdPiece){text->text, text->text_count};
    *view = (MdView){piece, 1};
    *start = 0;
    return (MdReader){.source = view, .length = text->text_count,
                      .piece_starts = start, .piece_starts_count = 1};
}

internal MdRange md_inline_range(const MdInlineWork* text, u32 begin, u32 end) {
    if (begin == end) {
        u32 at = begin < text->text_count ? text->sources[begin].begin :
                 (text->text_count ? text->sources[text->text_count - 1].end : 0);
        return (MdRange){at, at};
    }
    return (MdRange){text->sources[begin].begin, text->sources[end - 1].end};
}

internal bool md_node_content(MdNode* node, MdRange content) {
    if (content.begin < node->start_idx || content.end < content.begin || content.end > node->end_idx ||
        content.begin - node->start_idx > UINT16_MAX)
        return false;
    node->content_offset = content.begin - node->start_idx;
    node->content_length = content.end - content.begin;
    return true;
}

internal bool md_inline_node(MdInlineWork* text, ScratchArena* scratch, MdNodeKind kind,
                             u32 begin, u32 end, u32 content_begin, u32 content_end, MdLabelId reference) {
    MdRange range = md_inline_range(text, begin, end);
    MdNode node = {.kind = kind, .start_idx = range.begin, .end_idx = range.end};
    if (!md_node_content(&node, md_inline_range(text, content_begin, content_end))) {
        text->update->error = MD_ERROR_OVERFLOW;
        return false;
    }
    return md_add_node(text->update, scratch, text->owner, node, reference);
}

internal bool md_crosses(MdRange left, MdRange right) {
    return (left.begin < right.begin && right.begin < left.end && left.end < right.end) ||
           (right.begin < left.begin && left.begin < right.end && right.end < left.end);
}

internal bool md_inline_nests(MdInlineWork* text, u32 begin, u32 end) {
    MdRange range = md_inline_range(text, begin, end);
    const MdBlock* block = md_block_at(&text->update->next, text->owner);
    for (u32 i = 0; i < block->annotations_count; ++i) {
        const MdNode* node = &block->annotations[i].node;
        if (node->kind != MD_NODE_HIDDEN && node->kind != MD_NODE_REPLACEMENT &&
            md_crosses(range, (MdRange){node->start_idx, node->end_idx}))
            return false;
    }
    return true;
}

internal void md_claim(MdInlineWork* text, u32 begin, u32 end, u8 claim) {
    for (u32 i = begin; i < end; ++i)
        text->claimed[i] = claim;
}

internal bool md_inline_replace(MdInlineWork* text, ScratchArena* scratch, u32 begin, u32 end,
                                const u8* replacement, u32 length, u32 repeat) {
    MdRange range = md_inline_range(text, begin, end);
    MdNode node = {.kind = MD_NODE_REPLACEMENT, .start_idx = range.begin, .end_idx = range.end};
    if (!length || length > sizeof(node.data.replacement.text) || !repeat) {
        text->update->error = MD_ERROR_OVERFLOW;
        return false;
    }
    memcpy(node.data.replacement.text, replacement, length);
    node.data.replacement.length = length;
    node.data.replacement.repeat = repeat;
    md_claim(text, begin, end, 2);
    return md_add_node(text->update, scratch, text->owner, node, MD_ID_NONE);
}

internal u32 md_utf8(const u8* bytes, u32 length, i32* cp) {
    utf8proc_ssize_t n = utf8proc_iterate(bytes, length, cp);
    if (n < 1) {
        *cp = 0xfffd;
        return 1;
    }
    return (u32)n;
}

internal bool md_unicode_space(i32 cp) {
    utf8proc_category_t category = utf8proc_category(cp);
    return cp == 9 || cp == 10 || cp == 11 || cp == 12 || cp == 13 ||
           category == UTF8PROC_CATEGORY_ZS || category == UTF8PROC_CATEGORY_ZL || category == UTF8PROC_CATEGORY_ZP;
}

internal bool md_unicode_punct(i32 cp) {
    utf8proc_category_t category = utf8proc_category(cp);
    return md_ascii_punctuation(cp) || (category >= UTF8PROC_CATEGORY_PC && category <= UTF8PROC_CATEGORY_PO);
}

internal MdLabelId md_label(MdUpdateWork* work, ScratchArena* scratch, const u8* text,
                            u32 length, MdDefinitionKind kind) {
    /* MD4C's label equivalence: Unicode case folding, trimmed/collapsed space.
     * Unicode folding can expand a scalar, so it is not ASCII lowercasing. */
    u8* key = md_work_alloc(work, scratch, (usize)length * 12 + 1);
    if (!key)
        return MD_ID_NONE;
    u32 count = 0;
    bool space = false;
    for (u32 at = 0; at < length;) {
        i32 cp;
        at += md_utf8(text + at, length - at, &cp);
        if (md_unicode_space(cp)) {
            space = count != 0;
            continue;
        }
        if (space)
            key[count++] = ' ';
        space = false;
        i32 folded[8];
        utf8proc_ssize_t n = utf8proc_decompose_char(cp, folded, 8, UTF8PROC_CASEFOLD, NULL);
        if (n < 1 || n > 8) {
            work->error = MD_ERROR_OVERFLOW;
            return MD_ID_NONE;
        }
        for (utf8proc_ssize_t i = 0; i < n; ++i)
            count += (u32)utf8proc_encode_char(folded[i], key + count);
    }
    if (!count)
        return MD_ID_NONE;
    u64 hash = UINT64_C(14695981039346656037);
    for (u32 i = 0; i < count; ++i)
        hash = (hash ^ key[i]) * UINT64_C(1099511628211);
    for (u32 i = 0; i < work->next.labels_count; ++i) {
        MdLabel* label = &work->next.labels[i];
        if (label->alive && label->kind == kind && label->hash == hash &&
            label->key_count == count && !memcmp(label->key, key, count))
            return (MdLabelId){i, label->generation};
    }
    MdLabel label = {.alive = true, .generation = 1, .kind = kind, .hash = hash,
                     .key = key, .key_count = count, .winner = MD_ID_NONE};
    for (u32 i = 0; i < work->next.labels_count; ++i) {
        if (!work->next.labels[i].alive) {
            label.generation = work->next.labels[i].generation + 1;
            if (!label.generation) {
                work->error = MD_ERROR_OVERFLOW;
                return MD_ID_NONE;
            }
            work->next.labels[i] = label;
            return (MdLabelId){i, label.generation};
        }
    }
    u32 index = work->next.labels_count;
    if (!md_work_push(work, scratch, (void**)&work->next.labels, &work->next.labels_count,
                      sizeof(label), &label))
        return MD_ID_NONE;
    return (MdLabelId){index, label.generation};
}

internal MdDefinitionId md_reference(MdInlineWork* text, ScratchArena* scratch,
                                     const u8* key, u32 length, MdDefinitionKind kind, MdLabelId* id) {
    MdUpdateWork* work = text->update;
    *id = md_label(work, scratch, key, length, kind);
    if (id->index == UINT32_MAX)
        return MD_ID_NONE;
    MdBlock* block = md_block_at(&work->next, text->owner);
    bool found = false;
    for (u32 i = 0; i < block->reference_uses_count; ++i)
        found |= md_id_equal(block->reference_uses[i], *id);
    if (!found)
        md_work_push(work, scratch, (void**)&block->reference_uses, &block->reference_uses_count,
                      sizeof(*id), id);
    return work->next.labels[id->index].winner;
}

// =====================================================================================================================
// HTML and link attributes. Adapted from md_is_html_tag(), md_is_link_title().
// =====================================================================================================================

internal bool md_html_tag(MdReader* reader, u32 begin, u32 limit, u32* end) {
    if (begin >= limit || md_reader_byte(reader, begin) != '<')
        return false;
    u32 at = begin + 1;
    i32 state = 0;
    if (md_reader_byte(reader, at) == '/') {
        state = -1;
        ++at;
    }
    if (at >= limit || !md_ascii_alpha(md_reader_byte(reader, at)))
        return false;
    ++at;
    while (at < limit && (md_ascii_alnum(md_reader_byte(reader, at)) || md_reader_byte(reader, at) == '-'))
        ++at;
    while (at < limit) {
        i32 ch = md_reader_byte(reader, at);
        if (state > 40) {
            if (state == 41 && (md_ascii_whitespace(ch) || md_ascii_newline(ch) || md_ascii_any(ch, "\"'=<>`"))) {
                state = 0;
                continue;
            }
            if ((state == 42 && ch == '\'') || (state == 43 && ch == '"'))
                state = 0;
            ++at;
        } else if (md_ascii_whitespace(ch) || md_ascii_newline(ch)) {
            if (!state)
                state = 1;
            ++at;
        } else if (state <= 2 && (ch == '>' || (ch == '/' && at + 1 < limit && md_reader_byte(reader, at + 1) == '>'))) {
            *end = at + (ch == '/' ? 2 : 1);
            return true;
        } else if ((state == 1 || state == 2) && (md_ascii_alpha(ch) || ch == '_' || ch == ':')) {
            ++at;
            while (at < limit && (md_ascii_alnum(md_reader_byte(reader, at)) || md_ascii_any(md_reader_byte(reader, at), "_.:-")))
                ++at;
            state = 2;
        } else if (state == 2 && ch == '=') {
            state = 3;
            ++at;
        } else if (state == 3 && !md_ascii_any(ch, "'=<>`")) {
            state = ch == '"' ? 43 : 41;
            ++at;
        } else if (state == 3 && ch == '\'') {
            state = 42;
            ++at;
        } else {
            return false;
        }
    }
    return false;
}

internal bool md_find_literal(MdReader* reader, u32 begin, u32 end, const char* value,
                               bool fold, u32* found_end) {
    u32 length = (u32)strlen(value);
    if (begin > end || length > end - begin)
        return false;
    for (u32 at = begin; at <= end - length; ++at) {
        if (md_reader_match(reader, at, value, length, fold)) {
            *found_end = at + length;
            return true;
        }
    }
    return false;
}

internal bool md_html_inline(MdReader* reader, u32 begin, u32 limit, u32* end) {
    if (md_html_tag(reader, begin, limit, end))
        return true;
    if (md_reader_match(reader, begin, "<!--", 4, false))
        return md_find_literal(reader, begin + 2, limit, "-->", false, end);
    if (md_reader_match(reader, begin, "<?", 2, false))
        return md_find_literal(reader, begin + 2, limit, "?>", false, end);
    if (md_reader_match(reader, begin, "<![CDATA[", 9, false))
        return md_find_literal(reader, begin + 9, limit, "]]>", false, end);
    if (md_reader_match(reader, begin, "<!", 2, false) &&
        md_ascii_alpha(md_reader_byte(reader, begin + 2)))
        return md_find_literal(reader, begin + 2, limit, ">", false, end);
    return false;
}

internal u32 md_html_block_start(MdReader* reader, u32 begin, u32 end) {
    if (md_reader_byte(reader, begin) != '<')
        return 0;
    const char* raw[] = {"script", "pre", "style", "textarea"};
    for (u32 i = 0; i < 4; ++i) {
        u32 size = (u32)strlen(raw[i]);
        if (size + 1 <= end - begin && md_reader_match(reader, begin + 1, raw[i], size, true))
            return 1;
    }
    if (md_reader_match(reader, begin, "<!--", 4, false))
        return 2;
    if (md_reader_match(reader, begin, "<?", 2, false))
        return 3;
    i32 declaration = md_reader_byte(reader, begin + 2);
    if (md_reader_match(reader, begin, "<!", 2, false) && declaration >= 0 && declaration <= 127)
        return 4;
    if (md_reader_match(reader, begin, "<![CDATA[", 9, false))
        return 5;
    /* The MD4C/CommonMark type-six tag set. */
    const char* tags[] = {
        "address", "article", "aside", "base", "basefont", "blockquote", "body", "caption", "center",
        "col", "colgroup", "dd", "details", "dialog", "dir", "div", "dl", "dt", "fieldset", "figcaption",
        "figure", "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head",
        "header", "hr", "html", "iframe", "legend", "li", "link", "main", "menu", "menuitem", "nav",
        "noframes", "ol", "optgroup", "option", "p", "param", "search", "section", "summary",
        "table", "tbody", "td", "tfoot", "th", "thead", "title", "tr", "track", "ul", "video"
    };
    u32 at = begin + 1;
    if (md_reader_byte(reader, at) == '/')
        ++at;
    for (u32 i = 0; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        u32 size = (u32)strlen(tags[i]);
        if (at > end || size > end - at || !md_reader_match(reader, at, tags[i], size, true))
            continue;
        i32 ch = md_reader_byte(reader, at + size);
        if (at + size == end || md_ascii_blank(ch) || ch == '>' ||
            (ch == '/' && at + size + 1 < end && md_reader_byte(reader, at + size + 1) == '>'))
            return 6;
    }
    u32 close;
    if (md_html_tag(reader, begin, end, &close)) {
        while (close < end && md_ascii_blank(md_reader_byte(reader, close)))
            ++close;
        if (close == end)
            return 7;
    }
    return 0;
}

internal bool md_html_block_end(MdReader* reader, u32 mode, MdRange line) {
    u32 end;
    if (mode == 1)
        return md_find_literal(reader, line.begin, line.end, "</script>", true, &end) ||
               md_find_literal(reader, line.begin, line.end, "</pre>", true, &end) ||
               md_find_literal(reader, line.begin, line.end, "</style>", true, &end) ||
               md_find_literal(reader, line.begin, line.end, "</textarea>", true, &end);
    const char* closers[] = {"", "", "-->", "?>", ">", "]]>"};
    return mode < 6 && md_find_literal(reader, line.begin, line.end, closers[mode], false, &end);
}

internal bool md_link_title(MdReader* reader, u32 begin, u32 limit, u32* end, MdRange* title) {
    u32 at = begin, breaks = 0;
    while (at < limit && (md_ascii_whitespace(md_reader_byte(reader, at)) || md_ascii_newline(md_reader_byte(reader, at)))) {
        if (md_reader_byte(reader, at) == '\n' && ++breaks > 1)
            return false;
        ++at;
    }
    if (at == begin || at == limit)
        return false;
    i32 opener = md_reader_byte(reader, at);
    if (opener != '\'' && opener != '"' && opener != '(')
        return false;
    i32 closer = opener == '(' ? ')' : opener;
    u32 content = ++at;
    breaks = 0;
    while (at < limit) {
        i32 ch = md_reader_byte(reader, at);
        if (ch == '\\' && at + 1 < limit && md_ascii_punctuation(md_reader_byte(reader, at + 1))) {
            at += 2;
            breaks = 0;
            continue;
        }
        if (ch == closer) {
            *title = (MdRange){content, at};
            *end = at + 1;
            return true;
        }
        if (opener == '(' && ch == '(')
            return false;
        if (ch == '\n') {
            if (++breaks > 1)
                return false;
        } else if (!md_ascii_blank(ch)) {
            breaks = 0;
        }
        ++at;
    }
    return false;
}

internal u64 md_line_column(MdReader* reader, MdLine line) {
    u64 column = 0;
    for (u32 at = line.source.begin; at < line.content.begin; ++at)
        column = md_reader_byte(reader, at) == '\t' ? (column + 4) & ~UINT64_C(3) : column + 1;
    return column;
}

internal u32 md_skip_line_space(MdReader* reader, MdLine line, u32* columns) {
    u32 at = line.content.begin;
    u64 indent = line.indent_columns, column = md_line_column(reader, line);
    while (at < line.content.end && md_ascii_blank(md_reader_byte(reader, at))) {
        u32 width = md_reader_byte(reader, at) == '\t' ? 4 - (column & 3) : 1;
        indent += width;
        column += width;
        ++at;
    }
    *columns = indent > UINT32_MAX ? UINT32_MAX : (u32)indent;
    return at;
}

internal MdLine md_strip_columns(MdReader* reader, MdLine line, u32 columns) {
    u32 taken = min(line.indent_columns, columns);
    line.indent_columns -= taken;
    columns -= taken;
    u64 indent = md_line_column(reader, line);
    while (columns && line.content.begin < line.content.end) {
        i32 ch = md_reader_byte(reader, line.content.begin);
        if (!md_ascii_blank(ch))
            break;
        u32 width = ch == '\t' ? 4 - (indent & 3) : 1;
        ++line.content.begin;
        if (width > columns) {
            line.indent_columns = width - columns;
            break;
        }
        columns -= width;
        indent += width;
    }
    return line;
}

internal bool md_line_blank(MdReader* reader, MdLine line) {
    u32 columns;
    return md_skip_line_space(reader, line, &columns) == line.content.end;
}

internal bool md_block_interrupts(MdUpdateWork* work, MdLine line, bool paragraph) {
    MdReader* reader = &work->reader;
    u32 flags = work->next.flags;
    u32 indent, begin = md_skip_line_space(reader, line, &indent), end, value, ignored;
    if (begin == line.content.end)
        return true;
    if (indent >= 4 && !(flags & MD_FLAG_NOINDENTEDCODEBLOCKS))
        return false;
    if (md_reader_byte(reader, begin) == '>' ||
        md_is_atxheader_line(reader, flags, begin, &ignored, &end, &value) ||
        md_is_hr_line(reader, begin, &end, &ignored) ||
        md_is_opening_code_fence(reader, begin, &end, &value))
        return true;
    if (!(flags & MD_FLAG_NOHTMLBLOCKS)) {
        u32 mode = md_html_block_start(reader, begin, line.content.end);
        if (mode && (!paragraph || mode != 7))
            return true;
    }
    MdContainerFrame frame;
    if (md_is_container_mark(reader, flags, indent, begin, &end, &frame)) {
        bool ordered = frame.marker == '.' || frame.marker == ')';
        while (end < line.content.end && md_ascii_blank(md_reader_byte(reader, end)))
            ++end;
        return !paragraph || ((!ordered || frame.start == 1) && end < line.content.end);
    }
    return false;
}

internal MdBlockId md_parse_one(MdUpdateWork* work, ScratchArena* scratch, MdBlockId parent,
                                MdLine* lines, u32 count, u32* consumed);

internal bool md_parse_children(MdUpdateWork* work, ScratchArena* scratch, MdBlockId parent,
                                MdLine* lines, u32 count) {
    for (u32 at = 0; at < count && !work->error;) {
        u32 consumed = 0;
        MdBlockId child = md_parse_one(work, scratch, parent, lines + at, count - at, &consumed);
        if (child.index == UINT32_MAX || !consumed || !md_add_child(work, scratch, parent, child))
            return false;
        at += consumed;
    }
    return !work->error;
}

/* Definitions are stored as syntax blocks, not removed from edit coverage. */
internal MdBlockId md_parse_definition(MdUpdateWork* work, ScratchArena* scratch, MdBlockId parent,
                                       MdLine* lines, u32 count, u32* consumed) {
    u32 indent, begin = md_skip_line_space(&work->reader, lines[0], &indent);
    if (indent >= 4 || md_reader_byte(&work->reader, begin) != '[')
        return MD_ID_NONE;
    u32 available = 1;
    while (available < count && !md_line_blank(&work->reader, lines[available]))
        ++available;
    MdLine* candidate = md_work_clone(work, scratch, lines, available, sizeof(*lines));
    if (!candidate)
        return MD_ID_NONE;
    candidate[0].content.begin = begin;
    MdInlineWork text;
    if (!md_inline_text(work, scratch, MD_ID_NONE, candidate, available, 0, &text))
        return MD_ID_NONE;
    u32 at = 1, label_begin = at, characters = 0;
    bool footnote = (work->next.flags & MD_FLAG_FOOTNOTES) && at < text.text_count && text.text[at] == '^';
    if (footnote)
        label_begin = ++at;
    while (at < text.text_count && text.text[at] != ']') {
        if (text.text[at] == '[' || ++characters > 999)
            return MD_ID_NONE;
        if (text.text[at] == '\\' && at + 1 < text.text_count && md_ascii_punctuation(text.text[at + 1]))
            ++at;
        ++at;
    }
    if (at == label_begin || at + 1 >= text.text_count || text.text[at + 1] != ':')
        return MD_ID_NONE;
    u32 label_end = at;
    at += 2;
    MdPiece piece;
    MdView view;
    u32 start;
    MdReader logical = md_inline_reader(&text, &piece, &view, &start);
    MdRange destination = {0}, title = {0};
    bool has_title = false;
    u32 definition_end = at;
    if (!footnote) {
        u32 breaks = 0;
        while (at < text.text_count && (md_ascii_whitespace(text.text[at]) || text.text[at] == '\n')) {
            if (text.text[at] == '\n' && ++breaks > 1)
                return MD_ID_NONE;
            ++at;
        }
        if (!md_is_link_destination(&logical, at, text.text_count, &definition_end, &destination))
            return MD_ID_NONE;
        u32 after_title;
        if (md_link_title(&logical, definition_end, text.text_count, &after_title, &title)) {
            u32 tail = after_title;
            while (tail < text.text_count && md_ascii_blank(text.text[tail]))
                ++tail;
            if (tail == text.text_count || text.text[tail] == '\n') {
                has_title = true;
                definition_end = tail;
            }
        }
        while (definition_end < text.text_count && md_ascii_blank(text.text[definition_end]))
            ++definition_end;
        if (definition_end < text.text_count && text.text[definition_end] != '\n')
            return MD_ID_NONE;
    }
    MdLabelId label = md_label(work, scratch, text.text + label_begin, label_end - label_begin,
                               footnote ? MD_DEFINITION_FOOTNOTE : MD_DEFINITION_LINK);
    if (label.index == UINT32_MAX || !work->next.labels[label.index].key_count)
        return MD_ID_NONE;
    u32 used = 1;
    if (footnote) {
        used = available;
        while (used < count) {
            u32 columns;
            md_skip_line_space(&work->reader, lines[used], &columns);
            if (!md_line_blank(&work->reader, lines[used]) && columns < 4)
                break;
            ++used;
        }
    } else {
        u32 absolute_end = definition_end < text.text_count ? text.sources[definition_end].end : lines[available - 1].source.end;
        while (used < available && lines[used - 1].source.end < absolute_end)
            ++used;
    }
    u32 base = lines[0].source.begin;
    MdBlockId owner = md_new_block(work, scratch, parent, footnote ? MD_BLOCK_FOOTNOTE : MD_BLOCK_DEFINITION,
                                   base, lines[used - 1].source.end);
    if (owner.index == UINT32_MAX)
        return owner;
    MdDefinition definition = {.alive = true, .generation = 1, .owner = owner, .label = label,
                               .kind = footnote ? MD_DEFINITION_FOOTNOTE : MD_DEFINITION_LINK,
                               .source = {0, lines[used - 1].source.end - base}, .has_title = has_title};
    if (!footnote) {
        MdRange dest = md_inline_range(&text, destination.begin, destination.end);
        definition.destination = (MdRange){dest.begin - base, dest.end - base};
        if (has_title) {
            MdRange value = md_inline_range(&text, title.begin, title.end);
            definition.title = (MdRange){value.begin - base, value.end - base};
        }
    }
    MdDefinitionId id = {work->next.definitions_count, 1};
    if (work->next.first_free_definition != UINT32_MAX) {
        u32 index = work->next.first_free_definition;
        MdDefinition* retired = &work->next.definitions[index];
        id = (MdDefinitionId){index, retired->generation + 1};
        if (!id.generation) {
            work->error = MD_ERROR_OVERFLOW;
            return MD_ID_NONE;
        }
        work->next.first_free_definition = retired->next_free;
        definition.generation = id.generation;
    }
    if (id.index == work->next.definitions_count) {
        if (!md_work_push(work, scratch, (void**)&work->next.definitions, &work->next.definitions_count,
                          sizeof(definition), &definition))
            return MD_ID_NONE;
    } else {
        work->next.definitions[id.index] = definition;
    }
    md_block_at(&work->next, owner)->data.definition.definition = id;
    if (footnote) {
        MdLine* body = md_work_clone(work, scratch, lines, used, sizeof(*lines));
        if (!body)
            return MD_ID_NONE;
        body[0].content.begin = at < text.text_count ? text.sources[at].begin : body[0].content.end;
        if (body[0].content.begin > body[0].content.end)
            body[0].content.begin = body[0].content.end;
        if (body[0].content.begin < body[0].content.end && md_ascii_blank(md_reader_byte(&work->reader, body[0].content.begin)))
            ++body[0].content.begin;
        for (u32 i = 1; i < used; ++i)
            body[i] = md_strip_columns(&work->reader, body[i], 4);
        if (!md_parse_children(work, scratch, owner, body, used))
            return MD_ID_NONE;
    }
    *consumed = used;
    return owner;
}

internal bool md_hide_absolute(MdUpdateWork* work, ScratchArena* scratch, MdBlockId owner, u32 begin, u32 end) {
    if (begin >= end)
        return true;
    u32 base = md_block_base(&work->next, owner);
    MdNode node = {.kind = MD_NODE_HIDDEN, .start_idx = begin - base, .end_idx = end - base};
    return md_add_node(work, scratch, owner, node, MD_ID_NONE);
}

internal u32 md_table_cells(MdReader* reader, MdLine line, MdRange* cells, u32 capacity) {
    u32 begin = line.content.begin, end = line.content.end;
    while (begin < end && md_ascii_blank(md_reader_byte(reader, begin)))
        ++begin;
    while (end > begin && md_ascii_blank(md_reader_byte(reader, end - 1)))
        --end;
    if (begin < end && md_reader_byte(reader, begin) == '|')
        ++begin;
    if (end > begin && md_reader_byte(reader, end - 1) == '|') {
        u32 escapes = 0, at = end - 1;
        while (at > begin && md_reader_byte(reader, at - 1) == '\\') {
            ++escapes;
            --at;
        }
        if (!(escapes & 1))
            --end;
    }
    u32 count = 0, cell = begin;
    for (u32 at = begin; at <= end; ++at) {
        if (at < end && md_reader_byte(reader, at) == '\\' && at + 1 < end) {
            ++at;
            continue;
        }
        if (at == end || md_reader_byte(reader, at) == '|') {
            if (cells && count < capacity)
                cells[count] = (MdRange){cell, at};
            ++count;
            cell = at + 1;
        }
        if (at == end)
            break;
    }
    return count;
}

internal bool md_table_row(MdUpdateWork* work, ScratchArena* scratch, MdBlockId parent,
                           MdLine line, u32 row_number, u32 columns, const MdAlignment* alignment, bool header) {
    MdBlockId row = md_new_block(work, scratch, parent, MD_BLOCK_TABLE_ROW, line.source.begin, line.source.end);
    if (!md_add_child(work, scratch, parent, row))
        return false;
    MdRange* cells = md_work_alloc(work, scratch, (usize)columns * sizeof(*cells));
    if (!cells)
        return false;
    u32 count = md_table_cells(&work->reader, line, cells, columns);
    u32 cursor = line.source.begin;
    for (u32 i = 0; i < columns; ++i) {
        MdRange range = i < count ? cells[i] : (MdRange){line.content.end, line.content.end};
        if (!md_hide_absolute(work, scratch, row, cursor, range.begin))
            return false;
        MdBlockId cell = md_new_block(work, scratch, row, MD_BLOCK_TABLE_CELL, range.begin, range.end);
        if (!md_add_child(work, scratch, row, cell))
            return false;
        MdBlock* block = md_block_at(&work->next, cell);
        block->data.cell.row = row_number;
        block->data.cell.column = i;
        block->data.cell.header = header;
        block->data.cell.alignment = alignment[i];
        MdLine content = {.source = range, .content = range};
        while (content.content.begin < content.content.end && md_ascii_blank(md_reader_byte(&work->reader, content.content.begin)))
            ++content.content.begin;
        while (content.content.end > content.content.begin && md_ascii_blank(md_reader_byte(&work->reader, content.content.end - 1)))
            --content.content.end;
        if (!md_store_lines(work, scratch, cell, &content, 1))
            return false;
        cursor = range.end;
    }
    return md_hide_absolute(work, scratch, row, cursor, line.source.end);
}

/* Track whether an unmarked line may lazily continue a container paragraph.
 * Fence state prevents a code line from accidentally reopening a paragraph. */
internal void md_lazy_update(MdUpdateWork* work, MdLine line, bool* paragraph, u32* fence, u32* fence_length) {
    u32 indent, at = md_skip_line_space(&work->reader, line, &indent), end, length;
    while (indent < 4 && at < line.content.end) {
        if (md_reader_byte(&work->reader, at) == '>') {
            line.content.begin = at + 1;
            line.indent_columns = 0;
            if (md_ascii_blank(md_reader_byte(&work->reader, at + 1)))
                line = md_strip_columns(&work->reader, line, 1);
        } else {
            MdContainerFrame marker;
            u32 marker_end, ignored;
            if ((*paragraph && !md_block_interrupts(work, line, true)) ||
                md_is_hr_line(&work->reader, at, &marker_end, &ignored) ||
                !md_is_container_mark(&work->reader, work->next.flags, indent, at, &marker_end, &marker))
                break;
            line.content.begin = marker_end;
            line.indent_columns = 0;
            u32 padding;
            u32 content = md_skip_line_space(&work->reader, line, &padding);
            if (padding <= 4)
                line.content.begin = content;
            else
                line = md_strip_columns(&work->reader, line, 1);
        }
        *paragraph = false;
        at = md_skip_line_space(&work->reader, line, &indent);
    }
    if (*fence) {
        if (indent < 4 && md_is_closing_code_fence(&work->reader, at, *fence, *fence_length, &end))
            *fence = 0;
        *paragraph = false;
    } else if (indent < 4 && md_is_opening_code_fence(&work->reader, at, &end, &length)) {
        *fence = (u32)md_reader_byte(&work->reader, at);
        *fence_length = length;
        *paragraph = false;
    } else if (at == line.content.end || md_block_interrupts(work, line, *paragraph)) {
        *paragraph = false;
    } else if (*paragraph || indent < 4) {
        *paragraph = true;
    }
}

internal MdBlockId md_parse_one(MdUpdateWork* work, ScratchArena* scratch, MdBlockId parent,
                                MdLine* lines, u32 count, u32* consumed) {
    *consumed = 0;
    if (!count || work->error)
        return MD_ID_NONE;
    MdReader* reader = &work->reader;
    u32 flags = work->next.flags;
    u32 indent, begin = md_skip_line_space(reader, lines[0], &indent), end = 0, value = 0, ignored = 0;
    u32 base = lines[0].source.begin;
    if (begin == lines[0].content.end) {
        u32 used = 1;
        while (used < count && md_line_blank(reader, lines[used]))
            ++used;
        MdBlockId block = md_new_block(work, scratch, parent, MD_BLOCK_BLANK, base, lines[used - 1].source.end);
        if (block.index == UINT32_MAX)
            return block;
        md_block_at(&work->next, block)->data.blank.line_count = used;
        md_store_lines(work, scratch, block, lines, used);
        *consumed = used;
        return block;
    }
    bool indented_code = indent >= 4 && !(flags & MD_FLAG_NOINDENTEDCODEBLOCKS);
    if (!indented_code && md_reader_byte(reader, begin) == '>') {
        MdLine* body = md_work_clone(work, scratch, lines, count, sizeof(*lines));
        if (!body)
            return MD_ID_NONE;
        u32 used = 0, fence = 0, fence_length = 0;
        bool paragraph = false;
        for (; used < count; ++used) {
            u32 columns, at = md_skip_line_space(reader, lines[used], &columns);
            if (columns < 4 && md_reader_byte(reader, at) == '>') {
                body[used].content.begin = at + 1;
                body[used].indent_columns = 0;
                if (body[used].content.begin < body[used].content.end && md_ascii_blank(md_reader_byte(reader, at + 1))) {
                    i32 space = md_reader_byte(reader, at + 1);
                    ++body[used].content.begin;
                    if (space == '\t')
                        body[used].indent_columns = (4 - ((columns + 1) & 3)) - 1;
                }
                if (!used) {
                    MdLine after_marker = lines[used];
                    after_marker.content.begin = at + 1;
                    after_marker.indent_columns = 0;
                    u32 padding;
                    u32 content_begin = md_skip_line_space(reader, after_marker, &padding);
                    if (padding <= 4) {
                        body[used].content.begin = content_begin;
                        body[used].indent_columns = 0;
                    }
                }
                md_lazy_update(work, body[used], &paragraph, &fence, &fence_length);
            } else if (!paragraph || md_line_blank(reader, lines[used]) || md_block_interrupts(work, lines[used], false)) {
                break;
            }
        }
        MdBlockId quote = md_new_block(work, scratch, parent, MD_BLOCK_QUOTE, base, lines[used - 1].source.end);
        if (quote.index == UINT32_MAX)
            return quote;
        if (flags & MD_FLAG_ADMONITIONS) {
            u32 columns, at = md_skip_line_space(reader, body[0], &columns);
            const char* names[] = {"[!note]", "[!tip]", "[!important]", "[!warning]", "[!caution]"};
            for (u32 i = 0; i < 5; ++i) {
                u32 n = (u32)strlen(names[i]);
                if (body[0].content.end - at == n && md_reader_match(reader, at, names[i], n, true)) {
                    md_block_at(&work->next, quote)->kind = MD_BLOCK_ADMONITION;
                    md_block_at(&work->next, quote)->data.admonition.kind = (MdAdmonition)i;
                    body[0].content.begin = body[0].content.end;
                    break;
                }
            }
        }
        if (!md_parse_children(work, scratch, quote, body, used))
            return MD_ID_NONE;
        *consumed = used;
        return quote;
    }
    if (!indented_code && md_is_hr_line(reader, begin, &end, &ignored)) {
        MdBlockId block = md_new_block(work, scratch, parent, MD_BLOCK_THEMATIC_BREAK, base, lines[0].source.end);
        if (block.index == UINT32_MAX)
            return block;
        md_hide_absolute(work, scratch, block, base, lines[0].source.end);
        *consumed = 1;
        return block;
    }
    MdContainerFrame first;
    if (!indented_code && md_is_container_mark(reader, flags, indent, begin, &end, &first)) {
        MdBlockId list = md_new_block(work, scratch, parent, MD_BLOCK_LIST, base, base);
        if (list.index == UINT32_MAX)
            return list;
        MdBlock* list_block = md_block_at(&work->next, list);
        list_block->marker = first.marker;
        list_block->marker_indent = first.marker_indent;
        list_block->data.list.start = first.start;
        list_block->data.list.ordered = first.marker == '.' || first.marker == ')';
        u32 used = 0, number = first.start;
        bool loose = false;
        while (used < count) {
            u32 columns, at = md_skip_line_space(reader, lines[used], &columns), marker_end;
            MdContainerFrame marker;
            if (!md_is_container_mark(reader, flags, columns, at, &marker_end, &marker) ||
                marker.marker != first.marker || columns != first.marker_indent ||
                md_is_hr_line(reader, at, &ignored, &value))
                break;
            MdLine* body = md_work_clone(work, scratch, lines + used, count - used, sizeof(*lines));
            if (!body)
                return MD_ID_NONE;
            u32 content = marker_end, column = columns + marker_end - at;
            while (content < body[0].content.end && md_ascii_blank(md_reader_byte(reader, content))) {
                column = md_reader_byte(reader, content) == '\t' ? (column + 4) & ~3u : column + 1;
                ++content;
            }
            u32 padding = column - (columns + marker_end - at);
            if (!padding || padding > 4 || content == body[0].content.end) {
                padding = 1;
                body[0].content.begin = marker_end;
                body[0] = md_strip_columns(reader, body[0], padding);
            } else {
                body[0].content.begin = content;
                body[0].indent_columns = 0;
            }
            u32 required = columns + marker_end - at + padding;
            bool paragraph = false, had_blank = false, internal_blank = false;
            u32 fence = 0, fence_length = 0, item_count = 1;
            md_lazy_update(work, body[0], &paragraph, &fence, &fence_length);
            for (; used + item_count < count; ++item_count) {
                MdLine line = lines[used + item_count];
                u32 depth, cursor = md_skip_line_space(reader, line, &depth), next_end;
                MdContainerFrame next;
                if (md_line_blank(reader, line)) {
                    body[item_count] = md_strip_columns(reader, line, required);
                    had_blank = true;
                    paragraph = false;
                    if (item_count == 1 && md_line_blank(reader, body[0]))
                        break;
                    continue;
                }
                if (depth == columns && md_is_container_mark(reader, flags, depth, cursor, &next_end, &next) &&
                    next.marker == first.marker && !md_is_hr_line(reader, cursor, &ignored, &value)) {
                    loose |= had_blank;
                    break;
                }
                if (depth >= required) {
                    body[item_count] = md_strip_columns(reader, line, required);
                    internal_blank |= had_blank;
                    had_blank = false;
                    md_lazy_update(work, body[item_count], &paragraph, &fence, &fence_length);
                } else if (paragraph && !md_block_interrupts(work, line, true)) {
                    body[item_count] = line;
                } else {
                    break;
                }
            }
            MdBlockId item = md_new_block(work, scratch, list, MD_BLOCK_ITEM,
                                          lines[used].source.begin, lines[used + item_count - 1].source.end);
            if (!md_add_child(work, scratch, list, item))
                return MD_ID_NONE;
            MdBlock* item_block = md_block_at(&work->next, item);
            item_block->marker = first.marker;
            item_block->marker_indent = columns;
            item_block->content_indent = required;
            item_block->data.item.number = number++;
            loose |= internal_blank;
            u32 task = body[0].content.begin;
            if ((flags & MD_FLAG_TASKLISTS) && body[0].content.end - task >= 3 &&
                md_reader_byte(reader, task) == '[' && md_reader_byte(reader, task + 2) == ']' &&
                md_ascii_any(md_reader_byte(reader, task + 1), " xX") &&
                (task + 3 == body[0].content.end || md_ascii_blank(md_reader_byte(reader, task + 3)))) {
                item_block->data.item.task = true;
                item_block->data.item.checked = md_reader_byte(reader, task + 1) != ' ';
                body[0].content.begin = task + 3;
                if (body[0].content.begin < body[0].content.end)
                    ++body[0].content.begin;
            }
            if (!md_parse_children(work, scratch, item, body, item_count))
                return MD_ID_NONE;
            used += item_count;
        }
        if (!used) {
            work->error = MD_ERROR_INVALID_RANGE;
            return MD_ID_NONE;
        }
        list_block = md_block_at(&work->next, list);
        list_block->length = lines[used - 1].source.end - base;
        list_block->data.list.loose_children = loose;
        *consumed = used;
        return list;
    }
    bool fenced = !indented_code && md_is_opening_code_fence(reader, begin, &end, &value);
    if (fenced || indented_code) {
        u32 used = 1, content_first = fenced ? 1 : 0, content_count;
        bool closed = false;
        i32 marker = md_reader_byte(reader, begin);
        if (fenced) {
            for (; used < count; ++used) {
                u32 columns, at = md_skip_line_space(reader, lines[used], &columns), close;
                if (columns < 4 && md_is_closing_code_fence(reader, at, (u32)marker, value, &close)) {
                    closed = true;
                    break;
                }
            }
            content_count = used - 1;
            if (closed)
                ++used;
        } else {
            u32 last_content = 1;
            for (; used < count; ++used) {
                u32 columns;
                md_skip_line_space(reader, lines[used], &columns);
                if (!md_line_blank(reader, lines[used])) {
                    if (columns < 4)
                        break;
                    last_content = used + 1;
                }
            }
            used = last_content;
            content_count = used;
        }
        MdBlockId block = md_new_block(work, scratch, parent, MD_BLOCK_CODE, base, lines[used - 1].source.end);
        if (block.index == UINT32_MAX)
            return block;
        MdLine* body = md_work_clone(work, scratch, lines + content_first, content_count, sizeof(*lines));
        if (!body)
            return MD_ID_NONE;
        for (u32 i = 0; i < content_count; ++i)
            body[i] = md_strip_columns(reader, body[i], fenced ? indent : 4);
        MdBlock* code = md_block_at(&work->next, block);
        code->data.code.fenced = fenced;
        code->data.code.content = content_count ? (MdRange){body[0].source.begin - base,
                                         body[content_count - 1].source.end - base} :
                                         (MdRange){lines[0].source.end - base, lines[0].source.end - base};
        md_store_lines(work, scratch, block, body, content_count);
        *consumed = used;
        return block;
    }
    if (md_is_atxheader_line(reader, flags, begin, &ignored, &end, &value)) {
        MdLine line = lines[0];
        line.content.begin = ignored;
        while (line.content.end > line.content.begin && md_ascii_blank(md_reader_byte(reader, line.content.end - 1)))
            --line.content.end;
        u32 tail = line.content.end;
        while (tail > line.content.begin && md_reader_byte(reader, tail - 1) == '#')
            --tail;
        if (tail < line.content.end && (tail == line.content.begin || md_ascii_blank(md_reader_byte(reader, tail - 1)))) {
            line.content.end = tail;
            while (line.content.end > line.content.begin && md_ascii_blank(md_reader_byte(reader, line.content.end - 1)))
                --line.content.end;
        }
        MdBlockId heading = md_new_block(work, scratch, parent, MD_BLOCK_HEADING, base, line.source.end);
        if (heading.index == UINT32_MAX)
            return heading;
        md_block_at(&work->next, heading)->data.heading.level = value;
        md_store_lines(work, scratch, heading, &line, 1);
        *consumed = 1;
        return heading;
    }
    u32 html = flags & MD_FLAG_NOHTMLBLOCKS ? 0 : md_html_block_start(reader, begin, lines[0].content.end);
    if (html) {
        u32 used = 0;
        do {
            if (html >= 6 && md_line_blank(reader, lines[used]))
                break;
            bool closes = md_html_block_end(reader, html, lines[used].content);
            ++used;
            if (closes)
                break;
        } while (used < count);
        MdBlockId block = md_new_block(work, scratch, parent, MD_BLOCK_HTML, base, lines[used - 1].source.end);
        if (block.index == UINT32_MAX)
            return block;
        md_block_at(&work->next, block)->data.html.mode = html;
        md_store_lines(work, scratch, block, lines, used);
        *consumed = used;
        return block;
    }
    MdBlockId definition = md_parse_definition(work, scratch, parent, lines, count, consumed);
    if (definition.index != UINT32_MAX || work->error)
        return definition;
    u32 used = 1, level = 0, columns = 0;
    bool table = false;
    for (; used < count; ++used) {
        u32 depth, at = md_skip_line_space(reader, lines[used], &depth);
        if (depth < 4 && md_is_setext_underline(reader, at, &end, &level)) {
            ++used;
            break;
        }
        if (used == 1 && depth < 4 && (flags & MD_FLAG_TABLES) &&
            md_is_table_underline(reader, at, &end, &columns) &&
            columns == md_table_cells(reader, lines[0], NULL, 0)) {
            table = true;
            ++used;
            while (used < count && !md_line_blank(reader, lines[used]) && !md_block_interrupts(work, lines[used], true))
                ++used;
            break;
        }
        level = 0;
        if (md_block_interrupts(work, lines[used], true))
            break;
    }
    if (table) {
        MdAlignment* alignment = md_work_alloc(work, scratch, (usize)columns * sizeof(*alignment));
        if (!alignment)
            return MD_ID_NONE;
        md_analyze_table_alignment(reader, lines[1].content, alignment, columns);
        MdBlockId block = md_new_block(work, scratch, parent, MD_BLOCK_TABLE, base, lines[used - 1].source.end);
        if (block.index == UINT32_MAX)
            return block;
        md_block_at(&work->next, block)->data.table.columns = columns;
        md_block_at(&work->next, block)->data.table.rows = used - 1;
        MdBlockId head = md_new_block(work, scratch, block, MD_BLOCK_TABLE_HEAD, base, lines[0].source.end);
        if (!md_add_child(work, scratch, block, head) || !md_table_row(work, scratch, head, lines[0], 0, columns, alignment, true))
            return MD_ID_NONE;
        md_hide_absolute(work, scratch, block, lines[1].source.begin, lines[1].source.end);
        if (used > 2) {
            MdBlockId body = md_new_block(work, scratch, block, MD_BLOCK_TABLE_BODY, lines[2].source.begin, lines[used - 1].source.end);
            if (!md_add_child(work, scratch, block, body))
                return MD_ID_NONE;
            for (u32 i = 2; i < used; ++i)
                if (!md_table_row(work, scratch, body, lines[i], i - 1, columns, alignment, false))
                    return MD_ID_NONE;
        }
        *consumed = used;
        return block;
    }
    MdBlockId block = md_new_block(work, scratch, parent, level ? MD_BLOCK_HEADING : MD_BLOCK_PARAGRAPH,
                                   base, lines[used - 1].source.end);
    if (block.index == UINT32_MAX)
        return block;
    if (level)
        md_block_at(&work->next, block)->data.heading.level = level;
    u32 body_count = used - (level != 0);
    MdLine* body = md_work_clone(work, scratch, lines, body_count, sizeof(*lines));
    if (!body)
        return MD_ID_NONE;
    for (u32 i = 0; i < body_count; ++i) {
        if (level) {
            u32 depth;
            body[i].content.begin = md_skip_line_space(reader, body[i], &depth);
        } else {
            /* Port md_add_line_into_current_block's paragraph text_begin:
             * the fork restores original indentation bytes backwards, with
             * a tab accounting for up to four indentation columns. */
            u32 columns;
            u32 text_begin = md_skip_line_space(reader, body[i], &columns);
            while (columns && text_begin > body[i].source.begin && md_ascii_blank(md_reader_byte(reader, text_begin - 1))) {
                --text_begin;
                u32 width = md_reader_byte(reader, text_begin) == '\t' ? 4 : 1;
                columns -= min(columns, width);
            }
            body[i].content.begin = text_begin;
        }
        body[i].indent_columns = 0;
    }
    md_store_lines(work, scratch, block, body, body_count);
    *consumed = used;
    return block;
}

// =====================================================================================================================
// Inline resolution. Completed syntax writes annotations, never events.
// =====================================================================================================================

internal bool md_inline_claimed(const MdInlineWork* text, u32 begin, u32 end) {
    for (u32 i = begin; i < end; ++i)
        if (text->claimed[i])
            return true;
    return false;
}

internal u32* md_inline_code_closers(MdInlineWork* text, ScratchArena* scratch) {
    u32* next = md_work_alloc(text->update, scratch, ((usize)text->text_count + 1) * sizeof(u32));
    if (!next)
        return NULL;
    u32 heads[33];
    for (u32 i = 0; i < 33; ++i)
        heads[i] = UINT32_MAX;
    for (u32 at = text->text_count; at;) {
        --at;
        if (text->text[at] != '`')
            continue;
        u32 end = at + 1;
        while (at && text->text[at - 1] == '`')
            --at;
        u32 width = end - at;
        /* A backslash can escape an opener but is literal inside code. Every
         * full run is therefore a potential closer, including escaped runs.
         * A partly escaped opening run can start at any suffix position. */
        for (u32 p = at; p < end; ++p)
            next[p] = end - p <= 32 ? heads[end - p] : UINT32_MAX;
        if (width <= 32)
            heads[width] = at;
    }
    return next;
}

internal bool md_inline_verbatim(MdInlineWork* text, ScratchArena* scratch, u32 begin, u32 end, u8 newline) {
    for (u32 i = begin; i < end; ++i) {
        if (text->text[i] == '\n') {
            if (!md_inline_replace(text, scratch, i, i + 1, &newline, 1, 1))
                return false;
            for (u32 next_line = i + 1; next_line < end && md_ascii_blank(text->text[next_line]); ++next_line)
                text->claimed[next_line] = 1;
        } else if (!text->text[i]) {
            const u8 replacement[] = {0xef, 0xbf, 0xbd};
            if (!md_inline_replace(text, scratch, i, i + 1, replacement, 3, 1))
                return false;
        }
    }
    return true;
}

internal bool md_inline_code(MdInlineWork* text, ScratchArena* scratch, const u32* next, u32 at, u32* after) {
    u32 open_end = at + 1;
    while (open_end < text->text_count && text->text[open_end] == '`' && !text->claimed[open_end])
        ++open_end;
    u32 close = next[at];
    if (close == UINT32_MAX) {
        *after = open_end;
        return true;
    }
    u32 end = close + open_end - at;
    bool only_spaces = true;
    for (u32 i = open_end; i < close; ++i)
        only_spaces &= text->text[i] == ' ' || text->text[i] == '\n';
    u32 content_begin = open_end, content_end = close;
    if (!only_spaces && content_begin < content_end &&
        (text->text[content_begin] == ' ' || text->text[content_begin] == '\n') &&
        (text->text[content_end - 1] == ' ' || text->text[content_end - 1] == '\n')) {
        ++content_begin;
        --content_end;
    }
    if (!md_inline_node(text, scratch, MD_NODE_CODE_INLINE, at, end, content_begin, content_end, MD_ID_NONE))
        return false;
    md_claim(text, at, content_begin, 1);
    md_claim(text, content_begin, content_end, 3);
    md_claim(text, content_end, end, 1);
    if (!md_inline_verbatim(text, scratch, content_begin, content_end, ' '))
        return false;
    *after = end;
    return true;
}

internal bool md_inline_link_node(MdInlineWork* text, ScratchArena* scratch, MdNodeKind kind,
                                  u32 begin, u32 content_begin, u32 content_end, u32 end,
                                  MdLinkWork attributes) {
    MdRange range = md_inline_range(text, begin, end);
    MdNode node = {.kind = kind, .start_idx = range.begin, .end_idx = range.end};
    if (attributes.reference.index == UINT32_MAX) {
        MdRange dest = md_inline_range(text, attributes.destination.begin, attributes.destination.end);
        MdRange title = attributes.has_title ? md_inline_range(text, attributes.title.begin, attributes.title.end) : (MdRange){0};
        (void)title;
        node.data.link.dest_start = dest.begin;
        node.data.link.dest_end = dest.end;
        node.data.link.has_title = attributes.has_title;
    }
    node.data.link.prefix = attributes.prefix;
    if (!md_node_content(&node, md_inline_range(text, content_begin, content_end))) {
        text->update->error = MD_ERROR_OVERFLOW;
        return false;
    }
    /* Attribute text wins over any tentative inline syntax wholly inside it. */
    MdRange tail = md_inline_range(text, content_end, end);
    MdBlock* block = md_block_at(&text->update->next, text->owner);
    u32 kept = 0;
    for (u32 i = 0; i < block->annotations_count; ++i) {
        MdAnnotation annotation = block->annotations[i];
        if (annotation.node.start_idx >= tail.begin && annotation.node.end_idx <= tail.end)
            continue;
        block->annotations[kept++] = annotation;
    }
    block->annotations_count = kept;
    md_claim(text, begin, content_begin, 1);
    md_claim(text, content_end, end, 1);
    return md_add_node(text->update, scratch, text->owner, node, attributes.reference);
}

internal bool md_inline_brackets(MdInlineWork* text, ScratchArena* scratch) {
    MdUpdateWork* work = text->update;
    MdPiece piece;
    MdView view;
    u32 start;
    MdReader reader = md_inline_reader(text, &piece, &view, &start);
    MdMark* stack = NULL;
    u32 stack_count = 0;
    for (u32 at = 0; at < text->text_count && !work->error; ++at) {
        if (text->claimed[at])
            continue;
        if ((work->next.flags & MD_FLAG_WIKILINKS) && text->text[at] == '[' &&
            at + 1 < text->text_count && text->text[at + 1] == '[') {
            u32 close = at + 2, pipe = UINT32_MAX;
            while (close + 1 < text->text_count && text->text[close] != '\n') {
                if (text->text[close] == '|' && pipe == UINT32_MAX)
                    pipe = close;
                if (text->text[close] == ']' && text->text[close + 1] == ']')
                    break;
                ++close;
            }
            if (close + 1 < text->text_count && text->text[close] == ']' && close > at + 2) {
                MdLinkWork attr = {.reference = MD_ID_NONE, .destination = {at + 2, pipe == UINT32_MAX ? close : pipe}};
                u32 label = pipe == UINT32_MAX ? at + 2 : pipe + 1;
                if (!md_inline_link_node(text, scratch, MD_NODE_WIKI_LINK, at, label, close, close + 2, attr))
                    return false;
                at = close + 1;
                continue;
            }
        }
        bool image = text->text[at] == '!' && at + 1 < text->text_count && text->text[at + 1] == '[' && !text->claimed[at + 1];
        if (text->text[at] == '[' || image) {
            MdMark mark = {.begin = at, .end = at + (image ? 2 : 1), .character = image ? '!' : '[', .flags = 1};
            if (!md_work_push(work, scratch, (void**)&stack, &stack_count, sizeof(mark), &mark))
                return false;
            at += image;
            continue;
        }
        if (text->text[at] != ']' || !stack_count)
            continue;
        MdMark opener = stack[--stack_count];
        if (!opener.flags)
            continue;
        u32 label_begin = opener.end, label_end = at, end = at + 1;
        bool resolved = false;
        MdNodeKind kind = opener.character == '!' ? MD_NODE_IMAGE : MD_NODE_LINK;
        MdLinkWork attributes = {.reference = MD_ID_NONE};
        if (kind == MD_NODE_LINK && (work->next.flags & MD_FLAG_FOOTNOTES) &&
            label_begin < label_end && text->text[label_begin] == '^') {
            MdDefinitionId definition = md_reference(text, scratch, text->text + label_begin + 1,
                                                     label_end - label_begin - 1, MD_DEFINITION_FOOTNOTE, &attributes.reference);
            if (definition.index != UINT32_MAX) {
                if (!md_inline_node(text, scratch, MD_NODE_FOOTNOTE_REFERENCE, opener.begin, end, opener.begin, opener.begin, attributes.reference))
                    return false;
                md_claim(text, opener.begin, end, 1);
                continue;
            }
        }
        if (end < text->text_count && text->text[end] == '(') {
            u32 cursor = end + 1, breaks = 0;
            while (cursor < text->text_count && (md_ascii_whitespace(text->text[cursor]) || text->text[cursor] == '\n')) {
                if (text->text[cursor] == '\n' && ++breaks > 1)
                    break;
                ++cursor;
            }
            u32 after_destination = cursor;
            if (breaks <= 1 && ((cursor < text->text_count && text->text[cursor] == ')') ||
                md_is_link_destination(&reader, cursor, text->text_count, &after_destination, &attributes.destination))) {
                if (cursor == after_destination)
                    attributes.destination = (MdRange){cursor, cursor};
                u32 after_title;
                if (md_link_title(&reader, after_destination, text->text_count, &after_title, &attributes.title)) {
                    attributes.has_title = true;
                    after_destination = after_title;
                }
                breaks = 0;
                while (after_destination < text->text_count &&
                       (md_ascii_whitespace(text->text[after_destination]) || text->text[after_destination] == '\n')) {
                    if (text->text[after_destination] == '\n' && ++breaks > 1)
                        break;
                    ++after_destination;
                }
                if (breaks <= 1 && after_destination < text->text_count && text->text[after_destination] == ')') {
                    resolved = true;
                    end = after_destination + 1;
                }
            }
        }
        if (!resolved) {
            u32 key_begin = label_begin, key_end = label_end;
            bool valid = true;
            if (end < text->text_count && text->text[end] == '[') {
                u32 close = end + 1;
                while (close < text->text_count && text->text[close] != ']') {
                    if (text->text[close] == '[') {
                        valid = false;
                        break;
                    }
                    if (text->text[close] == '\\' && close + 1 < text->text_count)
                        ++close;
                    ++close;
                }
                if (valid && close < text->text_count) {
                    if (close != end + 1) {
                        key_begin = end + 1;
                        key_end = close;
                    }
                    end = close + 1;
                } else {
                    valid = false;
                }
            }
            for (u32 i = key_begin; valid && i < key_end; ++i) {
                if (text->text[i] == '\\' && i + 1 < key_end)
                    ++i;
                else if (text->text[i] == '[' || text->text[i] == ']')
                    valid = false;
            }
            if (valid && key_end > key_begin && key_end - key_begin <= 3996) {
                MdDefinitionId definition = md_reference(text, scratch, text->text + key_begin, key_end - key_begin,
                                                         MD_DEFINITION_LINK, &attributes.reference);
                resolved = definition.index != UINT32_MAX;
            }
        }
        if (resolved) {
            if (!md_inline_link_node(text, scratch, kind, opener.begin, label_begin, label_end, end, attributes))
                return false;
            if (kind == MD_NODE_LINK)
                for (u32 i = 0; i < stack_count; ++i)
                    if (stack[i].character == '[')
                        stack[i].flags = 0;
            at = end - 1;
        }
    }
    return !work->error;
}

internal i32 md_codepoint_before(const u8* text, u32 position) {
    if (!position)
        return ' ';
    u32 at = position - 1;
    for (u32 n = 0; at && (text[at] & 0xc0) == 0x80 && n < 3; ++n)
        --at;
    i32 cp;
    md_utf8(text + at, position - at, &cp);
    return cp;
}

internal bool md_inline_delimiters(MdInlineWork* text, ScratchArena* scratch) {
    MdUpdateWork* work = text->update;
    u32 flags = work->next.flags;
    for (u32 at = 0; at < text->text_count;) {
        u8 ch = text->text[at];
        bool enabled = ch == '*' || ch == '_' ||
            (ch == '~' && (flags & (MD_FLAG_STRIKETHROUGH | MD_FLAG_SUBSCRIPTS))) ||
            (ch == '^' && (flags & MD_FLAG_SUPERSCRIPTS)) ||
            (ch == '+' && (flags & MD_FLAG_INSERT)) || (ch == '=' && (flags & MD_FLAG_HIGHLIGHT)) ||
            (ch == '|' && (flags & MD_FLAG_SPOILERS));
        if (!enabled || text->claimed[at]) {
            ++at;
            continue;
        }
        u32 end = at + 1;
        while (end < text->text_count && text->text[end] == ch && !text->claimed[end])
            ++end;
        if (ch == '|' && end - at >= 2)
            end = at + 2;
        u32 width = end - at;
        if (((ch == '+' || ch == '=' || ch == '|') && width != 2) ||
            (ch == '~' && width > 2) || (ch == '^' && width != 1) ||
            (ch == '~' && width == 2 && !(flags & MD_FLAG_STRIKETHROUGH))) {
            at = end;
            continue;
        }
        i32 before = md_codepoint_before(text->text, at), after = ' ';
        if (end < text->text_count)
            md_utf8(text->text + end, text->text_count - end, &after);
        bool before_space = md_unicode_space(before), after_space = md_unicode_space(after);
        bool before_punct = md_unicode_punct(before), after_punct = md_unicode_punct(after);
        bool opens = !after_space && (!after_punct || before_space || before_punct);
        bool closes = !before_space && (!before_punct || after_space || after_punct);
        if (ch == '^' || ch == '+' || ch == '=' || (ch == '~' && width == 1 && (flags & MD_FLAG_SUBSCRIPTS))) {
            opens = !after_space;
            closes = !before_space;
        } else if (ch == '~') {
            opens = before_space || before_punct;
            closes = after_space || after_punct;
        } else if (ch == '|') {
            opens = closes = true;
        }
        if (ch == '_' && !(flags & MD_FLAG_UNDERLINE)) {
            bool left = opens, right = closes;
            opens = left && (!right || before_punct);
            closes = right && (!left || after_punct);
        }
        MdMark mark = {.begin = at, .end = end, .character = ch,
                       .flags = (u8)((opens ? 1 : 0) | (closes ? 2 : 0)), .auxiliary = width};
        if (!md_work_push(work, scratch, (void**)&text->marks, &text->marks_count, sizeof(mark), &mark))
            return false;
        at = end;
    }
    u32 bottoms[128][6] = {{0}};
    for (u32 i = 0; i < text->marks_count; ++i) {
        MdMark* close = &text->marks[i];
        while ((close->flags & 2) && close->begin < close->end) {
            u32 found = UINT32_MAX;
            u32 bucket = close->auxiliary % 3 + ((close->flags & 1) ? 3 : 0);
            u32 lower = bottoms[close->character][bucket];
            for (u32 j = i; j > lower;) {
                MdMark* open = &text->marks[--j];
                if (!(open->flags & 1) || open->character != close->character || open->begin == open->end)
                    continue;
                bool emphasis = open->character == '*' || (open->character == '_' && !(flags & MD_FLAG_UNDERLINE));
                if (emphasis && ((open->flags & 2) || (close->flags & 1)) &&
                    (open->auxiliary + close->auxiliary) % 3 == 0 &&
                    (open->auxiliary % 3 || close->auxiliary % 3))
                    continue;
                if (!emphasis && open->character != '_' && open->end - open->begin != close->end - close->begin)
                    continue;
                u32 match = emphasis ? min(open->end - open->begin, close->end - close->begin) : open->end - open->begin;
                if (emphasis)
                    match = min(match, 2u);
                if (!md_inline_nests(text, open->end - match, close->begin + match))
                    continue;
                found = j;
                break;
            }
            if (found == UINT32_MAX) {
                bottoms[close->character][bucket] = i;
                break;
            }
            MdMark* open = &text->marks[found];
            u32 width;
            MdNodeKind kind;
            switch (open->character) {
                case '*':
                case '_':
                    if (open->character == '_' && (flags & MD_FLAG_UNDERLINE)) {
                        width = 1;
                        kind = MD_NODE_UNDERLINE;
                    } else {
                        width = min(open->end - open->begin, close->end - close->begin) >= 2 ? 2 : 1;
                        kind = width == 2 ? MD_NODE_STRONG : MD_NODE_EMPHASIS;
                    }
                    break;
                case '~': width = open->end - open->begin;
                    kind = width == 1 && (flags & MD_FLAG_SUBSCRIPTS) ? MD_NODE_SUBSCRIPT : MD_NODE_STRIKETHROUGH; break;
                case '^': width = 1; kind = MD_NODE_SUPERSCRIPT; break;
                case '+': width = 2; kind = MD_NODE_INSERT; break;
                case '=': width = 2; kind = MD_NODE_HIGHLIGHT; break;
                default: width = 2; kind = MD_NODE_SPOILER; break;
            }
            if (!md_inline_node(text, scratch, kind, open->end - width, close->begin + width, open->end, close->begin, MD_ID_NONE))
                return false;
            md_claim(text, open->end - width, open->end, 1);
            md_claim(text, close->begin, close->begin + width, 1);
            open->end -= width;
            close->begin += width;
            for (u32 j = found + 1; j < i; ++j)
                text->marks[j].flags &= ~1u;
        }
    }
    return true;
}

internal u32 md_entity_decode(const u8* source, u32 length, u8 output[8]) {
    if (length < 3)
        return 0;
    if (source[1] == '#') {
        u32 at = 2, radix = 10, cp = 0;
        if (source[at] == 'x' || source[at] == 'X') {
            radix = 16;
            ++at;
        }
        for (; at + 1 < length; ++at) {
            u32 digit = source[at] <= '9' ? source[at] - '0' : (source[at] | 32) - 'a' + 10;
            cp = cp * radix + digit;
        }
        if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            cp = 0xfffd;
        return (u32)utf8proc_encode_char((i32)cp, output);
    }
    u32 lo = 0, hi = sizeof(md_entities) / sizeof(md_entities[0]);
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        usize size = strlen(md_entities[mid].name);
        int cmp = memcmp(source + 1, md_entities[mid].name, min(size, length - 2));
        if (!cmp)
            cmp = (length - 2 > size) - (length - 2 < size);
        if (cmp < 0)
            hi = mid;
        else if (cmp > 0)
            lo = mid + 1;
        else {
            u32 bytes = (u32)utf8proc_encode_char(md_entities[mid].first, output);
            if (md_entities[mid].second)
                bytes += (u32)utf8proc_encode_char(md_entities[mid].second, output + bytes);
            return bytes;
        }
    }
    return 0;
}

internal bool md_inside_link(MdInlineWork* text, u32 logical) {
    u32 source = text->sources[logical].begin;
    MdBlock* block = md_block_at(&text->update->next, text->owner);
    for (u32 i = 0; i < block->annotations_count; ++i) {
        const MdNode* node = &block->annotations[i].node;
        if ((node->kind == MD_NODE_LINK || node->kind == MD_NODE_IMAGE || node->kind == MD_NODE_WIKI_LINK) &&
            node->start_idx <= source && source < node->end_idx)
            return true;
    }
    return false;
}

/* md_analyze_permissive_autolink_segment(), with resolved syntax represented
 * by source claims rather than MD4C's mark-chain traversal. */
internal i32 md_autolink_segment(MdInlineWork* text, u32 at, u32 limit, u32* end,
                                bool backwards, u8 delimiter, const char* extra, const char* separators) {
    i32 components = 0, brackets = 0;
    bool word_boundary = true, component_boundary = true;
    u32 component_begin = at;
    while (at != limit) {
        if (backwards)
            --at;
        u8 ch = text->text[at];
        if (!md_ascii_alnum(ch) && !md_ascii_whitespace(ch) && text->claimed[at]) {
            at += backwards;
            break;
        }
        if (!backwards) {
            if (ch == '(')
                ++brackets;
            else if (ch == ')') {
                if (brackets <= 0)
                    break;
                --brackets;
            }
        }
        if (md_ascii_alnum(ch) || md_ascii_any(ch, extra)) {
            word_boundary = component_boundary = false;
        } else {
            if (word_boundary)
                break;
            if (md_ascii_any(ch, separators)) {
                word_boundary = true;
            } else if (delimiter && ch == delimiter) {
                if (component_boundary)
                    break;
                component_boundary = true;
                component_begin = at;
                ++components;
            } else {
                at += backwards;
                break;
            }
        }
        if (!backwards)
            ++at;
    }
    if (word_boundary || component_boundary)
        at = backwards ? at + 1 : (at ? at - 1 : 0);
    if (at != component_begin)
        ++components;
    if (brackets)
        return -1;
    *end = at;
    return components;
}

internal bool md_inline_autolinks(MdInlineWork* text, ScratchArena* scratch) {
    u32 flags = text->update->next.flags;
    if (!(flags & MD_FLAG_PERMISSIVEAUTOLINKS))
        return true;
    u32 line_begin = 0, line_end = 0;
    while (line_end < text->text_count && text->text[line_end] != '\n')
        ++line_end;
    for (u32 at = 0; at < text->text_count; ++at) {
        if (at == line_end) {
            line_begin = at + 1;
            line_end = line_begin;
            while (line_end < text->text_count && text->text[line_end] != '\n')
                ++line_end;
            continue;
        }
        if (text->claimed[at] || md_inside_link(text, at))
            continue;
        u32 begin = at, end = at;
        bool email = false, candidate = false;
        MdLinkPrefix prefix = MD_LINK_PREFIX_NONE;
        if (text->text[at] == ':' && (flags & MD_FLAG_PERMISSIVEURLAUTOLINKS)) {
            const char* schemes[] = {"http", "https", "ftp"};
            for (u32 i = 0; i < 3; ++i) {
                u32 n = (u32)strlen(schemes[i]);
                if (at - line_begin >= n && line_end - at > 3 && !memcmp(text->text + at - n, schemes[i], n) &&
                    text->text[at + 1] == '/' && text->text[at + 2] == '/') {
                    begin = at - n;
                    end = at + 3;
                    candidate = true;
                    break;
                }
            }
        } else if (text->text[at] == '.' && (flags & MD_FLAG_PERMISSIVEWWWAUTOLINKS) &&
                   at - line_begin >= 3 && !memcmp(text->text + at - 3, "www", 3)) {
            begin = at - 3;
            end = at + 1;
            prefix = MD_LINK_PREFIX_HTTP;
            candidate = true;
        } else if (text->text[at] == '@' && (flags & MD_FLAG_PERMISSIVEEMAILAUTOLINKS) && at > line_begin &&
                   line_end - at > 3 && md_ascii_alnum(text->text[at - 1]) && md_ascii_alnum(text->text[at + 1])) {
            if (md_autolink_segment(text, at, line_begin, &begin, true, 0, "", ".-_+") < 1)
                continue;
            if (begin - line_begin >= 7 && !memcmp(text->text + begin - 7, "mailto:", 7))
                begin -= 7;
            else if (begin - line_begin >= 5 && !memcmp(text->text + begin - 5, "xmpp:", 5))
                begin -= 5;
            else
                prefix = MD_LINK_PREFIX_MAILTO;
            end = at + 1;
            candidate = email = true;
        }
        if (!candidate || (begin > line_begin && !md_unicode_space(md_codepoint_before(text->text, begin)) &&
            !md_ascii_any(text->text[begin - 1], "({[") && text->claimed[begin - 1] != 1))
            continue;
        if (md_autolink_segment(text, end, line_end, &end, false, '.', "", "-_") < 2)
            continue;
        if (!email) {
            if (end < line_end && text->text[end] == '/') {
                if (md_autolink_segment(text, end + 1, line_end, &end, false, '/', ".+-_~%", "") < 0)
                    continue;
                if (end < line_end && text->text[end] == '/')
                    ++end;
            }
            if (end < line_end && text->text[end] == '?' &&
                md_autolink_segment(text, end + 1, line_end, &end, false, '&', "._=()", "+-") < 0)
                continue;
            if (end < line_end && text->text[end] == '#' &&
                md_autolink_segment(text, end + 1, line_end, &end, false, 0, "", ".-+_") < 0)
                continue;
        }
        i32 next = ' ';
        if (end < line_end)
            md_utf8(text->text + end, line_end - end, &next);
        if (end < line_end && !md_unicode_space(next) && !md_ascii_any(next, ")}].!?,;") && text->claimed[end] != 1)
            continue;
        if (!md_inline_nests(text, begin, end))
            continue;
        MdLinkWork attributes = {.reference = MD_ID_NONE, .destination = {begin, end}, .prefix = prefix};
        if (!md_inline_link_node(text, scratch, MD_NODE_LINK, begin, begin, end, end, attributes))
            return false;
        at = end - 1;
    }
    return true;
}

internal bool md_inline_finish(MdInlineWork* text, ScratchArena* scratch) {
    MdUpdateWork* work = text->update;
    MdPiece piece;
    MdView view;
    u32 start;
    MdReader reader = md_inline_reader(text, &piece, &view, &start);
    for (u32 at = 0; at < text->text_count; ++at) {
        if (text->claimed[at])
            continue;
        u32 end;
        if (text->text[at] == '&' && md_is_entity(&reader, at, text->text_count, &end)) {
            u8 bytes[8];
            u32 length = md_entity_decode(text->text + at, end - at, bytes);
            if (length) {
                if (!md_inline_replace(text, scratch, at, end, bytes, length, 1))
                    return false;
                at = end - 1;
                continue;
            }
        }
        if (!text->text[at]) {
            const u8 replacement[] = {0xef, 0xbf, 0xbd};
            if (!md_inline_replace(text, scratch, at, at + 1, replacement, 3, 1))
                return false;
        } else if (text->text[at] == '\n') {
            u32 begin = at;
            while (begin && md_ascii_blank(text->text[begin - 1]) && !text->claimed[begin - 1])
                --begin;
            bool hard = (at - begin >= 2 && text->text[at - 1] == ' ' && text->text[at - 2] == ' ') ||
                        (work->next.flags & MD_FLAG_HARD_SOFT_BREAKS);
            if (begin == at && at && text->text[at - 1] == '\\' && !text->claimed[at - 1]) {
                --begin;
                hard = true;
            }
            if (hard || md_block_at(&work->next, text->owner)->kind != MD_BLOCK_PARAGRAPH)
                md_claim(text, begin, at, 1);
            if (!md_inline_node(text, scratch, hard ? MD_NODE_HARD_BREAK : MD_NODE_SOFT_BREAK,
                                at, at + 1, at, at + 1, MD_ID_NONE))
                return false;
            if (text->sources[at].end - text->sources[at].begin != 1) {
                const u8 newline = '\n';
                if (!md_inline_replace(text, scratch, at, at + 1, &newline, 1, 1))
                    return false;
            }
        } else if ((work->next.flags & MD_FLAG_COLLAPSEWHITESPACE) && md_ascii_blank(text->text[at])) {
            u32 end_space = at + 1;
            while (end_space < text->text_count && md_ascii_blank(text->text[end_space]) && !text->claimed[end_space])
                ++end_space;
            const u8 space = ' ';
            if (!md_inline_replace(text, scratch, at, end_space, &space, 1, 1))
                return false;
            at = end_space - 1;
        }
    }
    u32 cursor = 0;
    for (u32 at = 0; at < text->text_count; ++at) {
        MdRange source = text->sources[at];
        if (source.begin > cursor && !md_hide_absolute(work, scratch, text->owner,
                                                       text->base + cursor, text->base + source.begin))
            return false;
        if (text->claimed[at] == 1 && !md_hide_absolute(work, scratch, text->owner,
                                                       text->base + source.begin, text->base + source.end))
            return false;
        cursor = source.end;
    }
    MdBlock* block = md_block_at(&work->next, text->owner);
    return md_hide_absolute(work, scratch, text->owner, text->base + cursor, text->base + block->length);
}

internal bool md_parse_inlines(MdUpdateWork* work, ScratchArena* scratch, MdBlockId owner) {
    MdBlock* block = md_block_at(&work->next, owner);
    u32 base = md_block_base(&work->next, owner);
    block->annotations = NULL;
    block->annotations_count = 0;
    block->reference_uses = NULL;
    block->reference_uses_count = 0;
    block->flags |= MD_BLOCK_DIRTY;
    MdInlineWork text;
    if (!md_inline_text(work, scratch, owner, block->lines, block->lines_count, base, &text))
        return false;
    for (u32 at = 0; at + 1 < text.text_count; ++at) {
        if (text.text[at] == '\\' && md_ascii_punctuation(text.text[at + 1])) {
            text.claimed[at] = 1;
            text.claimed[++at] = 4;
        }
    }
    u32* code_closers = md_inline_code_closers(&text, scratch);
    if (!code_closers)
        return false;
    MdPiece piece;
    MdView view;
    u32 start;
    MdReader logical = md_inline_reader(&text, &piece, &view, &start);
    for (u32 at = 0; at < text.text_count; ++at) {
        if (text.claimed[at])
            continue;
        if (text.text[at] == '`') {
            u32 after;
            if (!md_inline_code(&text, scratch, code_closers, at, &after))
                return false;
            at = after - 1;
            continue;
        }
        if ((work->next.flags & MD_FLAG_LATEXMATHSPANS) && text.text[at] == '$') {
            u32 width = at + 1 < text.text_count && text.text[at + 1] == '$' ? 2 : 1;
            u32 close = at + width;
            while (close < text.text_count) {
                if (text.text[close] == '\\' && close + 1 < text.text_count) {
                    close += 2;
                    continue;
                }
                if (text.text[close] == '$' && !text.claimed[close] &&
                    (width == 1 || (close + 1 < text.text_count && text.text[close + 1] == '$')))
                    break;
                ++close;
            }
            if (close < text.text_count && close > at + width) {
                if (!md_inline_node(&text, scratch, width == 1 ? MD_NODE_MATH_INLINE : MD_NODE_MATH_DISPLAY,
                                     at, close + width, at + width, close, MD_ID_NONE))
                    return false;
                md_claim(&text, at, at + width, 1);
                md_claim(&text, at + width, close, 3);
                md_claim(&text, close, close + width, 1);
                if (!md_inline_verbatim(&text, scratch, at + width, close, ' '))
                    return false;
                at = close + width - 1;
                continue;
            }
        }
        if (text.text[at] == '<') {
            u32 end;
            MdLinkPrefix prefix;
            if (md_is_autolink(&logical, at, text.text_count, &end, &prefix)) {
                MdLinkWork attr = {.reference = MD_ID_NONE, .destination = {at + 1, end - 1}, .prefix = prefix};
                if (!md_inline_link_node(&text, scratch, MD_NODE_LINK, at, at + 1, end - 1, end, attr))
                    return false;
                md_claim(&text, at + 1, end - 1, 3);
                at = end - 1;
            } else if (!(work->next.flags & MD_FLAG_NOHTMLSPANS) && md_html_inline(&logical, at, text.text_count, &end)) {
                if (!md_inline_node(&text, scratch, MD_NODE_HTML_INLINE, at, end, at, end, MD_ID_NONE))
                    return false;
                md_claim(&text, at, end, 3);
                if (!md_inline_verbatim(&text, scratch, at, end, '\n'))
                    return false;
                at = end - 1;
            }
        }
    }
    return md_inline_brackets(&text, scratch) && md_inline_delimiters(&text, scratch) &&
           md_inline_autolinks(&text, scratch) && md_inline_finish(&text, scratch);
}

internal bool md_parse_verbatim(MdUpdateWork* work, ScratchArena* scratch, MdBlockId owner) {
    MdBlock* block = md_block_at(&work->next, owner);
    u32 base = md_block_base(&work->next, owner), cursor = 0;
    block->annotations = NULL;
    block->annotations_count = 0;
    if (block->kind == MD_BLOCK_BLANK || block->kind == MD_BLOCK_DEFINITION)
        return md_hide_absolute(work, scratch, owner, base, base + block->length);
    for (u32 i = 0; i < block->lines_count; ++i) {
        MdLine line = block->lines[i];
        if (!md_hide_absolute(work, scratch, owner, base + cursor, base + line.content.begin))
            return false;
        MdLine absolute = line;
        absolute.source.begin += base;
        absolute.source.end += base;
        absolute.content.begin += base;
        absolute.content.end += base;
        u32 indentation;
        u32 text_begin = md_skip_line_space(&work->reader, absolute, &indentation) - base;
        if (indentation) {
            MdNode node = {.kind = MD_NODE_REPLACEMENT, .start_idx = line.content.begin, .end_idx = text_begin};
            node.data.replacement.text[0] = ' ';
            node.data.replacement.length = 1;
            node.data.replacement.repeat = indentation;
            if (!md_add_node(work, scratch, owner, node, MD_ID_NONE))
                return false;
        }
        for (u32 at = line.content.begin; at < line.content.end; ++at) {
            if (md_reader_byte(&work->reader, base + at) == 0) {
                MdNode node = {.kind = MD_NODE_REPLACEMENT, .start_idx = at, .end_idx = at + 1};
                memcpy(node.data.replacement.text, "\xef\xbf\xbd", 3);
                node.data.replacement.length = 3;
                node.data.replacement.repeat = 1;
                if (!md_add_node(work, scratch, owner, node, MD_ID_NONE))
                    return false;
            }
        }
        /* MD4C normalizes verbatim line ends and inserts the final newline. */
        if (line.source.end - line.content.end != 1 || md_reader_byte(&work->reader, base + line.content.end) != '\n') {
            MdNode node = {.kind = MD_NODE_REPLACEMENT, .start_idx = line.content.end, .end_idx = line.source.end};
            node.data.replacement.text[0] = '\n';
            node.data.replacement.length = 1;
            node.data.replacement.repeat = 1;
            if (!md_add_node(work, scratch, owner, node, MD_ID_NONE))
                return false;
        }
        cursor = line.source.end;
    }
    return md_hide_absolute(work, scratch, owner, base + cursor, base + block->length);
}

internal bool md_sort_nodes(MdUpdateWork* work, ScratchArena* scratch, void* elements, u32 count, usize size) {
    if (count < 2)
        return true;
    u8* values = elements;
    u8* temporary = md_work_alloc(work, scratch, (usize)count * size);
    if (!temporary)
        return false;
    for (usize width = 1; width < count; width *= 2) {
        for (usize begin = 0; begin < count; begin += width * 2) {
            u32 mid = (u32)min(begin + width, count), end = (u32)min(begin + 2 * width, count);
            u32 left = (u32)begin, right = mid;
            for (u32 out = (u32)begin; out < end; ++out) {
                const MdNode* a = (const MdNode*)(values + (usize)left * size);
                const MdNode* b = right < end ? (const MdNode*)(values + (usize)right * size) : NULL;
                bool before = !b || (left < mid && (a->start_idx < b->start_idx ||
                    (a->start_idx == b->start_idx && (a->end_idx > b->end_idx ||
                     (a->end_idx == b->end_idx && a->kind <= b->kind)))));
                u32 selected = left < mid && before ? left++ : right++;
                memcpy(temporary + (usize)out * size, values + (usize)selected * size, size);
            }
        }
        memcpy(values, temporary, (usize)count * size);
    }
    return true;
}

internal bool md_sort_offsets(MdUpdateWork* work, ScratchArena* scratch, u32* values, u32 count) {
    u32* temporary = md_work_alloc(work, scratch, (usize)count * sizeof(u32));
    if (!temporary)
        return false;
    for (usize width = 1; width < count; width *= 2) {
        for (usize begin = 0; begin < count; begin += width * 2) {
            u32 mid = (u32)min(begin + width, count), end = (u32)min(begin + 2 * width, count);
            u32 left = (u32)begin, right = mid;
            for (u32 out = (u32)begin; out < end; ++out)
                temporary[out] = left < mid && (right == end || values[left] <= values[right]) ? values[left++] : values[right++];
        }
        memcpy(values, temporary, (usize)count * sizeof(u32));
    }
    return true;
}

internal bool md_validate_nodes(MdUpdateWork* work, ScratchArena* scratch,
                                const void* elements, u32 count, usize size, u32 length) {
    u32* ends = md_work_alloc(work, scratch, (usize)count * sizeof(u32));
    if (!ends)
        return false;
    u32 depth = 0, mapping_end = 0;
    for (u32 i = 0; i < count; ++i) {
        const MdNode* node = (const MdNode*)((const u8*)elements + (usize)i * size);
        if (node->end_idx > length || node->start_idx > node->end_idx ||
            (u64)node->content_offset + node->content_length > node->end_idx - node->start_idx)
            goto invalid;
        while (depth && ends[depth - 1] <= node->start_idx)
            --depth;
        if (depth && node->end_idx > ends[depth - 1])
            goto invalid;
        if (node->end_idx > node->start_idx)
            ends[depth++] = node->end_idx;
        if (node->kind == MD_NODE_HIDDEN || node->kind == MD_NODE_REPLACEMENT) {
            if (node->start_idx < mapping_end)
                goto invalid;
            mapping_end = node->end_idx;
            if (node->kind == MD_NODE_REPLACEMENT && (!node->data.replacement.length ||
                node->data.replacement.length > 6 || !node->data.replacement.repeat))
                goto invalid;
        }
    }
    return true;
invalid:
    work->error = MD_ERROR_INVALID_RANGE;
    return false;
}

internal bool md_finish_annotations(MdUpdateWork* work, ScratchArena* scratch, MdBlockId owner) {
    MdBlock* block = md_block_at(&work->next, owner);
    u32 count = block->annotations_count;
    if (!count) {
        block->annotation_max_ends = NULL;
        block->annotation_max_ends_count = 0;
        return true;
    }
    /* Source mappings obey the same laminar interval invariant as syntax.
     * Split hidden ranges at syntax boundaries before sorting; never merge
     * hidden delimiter bytes across an enclosing effect's edge. */
    u32* cuts = NULL;
    u32 cuts_count = 0;
    for (u32 i = 0; i < count; ++i) {
        const MdNode* node = &block->annotations[i].node;
        if (node->kind == MD_NODE_HIDDEN)
            continue;
        u32 begin = node->start_idx, end = node->end_idx;
        if (!md_work_push(work, scratch, (void**)&cuts, &cuts_count, sizeof(u32), &begin) ||
            !md_work_push(work, scratch, (void**)&cuts, &cuts_count, sizeof(u32), &end))
            return false;
    }
    for (u32 i = 0; i < block->children_count; ++i) {
        const MdBlock* child = md_block_at(&work->next, block->children[i]);
        u32 begin = child->offset, end = begin + child->length;
        if (!md_work_push(work, scratch, (void**)&cuts, &cuts_count, sizeof(u32), &begin) ||
            !md_work_push(work, scratch, (void**)&cuts, &cuts_count, sizeof(u32), &end))
            return false;
    }
    if (!md_sort_offsets(work, scratch, cuts, cuts_count))
        return false;
    MdAnnotation* values = NULL;
    u32 used = 0;
    for (u32 i = 0; i < count; ++i) {
        MdAnnotation value = block->annotations[i];
        u32 final_end = value.node.end_idx;
        if (value.node.kind == MD_NODE_HIDDEN) {
            u32 lo = 0, hi = cuts_count;
            while (lo < hi) {
                u32 mid = lo + (hi - lo) / 2;
                if (cuts[mid] <= value.node.start_idx)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            for (u32 j = lo; j < cuts_count && cuts[j] < final_end; ++j) {
                if (cuts[j] == value.node.start_idx)
                    continue;
                value.node.end_idx = cuts[j];
                if (!md_work_push(work, scratch, (void**)&values, &used, sizeof(value), &value))
                    return false;
                value.node.start_idx = cuts[j];
            }
            value.node.end_idx = final_end;
        }
        if (!md_work_push(work, scratch, (void**)&values, &used, sizeof(value), &value))
            return false;
    }
    block->annotations = values;
    block->annotations_count = used;
    if (!md_sort_nodes(work, scratch, values, used, sizeof(*values)) ||
        !md_validate_nodes(work, scratch, values, used, sizeof(*values), block->length))
        return false;
    block->annotation_max_ends = md_work_clone(work, scratch, NULL, used, sizeof(u32));
    if (!block->annotation_max_ends)
        return false;
    block->annotation_max_ends_count = used;
    u32 maximum = 0;
    for (u32 i = 0; i < used; ++i) {
        maximum = max(maximum, block->annotations[i].node.end_idx);
        block->annotation_max_ends[i] = maximum;
    }
    return true;
}

// =====================================================================================================================
// Incremental ownership and dependency reconciliation.
// =====================================================================================================================

internal bool md_rebuild_lines(MdUpdateWork* work, ScratchArena* scratch, MdLine** result, u32* result_count) {
    *result = NULL;
    *result_count = 0;
    const MdBlock* old_root = md_block_at(work->state, work->state->root);
    u32 old_count = old_root ? old_root->lines_count : 0;
    u32 first = 0;
    while (first < old_count && old_root->lines[first].source.end <= work->edit.offset)
        ++first;
    if (first)
        --first; /* A CRLF or removed line boundary can reach backwards. */
    for (u32 i = 0; i < first; ++i)
        if (!md_work_push(work, scratch, (void**)result, result_count, sizeof(MdLine), &old_root->lines[i]))
            return false;
    u32 at = first < old_count ? old_root->lines[first].source.begin : 0;
    i64 delta = (i64)work->edit.inserted - work->edit.removed;
    u32 old_end = work->edit.offset + work->edit.removed;
    u32 new_end = work->edit.offset + work->edit.inserted;
    while (at < work->reader.length) {
        if (at >= new_end && old_root) {
            i64 mapped = (i64)at - delta;
            u32 lo = first, hi = old_count;
            while (lo < hi) {
                u32 mid = lo + (hi - lo) / 2;
                if ((i64)old_root->lines[mid].source.begin < mapped)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (mapped >= old_end && lo < old_count && old_root->lines[lo].source.begin == mapped) {
                for (u32 i = lo; i < old_count; ++i) {
                    MdLine line = old_root->lines[i];
                    line.source.begin = (u32)((i64)line.source.begin + delta);
                    line.source.end = (u32)((i64)line.source.end + delta);
                    line.content.begin = (u32)((i64)line.content.begin + delta);
                    line.content.end = (u32)((i64)line.content.end + delta);
                    if (!md_work_push(work, scratch, (void**)result, result_count, sizeof(line), &line))
                        return false;
                }
                return true;
            }
        }
        MdPhysicalLine physical;
        if (!md_reader_line(&work->reader, at, &physical))
            return false;
        MdLine line = {.source = {physical.begin, physical.end}, .content = {physical.begin, physical.content_end}};
        if (!md_work_push(work, scratch, (void**)result, result_count, sizeof(line), &line))
            return false;
        at = physical.end;
    }
    return true;
}

internal bool md_reparse_root(MdUpdateWork* work, ScratchArena* scratch) {
    MdLine* lines;
    u32 lines_count;
    if (!md_rebuild_lines(work, scratch, &lines, &lines_count))
        return false;
    MdBlock* old_root = md_block_at(work->state, work->state->root);
    if (work->next.root.index == UINT32_MAX) {
        work->next.root = md_new_block(work, scratch, MD_ID_NONE, MD_BLOCK_ROOT, 0, work->reader.length);
        if (work->next.root.index == UINT32_MAX)
            return false;
    }
    MdBlock* root = md_block_at(&work->next, work->next.root);
    root->length = work->reader.length;
    root->lines = lines;
    root->lines_count = lines_count;
    root->children = NULL;
    root->children_count = 0;
    root->annotations = NULL;
    root->annotations_count = 0;
    root->flags |= MD_BLOCK_DIRTY;
    u32 old_count = old_root ? old_root->children_count : 0;
    u32 first = 0;
    while (first < old_count) {
        MdBlock* block = md_block_at(work->state, old_root->children[first]);
        if (block->offset + block->length > work->edit.offset)
            break;
        ++first;
    }
    if (first)
        --first; /* Reconsider the preceding block's interruption/merge decision. */
    u32 begin = first < old_count ? md_block_at(work->state, old_root->children[first])->offset : 0;
    for (u32 i = 0; i < first; ++i)
        if (!md_add_child(work, scratch, work->next.root, old_root->children[i]))
            return false;
    u32 line = 0;
    while (line < lines_count && lines[line].source.begin < begin)
        ++line;
    u32 suffix = old_count;
    i64 delta = (i64)work->edit.inserted - work->edit.removed;
    while (line < lines_count) {
        u32 consumed;
        MdBlockId child = md_parse_one(work, scratch, work->next.root, lines + line, lines_count - line, &consumed);
        if (child.index == UINT32_MAX || !consumed || !md_add_child(work, scratch, work->next.root, child))
            return false;
        line += consumed;
        u32 next = line < lines_count ? lines[line].source.begin : work->reader.length;
        if (next < work->edit.offset + work->edit.inserted)
            continue;
        i64 mapped = (i64)next - delta;
        if (mapped < (i64)work->edit.offset + work->edit.removed)
            continue;
        u32 lo = first, hi = old_count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            if ((i64)md_block_at(work->state, old_root->children[mid])->offset < mapped)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < old_count && md_block_at(work->state, old_root->children[lo])->offset == mapped) {
            suffix = lo;
            break;
        }
    }
    for (u32 i = first; i < suffix; ++i)
        md_remove_block(work, old_root->children[i]);
    for (u32 i = suffix; i < old_count; ++i) {
        MdBlockId id = old_root->children[i];
        MdBlock* block = md_block_at(&work->next, id);
        block->offset = (u32)((i64)block->offset + delta);
        if (!md_add_child(work, scratch, work->next.root, id))
            return false;
    }
    return !work->error;
}

internal bool md_reconcile_definitions(MdUpdateWork* work, ScratchArena* scratch) {
    for (u32 i = 0; i < work->next.labels_count; ++i) {
        MdLabel* label = &work->next.labels[i];
        label->winner = MD_ID_NONE;
        label->definitions = NULL;
        label->definitions_count = 0;
        label->users = NULL;
        label->users_count = 0;
    }
    for (u32 i = 0; i < work->next.definitions_count; ++i) {
        MdDefinition* def = &work->next.definitions[i];
        if (!def->alive)
            continue;
        def->display_number = def->reference_count = 0;
        MdLabel* label = &work->next.labels[def->label.index];
        MdDefinitionId id = {i, def->generation};
        if (!md_work_push(work, scratch, (void**)&label->definitions, &label->definitions_count, sizeof(id), &id))
            return false;
        if (label->winner.index == UINT32_MAX || md_block_base(&work->next, def->owner) <
            md_block_base(&work->next, work->next.definitions[label->winner.index].owner))
            label->winner = id;
    }
    for (u32 i = 0; i < work->next.blocks_count; ++i) {
        MdBlock* block = &work->next.blocks[i];
        if (!block->alive || (block->flags & MD_BLOCK_DIRTY))
            continue;
        for (u32 j = 0; j < block->reference_uses_count; ++j) {
            MdLabelId id = block->reference_uses[j];
            MdDefinitionId old = id.index < work->state->labels_count ? work->state->labels[id.index].winner : MD_ID_NONE;
            if (!md_id_equal(old, work->next.labels[id.index].winner)) {
                block->flags |= MD_BLOCK_DIRTY;
                break;
            }
        }
    }
    return true;
}

internal bool md_finish_leaves(MdUpdateWork* work, ScratchArena* scratch) {
    for (u32 i = 0; i < work->next.blocks_count; ++i) {
        MdBlock* block = &work->next.blocks[i];
        if (!block->alive || !(block->flags & MD_BLOCK_DIRTY))
            continue;
        MdBlockId id = {i, block->generation};
        if (block->kind == MD_BLOCK_PARAGRAPH || block->kind == MD_BLOCK_HEADING || block->kind == MD_BLOCK_TABLE_CELL) {
            if (!md_parse_inlines(work, scratch, id))
                return false;
        } else if (block->kind == MD_BLOCK_CODE || block->kind == MD_BLOCK_HTML ||
                   block->kind == MD_BLOCK_BLANK || block->kind == MD_BLOCK_DEFINITION) {
            if (!md_parse_verbatim(work, scratch, id))
                return false;
        }
        if (!md_finish_annotations(work, scratch, id))
            return false;
    }
    for (u32 i = 0; i < work->next.blocks_count; ++i) {
        MdBlock* block = &work->next.blocks[i];
        if (!block->alive)
            continue;
        for (u32 j = 0; j < block->reference_uses_count; ++j) {
            MdLabel* label = &work->next.labels[block->reference_uses[j].index];
            MdReferenceUse use = {.owner = {i, block->generation}, .kind = label->kind};
            if (!md_work_push(work, scratch, (void**)&label->users, &label->users_count, sizeof(use), &use))
                return false;
        }
    }
    return !work->error;
}

internal bool md_visit_footnotes(MdUpdateWork* work, ScratchArena* scratch, MdBlockId id, bool definition_root) {
    MdBlock* block = md_block_at(&work->next, id);
    if (block->kind == MD_BLOCK_FOOTNOTE && !definition_root)
        return true;
    for (u32 i = 0; i < block->annotations_count; ++i) {
        MdAnnotation* annotation = &block->annotations[i];
        if (annotation->node.kind != MD_NODE_FOOTNOTE_REFERENCE)
            continue;
        MdDefinitionId winner = work->next.labels[annotation->reference.index].winner;
        if (winner.index == UINT32_MAX)
            continue;
        MdDefinition* def = &work->next.definitions[winner.index];
        if (!def->display_number) {
            def->display_number = work->next.footnote_order_count + 1;
            if (!md_work_push(work, scratch, (void**)&work->next.footnote_order, &work->next.footnote_order_count,
                              sizeof(winner), &winner))
                return false;
        }
        MdFootnoteOccurrence use = {.definition = winner, .source = {id, annotation->node.start_idx},
                                    .occurrence = ++def->reference_count};
        if (!md_work_push(work, scratch, (void**)&work->next.footnote_occurrences, &work->next.footnote_occurrences_count,
                          sizeof(use), &use))
            return false;
    }
    for (u32 i = 0; i < block->children_count; ++i)
        if (!md_visit_footnotes(work, scratch, block->children[i], false))
            return false;
    return true;
}

internal bool md_resolve_footnotes(MdUpdateWork* work, ScratchArena* scratch) {
    work->next.footnote_order = NULL;
    work->next.footnote_order_count = 0;
    work->next.footnote_occurrences = NULL;
    work->next.footnote_occurrences_count = 0;
    if (!(work->next.flags & MD_FLAG_FOOTNOTES))
        return true;
    if (!md_visit_footnotes(work, scratch, work->next.root, false))
        return false;
    /* Appending newly referenced bodies extends this queue; already numbered
     * definitions are never queued twice, including self/cyclic references. */
    for (u32 i = 0; i < work->next.footnote_order_count; ++i) {
        MdDefinition* def = &work->next.definitions[work->next.footnote_order[i].index];
        if (!md_visit_footnotes(work, scratch, def->owner, true))
            return false;
    }
    if (work->next.footnote_order_count) {
        MdNode section = {.kind = MD_NODE_FOOTNOTE_SECTION, .start_idx = 0, .end_idx = work->reader.length};
        section.data.footnote.count = work->next.footnote_order_count;
        if (!md_add_node(work, scratch, work->next.root, section, MD_ID_NONE) ||
            !md_finish_annotations(work, scratch, work->next.root))
            return false;
    }
    return true;
}

internal void* md_durable_alloc(MdUpdateWork* work, usize bytes) {
    if (!bytes)
        return NULL;
    MdAllocation** link = &work->state->free_allocations;
    while (*link && (*link)->bytes < bytes)
        link = &(*link)->next;
    MdAllocation* allocation = *link;
    if (allocation) {
        *link = allocation->next;
    } else {
        usize units = bytes / sizeof(MdAllocation) + (bytes % sizeof(MdAllocation) != 0) + 1;
        Arena* arena = work->state->arena;
        usize padding = (alignof(MdAllocation) - (arena->len & (alignof(MdAllocation) - 1))) & (alignof(MdAllocation) - 1);
        if (units >= PAGE_SIZE / sizeof(MdAllocation) || arena->len > ARENA_VCAP ||
            padding > ARENA_VCAP - arena->len || units * sizeof(MdAllocation) > ARENA_VCAP - arena->len - padding) {
            work->error = MD_ERROR_OUT_OF_MEMORY;
            return NULL;
        }
        allocation = arena_alloc_aligned(arena, MdAllocation, units);
        if (!allocation) {
            work->error = MD_ERROR_OUT_OF_MEMORY;
            return NULL;
        }
        allocation->bytes = (units - 1) * sizeof(MdAllocation);
        allocation->alignment = alignof(MdAllocation);
    }
    allocation->next = work->prepared_allocations;
    work->prepared_allocations = allocation;
    return allocation + 1;
}

internal bool md_prepare_array(MdUpdateWork* work, void** pointer, const void* old, u32 count, usize size) {
    if (!count) {
        *pointer = NULL;
        return true;
    }
    if (*pointer == old)
        return true;
    if (size && (usize)count > SIZE_MAX / size) {
        work->error = MD_ERROR_OVERFLOW;
        return false;
    }
    void* result = md_durable_alloc(work, (usize)count * size);
    if (!result)
        return false;
    memcpy(result, *pointer, (usize)count * size);
    *pointer = result;
    return true;
}

internal void md_retire(MdState* state, void* pointer, const void* retained) {
    if (!pointer || pointer == retained)
        return;
    MdAllocation* allocation = (MdAllocation*)pointer - 1;
    allocation->next = state->free_allocations;
    state->free_allocations = allocation;
}

internal bool md_finish_content(MdUpdateWork* work, MdBlockId id) {
    MdBlock* block = md_block_at(&work->next, id);
    MdRange content = {0};
    if (block->kind == MD_BLOCK_CODE) {
        content = block->data.code.content;
    } else if (block->kind != MD_BLOCK_BLANK && block->kind != MD_BLOCK_DEFINITION && block->kind != MD_BLOCK_THEMATIC_BREAK &&
               block->lines_count && !block->children_count) {
        content.begin = block->lines[0].content.begin;
        content.end = block->lines[block->lines_count - 1].content.end;
    }
    bool have_content = content.begin != content.end;
    u32 preceding_end = 0;
    for (u32 i = 0; i < block->children_count; ++i) {
        MdBlockId child_id = block->children[i];
        MdBlock* child = md_block_at(&work->next, child_id);
        if (!child || !md_id_equal(child->parent, id) || child->offset < preceding_end ||
            (u64)child->offset + child->length > block->length) {
            work->error = MD_ERROR_INVALID_RANGE;
            return false;
        }
        preceding_end = child->offset + child->length;
        if (!md_finish_content(work, child_id))
            return false;
        if (child->content.end != child->content.begin) {
            if (!have_content)
                content.begin = child->offset + child->content.begin;
            content.end = child->offset + child->content.end;
            have_content = true;
        }
    }
    if (block->kind == MD_BLOCK_ROOT)
        content = (MdRange){0, block->length};
    if (content.begin > UINT16_MAX && block->kind != MD_BLOCK_ROOT) {
        work->error = MD_ERROR_OVERFLOW;
        return false;
    }
    if (content.end < content.begin || content.end > block->length) {
        work->error = MD_ERROR_INVALID_RANGE;
        return false;
    }
    block->content = content;
    return true;
}

internal bool md_prepare_commit(MdUpdateWork* work) {
    for (u32 i = 0; i < work->next.blocks_count; ++i) {
        MdBlock* block = &work->next.blocks[i];
        const MdBlock empty = {0};
        const MdBlock* old = i < work->state->blocks_count ? &work->state->blocks[i] : &empty;
        if (!block->alive) {
            block->children_count = block->lines_count = block->annotations_count = 0;
            block->reference_uses_count = block->annotation_max_ends_count = 0;
        }
        if (!md_prepare_array(work, (void**)&block->children, old->children, block->children_count, sizeof(MdBlockId)) ||
            !md_prepare_array(work, (void**)&block->lines, old->lines, block->lines_count, sizeof(MdLine)) ||
            !md_prepare_array(work, (void**)&block->annotations, old->annotations, block->annotations_count, sizeof(MdAnnotation)) ||
            !md_prepare_array(work, (void**)&block->reference_uses, old->reference_uses, block->reference_uses_count, sizeof(MdLabelId)) ||
            !md_prepare_array(work, (void**)&block->annotation_max_ends, old->annotation_max_ends,
                               block->annotation_max_ends_count, sizeof(u32)))
            return false;
        block->flags &= ~MD_BLOCK_DIRTY;
    }
    for (u32 i = 0; i < work->next.labels_count; ++i) {
        MdLabel* label = &work->next.labels[i];
        const MdLabel empty = {0};
        const MdLabel* old = i < work->state->labels_count ? &work->state->labels[i] : &empty;
        if (!label->definitions_count && !label->users_count) {
            label->alive = false;
            label->key_count = 0;
        }
        if (!md_prepare_array(work, (void**)&label->key, old->key, label->key_count, sizeof(u8)) ||
            !md_prepare_array(work, (void**)&label->definitions, old->definitions, label->definitions_count, sizeof(MdDefinitionId)) ||
            !md_prepare_array(work, (void**)&label->users, old->users, label->users_count, sizeof(MdReferenceUse)))
            return false;
    }
    return md_prepare_array(work, (void**)&work->next.blocks, work->state->blocks, work->next.blocks_count, sizeof(MdBlock)) &&
           md_prepare_array(work, (void**)&work->next.definitions, work->state->definitions, work->next.definitions_count, sizeof(MdDefinition)) &&
           md_prepare_array(work, (void**)&work->next.labels, work->state->labels, work->next.labels_count, sizeof(MdLabel)) &&
           md_prepare_array(work, (void**)&work->next.footnote_order, work->state->footnote_order,
                              work->next.footnote_order_count, sizeof(MdDefinitionId)) &&
           md_prepare_array(work, (void**)&work->next.footnote_occurrences, work->state->footnote_occurrences,
                              work->next.footnote_occurrences_count, sizeof(MdFootnoteOccurrence));
}

internal void md_commit(MdUpdateWork* work) {
    MdState* state = work->state;
    for (u32 i = 0; i < state->blocks_count; ++i) {
        MdBlock* old = &state->blocks[i];
        MdBlock* block = &work->next.blocks[i];
        md_retire(state, old->children, block->children);
        md_retire(state, old->lines, block->lines);
        md_retire(state, old->annotations, block->annotations);
        md_retire(state, old->reference_uses, block->reference_uses);
        md_retire(state, old->annotation_max_ends, block->annotation_max_ends);
    }
    for (u32 i = 0; i < state->labels_count; ++i) {
        MdLabel* old = &state->labels[i];
        MdLabel* label = &work->next.labels[i];
        md_retire(state, old->key, label->key);
        md_retire(state, old->definitions, label->definitions);
        md_retire(state, old->users, label->users);
    }
    md_retire(state, state->blocks, work->next.blocks);
    md_retire(state, state->definitions, work->next.definitions);
    md_retire(state, state->labels, work->next.labels);
    md_retire(state, state->footnote_order, work->next.footnote_order);
    md_retire(state, state->footnote_occurrences, work->next.footnote_occurrences);
    work->next.free_allocations = state->free_allocations;
    work->next.error = MD_ERROR_NONE;
    work->next.revision = state->revision + 1;
    *state = work->next;
}

internal void md_abort(MdUpdateWork* work) {
    MdAllocation* allocation = work->prepared_allocations;
    while (allocation) {
        MdAllocation* next = allocation->next;
        allocation->next = work->state->free_allocations;
        work->state->free_allocations = allocation;
        allocation = next;
    }
    work->state->error = work->error ? work->error : MD_ERROR_INVALID_RANGE;
}

internal bool md_apply(MdState* state, ScratchArena* scratch, const MdView* source, MdEdit edit) {
    MdUpdateWork work = {.state = state, .next = *state, .edit = edit};
    if (!state->arena || !scratch || !scratch->data || !md_reader_init(&work.reader, source, scratch)) {
        state->error = MD_ERROR_INVALID_VIEW;
        return false;
    }
    if (edit.offset > state->source_length || edit.removed > state->source_length - edit.offset ||
        (u64)state->source_length - edit.removed + edit.inserted != work.reader.length) {
        state->error = MD_ERROR_INVALID_EDIT;
        return false;
    }
    work.next.source_length = work.reader.length;
    work.next.blocks = md_work_clone(&work, scratch, state->blocks, state->blocks_count, sizeof(MdBlock));
    work.next.definitions = md_work_clone(&work, scratch, state->definitions, state->definitions_count, sizeof(MdDefinition));
    work.next.labels = md_work_clone(&work, scratch, state->labels, state->labels_count, sizeof(MdLabel));
    if (work.error || !md_reparse_root(&work, scratch) || !md_reconcile_definitions(&work, scratch) ||
        !md_finish_leaves(&work, scratch) || !md_resolve_footnotes(&work, scratch) ||
        !md_finish_content(&work, work.next.root) || !md_prepare_commit(&work)) {
        md_abort(&work);
        return false;
    }
    md_commit(&work);
    return true;
}

MdState md_create(const MdView* source, Arena* arena, ScratchArena* scratch, u32 flags) {
    MdState state = {.arena = arena, .flags = flags, .root = MD_ID_NONE,
                     .first_free_block = UINT32_MAX, .first_free_definition = UINT32_MAX};
    if (flags & ~0xffff7fu) {
        state.error = MD_ERROR_INVALID_OPTIONS;
        return state;
    }
    u64 length = 0;
    if (!source || (source->count && !source->pieces)) {
        state.error = MD_ERROR_INVALID_VIEW;
        return state;
    }
    for (u32 i = 0; i < source->count; ++i) {
        length += source->pieces[i].length;
        if (length > UINT32_MAX) {
            state.error = MD_ERROR_OVERFLOW;
            return state;
        }
    }
    md_apply(&state, scratch, source, (MdEdit){.inserted = (u32)length});
    return state;
}

void md_update(MdState* state, ScratchArena* scratch, const MdView* source,
                u32 length, u32 offset, u32 removed_length) {
    if (state)
        md_apply(state, scratch, source, (MdEdit){offset, removed_length, length});
}

void md_destroy(MdState* state) {
    if (state)
        *state = (MdState){0};
}

// =====================================================================================================================
// Range queries. Absolute coordinates are materialized only here.
// =====================================================================================================================

internal bool md_overlaps(u32 begin, u32 end, u32 query_begin, u32 query_end, u32 document_end) {
    if (begin == end)
        return begin >= query_begin && (begin < query_end || (begin == query_end && query_end == document_end));
    return begin < query_end && end > query_begin;
}

internal bool md_query_append(MdState* state, MdNode* nodes, u32* count, MdNode node,
                              u32 begin, u32 end) {
    if (!md_overlaps(node.start_idx, node.end_idx, begin, end, state->source_length))
        return true;
    if (*count == UINT32_MAX) {
        state->error = MD_ERROR_OVERFLOW;
        return false;
    }
    if (nodes)
        nodes[*count] = node;
    ++*count;
    return true;
}

internal bool md_query_annotation(MdState* state, MdBlockId owner, const MdAnnotation* annotation,
                                  u32 base, MdNode* out) {
    *out = annotation->node;
    out->start_idx += base;
    out->end_idx += base;
    if (out->kind == MD_NODE_LINK || out->kind == MD_NODE_IMAGE || out->kind == MD_NODE_WIKI_LINK) {
        if (annotation->reference.index == UINT32_MAX) {
            out->data.link.dest_start += base;
            out->data.link.dest_end += base;
        } else {
            MdDefinitionId winner = state->labels[annotation->reference.index].winner;
            if (winner.index == UINT32_MAX)
                return false;
            MdDefinition* def = &state->definitions[winner.index];
            u32 def_base = md_block_base(state, def->owner);
            out->data.link.dest_start = def_base + def->destination.begin;
            out->data.link.dest_end = def_base + def->destination.end;
            out->data.link.has_title = def->has_title;
        }
    } else if (out->kind == MD_NODE_FOOTNOTE_REFERENCE) {
        MdDefinitionId winner = state->labels[annotation->reference.index].winner;
        if (winner.index == UINT32_MAX)
            return false;
        MdDefinition* def = &state->definitions[winner.index];
        out->data.footnote.number = def->display_number;
        out->data.footnote.count = def->reference_count;
        for (u32 i = 0; i < state->footnote_occurrences_count; ++i) {
            MdFootnoteOccurrence use = state->footnote_occurrences[i];
            if (md_id_equal(use.source.owner, owner) && use.source.offset == annotation->node.start_idx) {
                out->data.footnote.occurrence = use.occurrence;
                break;
            }
        }
    }
    return true;
}

internal bool md_query_tree(MdState* state, MdBlockId id, u32 base, u32 quote_depth, u32 list_depth,
                            bool ordered, MdNode* nodes, u32* count, u32 begin, u32 end) {
    const MdBlock* block = md_block_at(state, id);
    if (!block)
        return true;
    base += block->offset;
    if (block->kind != MD_BLOCK_ROOT && !md_overlaps(base, base + block->length, begin, end, state->source_length))
        return true;
    MdNode node = {.start_idx = base, .end_idx = base + block->length};
    bool structural = true;
    switch (block->kind) {
        case MD_BLOCK_ROOT: case MD_BLOCK_TRIVIA: case MD_BLOCK_DEFINITION:
            structural = false; break;
        case MD_BLOCK_PARAGRAPH: node.kind = MD_NODE_PARAGRAPH; break;
        case MD_BLOCK_HEADING: node.kind = MD_NODE_HEADING; node.data.heading_level = block->data.heading.level; break;
        case MD_BLOCK_QUOTE: node.kind = MD_NODE_QUOTE; node.data.quote_depth = ++quote_depth; break;
        case MD_BLOCK_ADMONITION: node.kind = MD_NODE_ADMONITION; node.data.admonition = block->data.admonition.kind; break;
        case MD_BLOCK_LIST:
            node.kind = MD_NODE_LIST;
            node.data.list.depth = ++list_depth;
            node.data.list.start = block->data.list.start;
            node.data.list.tight = !block->data.list.loose_children;
            ordered = block->data.list.ordered != 0;
            node.data.list.ordered = ordered;
            break;
        case MD_BLOCK_ITEM:
            if (list_depth > UINT16_MAX || block->data.item.number > UINT16_MAX) {
                state->error = MD_ERROR_OVERFLOW;
                return false;
            }
            node.kind = MD_NODE_LIST_ITEM;
            node.data.item.depth = (u16)list_depth;
            node.data.item.number = (u16)block->data.item.number;
            node.data.item.ordered = ordered;
            node.data.item.task = block->data.item.task;
            node.data.item.checked = block->data.item.checked;
            break;
        case MD_BLOCK_CODE:
            node.kind = MD_NODE_CODE_BLOCK;
            node.data.code.fenced = block->data.code.fenced;
            break;
        case MD_BLOCK_HTML: node.kind = MD_NODE_HTML_BLOCK; break;
        case MD_BLOCK_THEMATIC_BREAK: node.kind = MD_NODE_THEMATIC_BREAK; break;
        case MD_BLOCK_BLANK:
            node.kind = MD_NODE_BLANK;
            node.data.line_count = block->data.blank.line_count;
            structural = (state->flags & MD_FLAG_PRESERVEBLANKLINES) != 0;
            break;
        case MD_BLOCK_TABLE: node.kind = MD_NODE_TABLE; node.data.table.columns = block->data.table.columns; break;
        case MD_BLOCK_TABLE_HEAD: node.kind = MD_NODE_TABLE_HEAD; break;
        case MD_BLOCK_TABLE_BODY: node.kind = MD_NODE_TABLE_BODY; break;
        case MD_BLOCK_TABLE_ROW: node.kind = MD_NODE_TABLE_ROW; break;
        case MD_BLOCK_TABLE_CELL:
            node.kind = MD_NODE_TABLE_CELL;
            node.data.cell.row = block->data.cell.row;
            node.data.cell.column = block->data.cell.column;
            node.data.cell.alignment = block->data.cell.alignment;
            node.data.cell.header = block->data.cell.header;
            break;
        case MD_BLOCK_FOOTNOTE: {
            MdDefinitionId definition = block->data.definition.definition;
            MdDefinition* def = &state->definitions[definition.index];
            if (!def->display_number) {
                node.kind = MD_NODE_HIDDEN;
                return md_query_append(state, nodes, count, node, begin, end);
            }
            node.kind = MD_NODE_FOOTNOTE_DEFINITION;
            node.data.footnote.number = def->display_number;
            node.data.footnote.count = def->reference_count;
            break;
        }
    }
    if (structural) {
        if (!md_node_content(&node, (MdRange){base + block->content.begin, base + block->content.end})) {
            state->error = MD_ERROR_OVERFLOW;
            return false;
        }
        if (!md_query_append(state, nodes, count, node, begin, end))
            return false;
    }
    u32 local_begin = begin > base ? begin - base : 0;
    u32 lo = 0, hi = block->annotations_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (block->annotation_max_ends[mid] < local_begin)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (u32 i = lo; i < block->annotations_count; ++i) {
        const MdAnnotation* annotation = &block->annotations[i];
        if ((u64)base + annotation->node.start_idx > end)
            break;
        MdNode result;
        if (!md_query_annotation(state, id, annotation, base, &result)) {
            if (state->error)
                return false;
            continue;
        }
        if (!md_query_append(state, nodes, count, result, begin, end))
            return false;
    }
    lo = 0;
    hi = block->children_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        const MdBlock* child = md_block_at(state, block->children[mid]);
        if ((u64)base + child->offset + child->length < begin)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (u32 i = lo; i < block->children_count; ++i) {
        const MdBlock* child = md_block_at(state, block->children[i]);
        if ((u64)base + child->offset > end)
            break;
        if (!md_query_tree(state, block->children[i], base, quote_depth, list_depth, ordered, nodes, count, begin, end))
            return false;
    }
    return true;
}

MdResult md_get(MdState* state, Arena* arena, ScratchArena* scratch, u32 begin, u32 end) {
    MdResult empty = {0};
    if (!state)
        return empty;
    if (!arena || !scratch || !scratch->data || begin > end || end > state->source_length) {
        state->error = MD_ERROR_INVALID_RANGE;
        return empty;
    }
    state->error = MD_ERROR_NONE;
    if (begin == end)
        return empty;
    u32 count = 0;
    if (!md_query_tree(state, state->root, 0, 0, 0, false, NULL, &count, begin, end) || !count)
        return empty;
    usize bytes = (usize)count * sizeof(MdNode);
    usize padding = (alignof(MdNode) - (arena->len & (alignof(MdNode) - 1))) & (alignof(MdNode) - 1);
    if (!arena->data || bytes >= PAGE_SIZE || arena->len > ARENA_VCAP || padding > ARENA_VCAP - arena->len ||
        bytes > ARENA_VCAP - arena->len - padding) {
        state->error = MD_ERROR_OUT_OF_MEMORY;
        return empty;
    }
    MdUpdateWork work = {.state = state};
    MdNode* temporary = md_work_alloc(&work, scratch, bytes);
    if (!temporary) {
        state->error = work.error;
        return empty;
    }
    u32 written = 0;
    if (!md_query_tree(state, state->root, 0, 0, 0, false, temporary, &written, begin, end))
        return empty;
    if (!md_sort_nodes(&work, scratch, temporary, written, sizeof(MdNode)) ||
        !md_validate_nodes(&work, scratch, temporary, written, sizeof(MdNode), state->source_length)) {
        state->error = work.error;
        return empty;
    }
    MdNode* nodes = arena_alloc_aligned(arena, MdNode, count);
    if (!nodes) {
        state->error = MD_ERROR_OUT_OF_MEMORY;
        return empty;
    }
    memcpy(nodes, temporary, bytes);
    return (MdResult){.nodes = nodes, .count = written};
}

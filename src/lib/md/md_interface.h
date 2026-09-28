#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/mem/arena.h>

typedef struct MdState MdState;

typedef struct MdPiece {
    u8* bytes;
    u32 length;
} MdPiece;

typedef struct MdView {
    MdPiece* pieces;
    u32      count;
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

/* Flat, read-only annotations for the requested range, including enclosing
 * effects. Node ranges retain their full extents rather than being clipped.
 * Only active interpretations are returned; dormant syntax is not exposed.
 * Overlapping inline effects combine. Quote/list depths are already resolved
 * (use the deepest applicable depth, do not add depths together).
 * Hidden and replacement mappings do not overlap each other; replacement
 * output inherits the active inline effects at its source range.
 * Annotations are not an enter/leave stream and require no parent traversal. */
typedef struct MdResult {
    rop(ro MdNode) nodes;
    u32 count;
} MdResult;

/* arena owns durable state allocations and must outlive the state.
 * ScratchArena parameters are caller-supplied temporary storage, borrowed only
 * for the call. No scratch allocator or scratch-backed pointer is retained. */
/* flags selects the MD_FLAG_* options declared below. */
MdState md_create(rop(ro MdView) source, rop(rw Arena) arena, rop(rw ScratchArena) scratch, u32 flags);

/* source exposes the complete post-edit document.
 * Replace the previous document's [offset, offset + removed_length) with
 * length inserted bytes, available in source at [offset, offset + length).
 * Bytes outside the replacement must be unchanged.
 * Insert: removed_length == 0. Delete: length == 0. */
void md_update(rop(rw MdState) state,
               rop(rw ScratchArena) scratch,
               rop(ro MdView) source,
               u32 length,
               u32 offset,
               u32 removed_length);

/* Query [begin, end). Result nodes are allocated in arena and remain valid
 * until that storage is released. Later parser calls do not modify them;
 * their offsets describe the document at the time of this query.
 * All source-dependent parsing completes during md_create()/md_update(). */
MdResult md_get(rop(rw MdState) state, rop(rw Arena) arena, rop(rw ScratchArena) scratch, u32 begin, u32 end);

/* Clear the state without resetting or destroying the caller's Arena. */
void md_destroy(rop(rw MdState) state);

/* Options retain the owned MD4C fork's bit values. Options are fixed at create.
 * Underline changes '_' interpretation; disabling HTML/indented code changes
 * the grammar. Supporting all options does not mean enabling all flags. */
#define MD_FLAG_COLLAPSEWHITESPACE       0x1u
#define MD_FLAG_PERMISSIVEATXHEADERS     0x2u
#define MD_FLAG_PERMISSIVEURLAUTOLINKS   0x4u
#define MD_FLAG_PERMISSIVEEMAILAUTOLINKS 0x8u
#define MD_FLAG_NOINDENTEDCODEBLOCKS     0x10u
#define MD_FLAG_NOHTMLBLOCKS             0x20u
#define MD_FLAG_NOHTMLSPANS              0x40u
#define MD_FLAG_TABLES                   0x100u
#define MD_FLAG_STRIKETHROUGH            0x200u
#define MD_FLAG_PERMISSIVEWWWAUTOLINKS   0x400u
#define MD_FLAG_TASKLISTS                0x800u
#define MD_FLAG_LATEXMATHSPANS           0x1000u
#define MD_FLAG_WIKILINKS                0x2000u
#define MD_FLAG_UNDERLINE                0x4000u
#define MD_FLAG_HARD_SOFT_BREAKS         0x8000u
#define MD_FLAG_SPOILERS                 0x10000u
#define MD_FLAG_SUPERSCRIPTS             0x20000u
#define MD_FLAG_SUBSCRIPTS               0x40000u
#define MD_FLAG_ADMONITIONS              0x80000u
#define MD_FLAG_FOOTNOTES                0x100000u
#define MD_FLAG_HIGHLIGHT                0x200000u
#define MD_FLAG_PRESERVEBLANKLINES       0x400000u
#define MD_FLAG_INSERT                   0x800000u
#define MD_FLAG_PERMISSIVEAUTOLINKS                                                                                    \
    (MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_PERMISSIVEEMAILAUTOLINKS | MD_FLAG_PERMISSIVEWWWAUTOLINKS)
#define MD_FLAG_NOHTML        (MD_FLAG_NOHTMLBLOCKS | MD_FLAG_NOHTMLSPANS)
#define MD_DIALECT_COMMONMARK 0u
#define MD_DIALECT_GITHUB                                                                                              \
    (MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_ADMONITIONS |  \
     MD_FLAG_FOOTNOTES)
#define MD_DEFAULT_FLAGS (MD_DIALECT_COMMONMARK | MD_FLAG_PRESERVEBLANKLINES)

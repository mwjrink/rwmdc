# Markdown parser: concrete internal design

This is the design declaration file, not a placeholder parser implementation.
The approved narrow edit to `md_interface.h` adds caller-supplied scratch and
clarifies arena/result ownership. Extension schema, options, error handling and
boundary conventions below remain proposed; they are not current declarations.

## Decisions

- `MdState` contains the durable state directly. No `MdStorage`, pimpl, or
  separately allocated state wrapper.
- All arrays are explicit `T *elements; u32 elements_count;` members. No vector
  types, collection macros, or capacity fields in syntax structs.
- The caller supplies `Arena` for durable allocations and `ScratchArena` for
  temporary work. No struct contains a `ScratchArena`, a pointer to one, or a
  stored scratch allocator. Functions pass scratch explicitly to callees.
- Source pieces and bytes are borrowed only during create/update. There is no
  durable document copy and no source pointer in `MdState`.
- Keep all the extension grammar supported by this MD4C fork. Extensions and
  parsing options are selectable; supporting them all does not mean enabling
  conflicting interpretations simultaneously.
- Persistent block structure is the parser's structure. Rendering annotations
  are written directly from recognized blocks and resolved inline marks.
  There is no callback/event representation and no second persistent render
  index constructed from one.
- Incremental reuse operates on complete leaf blocks and container subtrees.
  Inline delimiter analysis reruns for affected leaves. This is the intended
  final granularity, not a temporary paragraph-only fast path.
- All source-dependent work completes before create/update returns. `md_get`
  cannot lazily parse because it receives no source.

The C below uses ordinary pointer spelling for readability. Implementation
signatures will use the project's `rop`, `ro`, and `rw` annotations consistently.
`u8`, `u32`, `u64`, `i32`, `Arena`, and `ScratchArena` are project types.

## 1. Required public-interface decisions

The current `md_interface.h` accepts caller-supplied scratch, but does not yet
represent all MD4C extensions or expose options. Do not silently implement a
different API, create public wrappers that acquire hidden scratch, or squeeze
unsupported syntax into unrelated node kinds.

The proposed public signatures are:

```c
MdState md_create(const MdView *source, Arena *arena,
                  ScratchArena *scratch, MdOptions options);

void md_update(MdState *state, const MdView *source, ScratchArena *scratch,
               u32 length, u32 offset, u32 removed_length);

MdResult md_get(MdState *state, Arena *arena, ScratchArena *scratch,
                u32 begin, u32 end);

void md_destroy(MdState *state);
```

`md_update` retains the current argument meanings and void return. Proposed
failure reporting is `state->error`, cleared on a successful operation. A
failed update leaves the previous syntax state usable. This error convention
also needs approval; it is not already specified by the public interface.

`md_get` allocates its complete result in the supplied Arena. It retains no
query buffer in the state. The interface now specifies that result storage
lives until the caller releases its Arena allocations; its coordinates describe
the document revision at the time of the query. Later parser calls do not
modify previously returned nodes.

Other semantics to settle before implementation:

1. Whether `data.code.start/end` describes code content or fence info. Proposed:
   retain both as explicitly named source ranges.
2. Query inclusion of zero-width replacements, particularly the final synthetic
   code newline at EOF. Proposed ordinary inclusion is `begin <= p < end`, with
   a specified EOF rule rather than silently dropping EOF insertions.
3. How source-range queries expose the display ordering of footnote definitions.
   Source extents stay source-anchored; footnote attributes carry display order.
4. Empty versus absent link titles and other optional attributes. Proposed:
   explicit presence flags; a zero-width range alone cannot distinguish them.

### Extension output required

Keep existing node kinds and add the following semantic kinds/attributes:

- TABLE, TABLE_HEAD, TABLE_BODY, TABLE_ROW, TABLE_CELL; row/column indices,
  column alignment, and header-cell status.
- Task state and task-mark source range on LIST_ITEM; not a replacement of the
  existing list-item node.
- ADMONITION with the recognized kind: note, tip, important, warning, caution.
- STRIKETHROUGH, INSERT, UNDERLINE, SPOILER, SUPERSCRIPT, SUBSCRIPT, HIGHLIGHT.
- MATH_INLINE and MATH_DISPLAY.
- WIKI_LINK with target source range and optional distinct label range.
- FOOTNOTE_REFERENCE, FOOTNOTE_DEFINITION, and FOOTNOTE_SECTION. Attributes
  include definition number/display order, reference occurrence and reference
  count. The section is synthesized from active footnote definitions; it is
  not a fake source block inserted into the source tree.
- Link target prefix metadata for autolinks whose semantic destination needs
  an implicit `mailto:` or `http://`. Raw source ranges remain raw ranges.

These are proposed additions to the one `MdNode` schema, not a parallel public
node format or a translation bridge.

## 2. Options, handles, and ranges

```c
typedef struct MdOptions {
    u32 flags;
} MdOptions;

/* Preserve the fork's option bit values. Public names can be finalized when
 * the interface change is approved. */
enum MdOptionFlag {
    MD_OPT_COLLAPSE_WHITESPACE       = 0x000001,
    MD_OPT_PERMISSIVE_ATX_HEADERS    = 0x000002,
    MD_OPT_PERMISSIVE_URL_AUTOLINKS  = 0x000004,
    MD_OPT_PERMISSIVE_EMAIL_LINKS   = 0x000008,
    MD_OPT_NO_INDENTED_CODE         = 0x000010,
    MD_OPT_NO_HTML_BLOCKS           = 0x000020,
    MD_OPT_NO_HTML_SPANS            = 0x000040,
    MD_OPT_TABLES                   = 0x000100,
    MD_OPT_STRIKETHROUGH            = 0x000200,
    MD_OPT_PERMISSIVE_WWW_LINKS     = 0x000400,
    MD_OPT_TASK_LISTS               = 0x000800,
    MD_OPT_MATH                     = 0x001000,
    MD_OPT_WIKI_LINKS               = 0x002000,
    MD_OPT_UNDERLINE                = 0x004000,
    MD_OPT_HARD_SOFT_BREAKS         = 0x008000,
    MD_OPT_SPOILERS                 = 0x010000,
    MD_OPT_SUPERSCRIPTS             = 0x020000,
    MD_OPT_SUBSCRIPTS               = 0x040000,
    MD_OPT_ADMONITIONS              = 0x080000,
    MD_OPT_FOOTNOTES                = 0x100000,
    MD_OPT_HIGHLIGHT                = 0x200000,
    MD_OPT_PRESERVE_BLANK_LINES     = 0x400000,
    MD_OPT_INSERT                   = 0x800000,
};

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

/* index == UINT32_MAX denotes no object. */
typedef struct MdLocalRange {
    u32 begin;
    u32 end;
} MdLocalRange;

typedef struct MdLocation {
    MdBlockId owner;
    u32 offset;
} MdLocation;

typedef struct MdEdit {
    u32 offset;
    u32 removed;
    u32 inserted;
} MdEdit;
```

Options are immutable for a state. A different dialect requires a new create,
not changing flags underneath retained syntax.

Generation-checked IDs survive relocation of the backing slot arrays. IDs are
private and never appear in `MdNode` or `MdResult`.

## 3. Persistent blocks and annotations

There is one ordered block tree. Sibling source extents are disjoint; container
extents enclose children. Source prefixes and otherwise invisible syntax still
belong to a leaf, so edits never fall into an unindexed hole.

```c
typedef enum MdBlockKind {
    MD_BLOCK_ROOT_INTERNAL,
    MD_BLOCK_TRIVIA_INTERNAL,
    MD_BLOCK_DEFINITION_INTERNAL,
    MD_BLOCK_PARAGRAPH_INTERNAL,
    MD_BLOCK_HEADING_INTERNAL,
    MD_BLOCK_QUOTE_INTERNAL,
    MD_BLOCK_LIST_INTERNAL,
    MD_BLOCK_ITEM_INTERNAL,
    MD_BLOCK_CODE_INTERNAL,
    MD_BLOCK_HTML_INTERNAL,
    MD_BLOCK_THEMATIC_BREAK_INTERNAL,
    MD_BLOCK_BLANK_INTERNAL,
    MD_BLOCK_TABLE_INTERNAL,
    MD_BLOCK_TABLE_HEAD_INTERNAL,
    MD_BLOCK_TABLE_BODY_INTERNAL,
    MD_BLOCK_TABLE_ROW_INTERNAL,
    MD_BLOCK_TABLE_CELL_INTERNAL,
    MD_BLOCK_ADMONITION_INTERNAL,
    MD_BLOCK_FOOTNOTE_INTERNAL,
} MdBlockKind;

typedef enum MdLineKind {
    MD_LINE_BLANK_INTERNAL,
    MD_LINE_TEXT_INTERNAL,
    MD_LINE_THEMATIC_BREAK_INTERNAL,
    MD_LINE_ATX_INTERNAL,
    MD_LINE_SETEXT_INTERNAL,
    MD_LINE_INDENTED_CODE_INTERNAL,
    MD_LINE_FENCED_CODE_INTERNAL,
    MD_LINE_HTML_INTERNAL,
    MD_LINE_TABLE_INTERNAL,
    MD_LINE_TABLE_UNDERLINE_INTERNAL,
} MdLineKind;

typedef struct MdLine {
    MdLocalRange source;
    MdLocalRange content;
    u32 indent_columns;
    u32 flags;
} MdLine;

typedef struct MdRestart {
    MdBlockId preceding_leaf;
    MdLineKind previous_line_kind;
    bool previous_line_loosens_list;
    bool item_started_with_two_blank_lines;
    bool valid;
} MdRestart;

/* One active interpretation. MdNode is the approved, extension-complete public
 * node schema. Positions are local to the owning block while stored here.
 * Reference link attributes are materialized through the label's winner;
 * direct-link attributes are local ranges in node.data.link. */
typedef struct MdAnnotation {
    MdNode node;
    MdLabelId reference;
} MdAnnotation;

typedef struct MdBlock {
    u32 generation;
    u32 next_free;
    bool alive;

    MdBlockKind kind;
    MdBlockId parent;
    u32 offset;                    /* Relative to parent. */
    u32 length;

    /* Structural parser facts, not rendering counters reconstructed later. */
    u32 marker;
    u32 marker_indent;
    u32 content_indent;
    u32 flags;

    union {
        struct {
            u32 level;
            MdLocalRange open;
            MdLocalRange close;
        } heading;
        struct {
            u32 start;
            u32 loose_children;
            bool ordered;
        } list;
        struct {
            u32 number;
            u32 loosening_lines;
            MdLocalRange task_mark;
            bool task;
            bool checked;
        } item;
        struct {
            u32 fence_character;
            u32 fence_length;
            MdLocalRange content;
            MdLocalRange info;
            MdLocalRange open;
            MdLocalRange close;
            bool fenced;
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
            u32 row;
            u32 column;
            u32 alignment;
            bool header;
        } cell;
        struct {
            u32 kind;
        } admonition;
        struct {
            MdDefinitionId definition;
        } definition;
    } data;

    MdBlockId *children;
    u32 children_count;

    MdLine *lines;
    u32 lines_count;

    MdAnnotation *annotations;
    u32 annotations_count;

    /* Prefix maximum ends for overlap lookup among ordered annotations. */
    u32 *annotation_max_ends;
    u32 annotation_max_ends_count;

    MdLabelId *reference_uses;
    u32 reference_uses_count;

    MdRestart restart;
} MdBlock;
```

Block children are ordered arrays, not linked allocation chains. Structural
edits rebuild the affected child arrays, reusing unchanged child IDs. Relative
positions limit relocation work to affected sibling lists/ancestor lengths,
not every annotation later in the document.

Structural nodes are derived from these block records during query. Inline
annotations and mappings are already final records. There is no persistent
copy of structural nodes in a separate output index.

Marker suppression belongs to source-owning leaves, including trivia leaves
for marker-only lines. A parent quote and its child paragraph must not both
claim the same hidden bytes.

Table cells own their inline records. Table separators and source outside cell
content remain indexed as syntax/trivia; they do not disappear from edit
coverage. Footnote bodies remain in their source location in the tree. Display
ordering is separate metadata, not destructive reordering of the source tree.

## 4. Definitions and dependencies

```c
typedef enum MdDefinitionKind {
    MD_DEFINITION_LINK,
    MD_DEFINITION_FOOTNOTE,
} MdDefinitionKind;

typedef struct MdDefinition {
    u32 generation;
    u32 next_free;
    bool alive;

    MdDefinitionKind kind;
    MdBlockId owner;
    MdLabelId label;
    MdLocalRange source;
    MdLocalRange destination;
    MdLocalRange title;
    bool has_title;

    /* Footnotes only. These are final semantic facts, not a traversal stack. */
    u32 display_number;
    u32 reference_count;
} MdDefinition;

typedef struct MdReferenceUse {
    MdBlockId owner;
    u32 offset;
    MdDefinitionKind kind;
} MdReferenceUse;

typedef struct MdLabel {
    u32 generation;
    u32 next_free;
    bool alive;

    MdDefinitionKind kind;
    u64 hash;
    u8 *key;
    u32 key_count;

    MdDefinitionId *definitions;
    u32 definitions_count;

    /* Includes unresolved lookup attempts, not only successful links. */
    MdReferenceUse *users;
    u32 users_count;

    MdDefinitionId winner;
} MdLabel;

typedef struct MdFootnoteOccurrence {
    MdDefinitionId definition;
    MdLocation source;
    u32 occurrence;
} MdFootnoteOccurrence;
```

Definitions for a label are kept in document order, including duplicates.
Deleting the winner can activate the next definition. Labels own normalized
lookup keys; they do not retain borrowed source strings.

Definition edits invalidate dependent inline leaves. Adding a previously
missing definition invalidates failed lookup users too. Inline analysis of a
leaf replaces all its previous reference-use entries as one transaction.

Footnotes also have a document-wide ordering dependency: changing the first
reference can renumber otherwise unchanged definitions. Recompute the active
footnote traversal/order from retained occurrences when its inputs change,
following the fork's first-reference rules and handling references in footnote
bodies. Ordinary inline syntax need not be reparsed merely to change a number.
Any syntax decision that depends on definition availability does need reparse.

Inactive footnote definitions and their dormant bodies are retained internally
but not exposed as active rendered definitions. Query visibility follows the
resolved active-definition set.

## 5. Durable allocation ownership and MdState

Caller Arena allocations have caller-controlled lifetime. The parser never
resets or destroys the caller's Arena.

Repeated edits must not allocate a fresh historical document forever. Retired
parser-owned arrays/records are recycled inside the state. Allocation headers
record size/alignment; arrays themselves still have only pointer/count fields.
The concrete Arena allocation entry points must be read before implementing
these helpers; their definitions are outside the current folder permission.

```c
typedef struct MdAllocation {
    struct MdAllocation *next;
    usize bytes;
    usize alignment;
} MdAllocation;

struct MdState {
    Arena *arena;
    MdOptions options;
    MdError error;
    u32 source_length;
    u64 revision;

    MdBlock *blocks;
    u32 blocks_count;
    u32 first_free_block;
    MdBlockId root;

    MdDefinition *definitions;
    u32 definitions_count;
    u32 first_free_definition;

    MdLabel *labels;
    u32 labels_count;
    u32 first_free_label;

    MdLabelId *label_buckets;
    u32 label_buckets_count;

    MdFootnoteOccurrence *footnote_occurrences;
    u32 footnote_occurrences_count;

    MdDefinitionId *footnote_order;
    u32 footnote_order_count;

    MdAllocation *free_allocations;
};
```

The state is single-owner. Copying this C struct does not clone its owned
records. `md_destroy` releases the logical state and clears the handle; physical
Arena reclamation is the caller's responsibility. Internal recycling controls
allocation growth during the state's lifetime. If the actual Arena API already
provides suitable recycling, use it rather than duplicate its allocator.

No source view, query buffer, parser context, or scratch allocator is retained
in `MdState`. No `MdStorage` or generic collection wrapper exists.

## 6. Operation-local structs

These exist only during a call. Their array storage can come from the supplied
ScratchArena, but none of them stores that arena or an allocator callback.

```c
typedef struct MdReader {
    const MdView *source;
    u32 length;

    u32 *piece_starts;
    u32 piece_starts_count;

    u32 piece;
    u32 position;
} MdReader;

typedef struct MdPhysicalLine {
    u32 begin;
    u32 content_end;
    u32 end;
} MdPhysicalLine;

typedef struct MdAnalyzedLine {
    MdLineKind kind;
    MdPhysicalLine source;
    u32 content_begin;
    u32 content_end;
    u32 indent;
    u32 data;
    bool begins_block;
    bool closes_fence;
} MdAnalyzedLine;

typedef struct MdContainerFrame {
    MdBlockId block;
    u32 marker;
    u32 marker_indent;
    u32 content_indent;
    bool previous_line_loosens_list;
    bool item_started_with_two_blank_lines;
} MdContainerFrame;

typedef struct MdMark {
    u32 begin;
    u32 end;
    u32 previous;
    u32 next;
    u32 auxiliary;            /* Index into typed operation-local link data. */
    u8 character;
    u8 flags;
} MdMark;

typedef struct MdLinkWork {
    MdLocalRange destination;
    MdLocalRange title;
    MdLabelId reference;
    u32 implicit_prefix;
    bool has_title;
} MdLinkWork;

typedef struct MdRestartPoint {
    MdLocation position;
    MdRestart context;
} MdRestartPoint;

typedef struct MdBlockChange {
    MdBlockId id;
    MdBlock replacement;
    bool removed;
} MdBlockChange;

typedef struct MdDefinitionChange {
    MdDefinitionId id;
    MdDefinition replacement;
    bool removed;
} MdDefinitionChange;

typedef struct MdLabelChange {
    MdLabelId id;
    MdLabel replacement;
    bool removed;
} MdLabelChange;

typedef struct MdUpdateWork {
    MdState *state;
    MdReader reader;
    MdEdit edit;
    MdError error;

    MdBlockChange *blocks;
    u32 blocks_count;

    MdDefinitionChange *definitions;
    u32 definitions_count;

    MdLabelChange *labels;
    u32 labels_count;

    MdBlockId *dirty_inlines;
    u32 dirty_inlines_count;

    MdBlockId *dirty_ancestors;
    u32 dirty_ancestors_count;

    MdAllocation *prepared_allocations;
} MdUpdateWork;

typedef struct MdParseWork {
    MdUpdateWork *update;
    MdBlockId leaf;
    MdAnalyzedLine pivot;

    MdContainerFrame *containers;
    u32 containers_count;

    MdMark *marks;
    u32 marks_count;

    /* Delimiter-stack heads, with UINT32_MAX for an empty stack. */
    u32 *opener_heads;
    u32 opener_heads_count;

    MdLinkWork *links;
    u32 links_count;

    u32 fence_character;
    u32 fence_length;
    u32 html_mode;
    u32 pending_blank_lines;
    u32 pending_blank_begin;
    u32 pending_blank_end;

    u32 html_comment_horizon;
    u32 html_processing_horizon;
    u32 html_declaration_horizon;
    u32 html_cdata_horizon;
} MdParseWork;
```

New and changed records are staged in `MdUpdateWork`; unchanged records are
read from the live state. Prepare-commit allocates the final durable arrays.
Commit installs those records and recycles retired allocations without any
fallible allocation. Abort recycles prepared allocations and leaves the old
semantic state intact. It does not copy the entire document/tree per edit.

Scratch allocation scopes are established/released using the real project
ScratchArena API. No invented scratch acquire/reset API should be implemented
before that API has been inspected with permission.

## 7. Function declarations and contracts

These are the intended internal functions, not compatibility wrappers. A
scratch parameter appears wherever a function can allocate temporary work;
functions which do not need scratch do not receive it.

### Allocation and record lookup

```c
void *md_durable_allocate(MdState *state, usize bytes, usize alignment);
void md_durable_recycle(MdState *state, void *allocation);

const MdBlock *md_block_read(const MdUpdateWork *work, MdBlockId id);
MdBlock *md_block_change(MdUpdateWork *work, MdBlockId id,
                         ScratchArena *scratch);
MdBlockId md_block_add(MdUpdateWork *work, MdBlockKind kind,
                       ScratchArena *scratch);
void md_block_remove(MdUpdateWork *work, MdBlockId id,
                      ScratchArena *scratch);

bool md_update_begin(MdUpdateWork *work, MdState *state,
                      const MdView *source, MdEdit edit,
                      ScratchArena *scratch);
bool md_update_prepare(MdUpdateWork *work, ScratchArena *scratch);
void md_update_commit(MdUpdateWork *work);
void md_update_abort(MdUpdateWork *work);
```

- Lookup reads the staged replacement first, otherwise the live record.
- A writable record is staged once; it is not an alias of the live record.
- Allocation headers are aligned for the actual payload.
- All count/byte-size arithmetic is checked. ID generations are validated.
- Begin validates the view and edit relation before syntax changes are staged.
- Commit cannot allocate or fail; prepare does all allocation/validation first.

### Piece reader

```c
bool md_reader_init(MdReader *reader, const MdView *source,
                     ScratchArena *scratch);
bool md_reader_byte(MdReader *reader, u32 offset, u8 *byte);
bool md_reader_match(MdReader *reader, u32 offset,
                      const u8 *bytes, u32 length);
bool md_reader_copy(MdReader *reader, u32 begin, u32 end, u8 *destination);
bool md_reader_line(MdReader *reader, u32 offset, MdPhysicalLine *line);
bool md_reader_decode(MdReader *reader, u32 offset, i32 *cp, u32 *width);
```

The reader validates NULL/empty pieces and total-length overflow. It never
assumes NUL termination or contiguity across a piece boundary. Small byte and
UTF-8 reads do not allocate. Token normalization allocates through explicitly
passed scratch in the calling grammar helper, not through a hidden reader
allocator. Full-document flattening is unnecessary.

### Restart, block recognition, and suffix reuse

```c
bool md_find_restart(MdUpdateWork *work, MdRestartPoint *point,
                      ScratchArena *scratch);
bool md_restore_context(MdParseWork *parse, MdUpdateWork *work,
                         const MdRestartPoint *point, ScratchArena *scratch);

bool md_scan_line(MdParseWork *parse, const MdPhysicalLine *line,
                   MdAnalyzedLine *analysis, ScratchArena *scratch);
bool md_accept_line(MdParseWork *parse, const MdAnalyzedLine *line,
                     ScratchArena *scratch);
bool md_finish_leaf(MdParseWork *parse, ScratchArena *scratch);
bool md_open_container(MdParseWork *parse, MdBlockKind kind,
                        const MdAnalyzedLine *line, ScratchArena *scratch);
bool md_close_container(MdParseWork *parse, ScratchArena *scratch);

bool md_can_reuse_suffix(MdParseWork *parse, const MdRestartPoint *old);
bool md_reparse_blocks(MdUpdateWork *work, ScratchArena *scratch);
bool md_recompute_ancestors(MdUpdateWork *work, ScratchArena *scratch);
```

A stored restart is valid only at a finalized boundary with no open leaf,
pending blank run, or partially consumed definition. Reconstruct ancestor
continuation frames from the tree and restore the boundary's preceding context.
Do not checkpoint arbitrary MD4C program state.

The restart search includes the preceding boundary when an edit can alter
line termination, paragraph interruption, a Setext/table underline, or the
lookahead that finalized the preceding block. It moves outward/backward when
necessary; it does not blindly trust the leaf containing `offset`.

Suffix reuse requires an unchanged mapped position, a compatible finalized
boundary, equivalent relevant container context, and no pending decision that
can absorb the suffix. Root is always a valid restart; reparsing to EOF is a
valid result when syntax never converges. Neither is a substitute for the
restart/convergence implementation.

Aggregate changes (list tightness, item numbering, table dimensions, ancestor
lengths) propagate independently of the source-line convergence check.

### Inline grammar and direct annotation storage

```c
bool md_parse_inlines(MdUpdateWork *work, MdBlockId block,
                      ScratchArena *scratch);
bool md_collect_marks(MdParseWork *parse, ScratchArena *scratch);
bool md_resolve_brackets(MdParseWork *parse, ScratchArena *scratch);
bool md_resolve_marks(MdParseWork *parse, ScratchArena *scratch);
bool md_store_annotations(MdParseWork *parse, ScratchArena *scratch);

bool md_annotation_add(MdParseWork *parse, const MdAnnotation *annotation,
                       ScratchArena *scratch);
bool md_hide(MdParseWork *parse, MdLocalRange range, ScratchArena *scratch);
bool md_replace(MdParseWork *parse, MdLocalRange range,
                 const u8 *text, u32 length, u32 repeat,
                 ScratchArena *scratch);
bool md_annotations_finish(MdParseWork *parse, ScratchArena *scratch);
```

Keep MD4C's mark-resolution precedence and delimiter rules. Completed pairs
write one semantic range and their source mappings, not enter/leave records.
Ordinary text does not allocate a record. Normalized code whitespace, entity
expansion, escaped punctuation, hard/soft breaks, NUL substitution and verbatim
block normalization are all handled explicitly.

Annotation finalization establishes deterministic ordering, builds the overlap
lookup metadata, and checks hidden/replacement exclusivity. Source suppression
is decided by the grammar's winning interpretation, not repaired heuristically
at query time. Two-scalar entities must fit the public replacement payload or
be represented without violating mapping exclusivity; verify this against the
actual entity table before choosing the finalized payload width.

### Reference definitions and footnotes

```c
bool md_normalize_label(MdReader *reader, MdDefinitionKind kind,
                        u32 begin, u32 end, u8 **key, u32 *key_count,
                        ScratchArena *scratch);
MdLabelId md_label_intern(MdUpdateWork *work, MdDefinitionKind kind,
                          const u8 *key, u32 key_count, ScratchArena *scratch);

bool md_definition_add(MdParseWork *parse, const MdDefinition *definition,
                        ScratchArena *scratch);
bool md_reference_lookup(MdParseWork *parse, MdLabelId label,
                          MdDefinitionId *winner, ScratchArena *scratch);

bool md_reconcile_definitions(MdUpdateWork *work, ScratchArena *scratch);
bool md_invalidate_reference_users(MdUpdateWork *work, ScratchArena *scratch);
bool md_resolve_footnote_order(MdUpdateWork *work, ScratchArena *scratch);
```

Lookup records successful and unsuccessful dependencies. Definition selection
uses source order, not slot/allocation order. Final inline reparses use the
post-edit definition table, including definitions later in the document.
Footnote traversal terminates on cycles and preserves the fork's ordering
semantics. Activating a previously dormant footnote body can enqueue further
inline/dependency work, which must complete before commit.

### Queries

```c
bool md_query_count(MdState *state, u32 begin, u32 end,
                     u32 *count, ScratchArena *scratch);
bool md_query_write(MdState *state, u32 begin, u32 end,
                     MdNode *nodes, u32 nodes_count, ScratchArena *scratch);

bool md_materialize_block(const MdState *state, MdBlockId block,
                           u32 absolute_base, MdNode *node);
bool md_materialize_annotation(const MdState *state, MdBlockId owner,
                                const MdAnnotation *annotation,
                                u32 absolute_base, MdNode *node);
```

Count then allocate exactly once in the supplied result Arena, then write.
Both traversals use the same selection predicate and deterministic ordering.
There is no persistent query cache and no dependency on a retained source view.

Queries include intersecting annotations and applicable enclosing effects.
Reference attributes resolve through winning definitions and block-local
anchors. Active footnote metadata is read from retained semantic state. No
syntax or reference availability is resolved lazily in `md_get`.

## 8. Top-level function definitions / call ordering

These definitions specify the intended control flow. They are not executable
placeholder bodies: the concrete Arena/ScratchArena scope and allocation calls
must use the existing project APIs after those are available for inspection.

### Shared mutation operation

```text
md_apply_change(state, source, edit, scratch):
    enter a caller-scratch scope
    initialize operation-local MdUpdateWork
    md_update_begin(work, state, source, edit, scratch)
    md_find_restart(work, point, scratch)
    md_restore_context(parse, work, point, scratch)
    md_reparse_blocks(work, scratch)
        reader_line -> scan_line -> accept_line
        accept_line -> finish_leaf / open_container / close_container
        finish_leaf -> definition_add where appropriate
        test reusable suffix only at finalized boundaries
    md_reconcile_definitions(work, scratch)
    md_invalidate_reference_users(work, scratch)
    parse dirty inline leaves against the final definition table
        collect_marks -> resolve_brackets / resolve_marks
        reference_lookup records successful and failed dependencies
        store_annotations -> annotations_finish
    resolve active footnote bodies and order to completion
        enqueue newly activated/reinterpreted bodies as necessary
        deduplicate work; terminate cyclic reference traversal
    md_recompute_ancestors(work, scratch)
    md_update_prepare(work, scratch)
    md_update_commit(work)                 // No fallible allocation here.
    state.error = NONE
    leave scratch scope

on any precommit failure:
    md_update_abort(work)
    state.error = operation error
    leave scratch scope
    retain the previous semantic state
```

No references to the reader, scratch arrays, view, or source bytes survive
commit. Prepare copies only staged final records into durable allocations.

### md_create

```text
md_create(source, arena, scratch, options):
    initialize MdState directly with arena, options, and empty slot lists
    validate options
    apply one insertion of the supplied source into an empty root
        -> md_apply_change(state, source, {0, 0, source_length}, scratch)
    return MdState by value
```

Failure returns an empty/destroyable state with its error set. There is no
separately allocated state wrapper and no hidden scratch acquisition.

### md_update

```text
md_update(state, source, scratch, length, offset, removed_length):
    validate offset/removal against state.source_length
    validate source length == old_length - removed_length + length
    -> md_apply_change(state, source,
                       {offset, removed_length, length}, scratch)
```

Unchanged bytes outside the replacement are a caller guarantee, as already
specified in `md_interface.h`. No exact old/new comparison is possible without
old bytes, and this API does not require one.

### md_get

```text
md_get(state, arena, scratch, begin, end):
    validate range
    enter a caller-scratch scope
    -> md_query_count(state, begin, end, count, scratch)
    allocate count MdNode records in the caller's result Arena
    -> md_query_write(state, begin, end, nodes, count, scratch)
    leave scratch scope
    return {nodes, count}
```

An empty result needs no allocation. Failure reporting follows the approved
error convention. Result records contain their own replacement bytes, not
pointers into temporary or durable parser-owned string buffers.

### md_destroy

```text
md_destroy(state):
    clear logical ownership and the state value
    do not reset/destroy the caller's Arena
```

Arena lifetime is explicit. If per-state immediate physical release is wanted,
that must be supported by the chosen caller Arena ownership/API, not by
resetting an arena that might also contain unrelated allocations.

## 9. MD4C keep / adapt / remove map

### Keep the algorithms and tables

- Unicode punctuation, whitespace and case-folding data.
- ATX/Setext heading, thematic break, fence and indentation recognition.
- Quote/list continuation and lazy-continuation grammar.
- HTML block/span recognition.
- Code-span matching and normalization.
- Emphasis delimiter classification, rule-of-three and pairing precedence.
- Bracket resolution and nested-link/image restrictions.
- Link destination/title/reference recognition.
- Entity recognition and the named entity data in `entities.h`.
- All extension recognizers: tables, tasks, math, wiki links, insert, deletion,
  underline, spoilers, super/subscript, highlight, admonitions, footnotes and
  permissive autolinks.

### Adapt

- `CH`, `STR` and direct `ctx->text` operations: use piece-aware reads. Pointer
  arithmetic consumers must be audited, not merely covered by a new macro.
- `md_analyze_line`: explicit parser context, stable block handles, direct
  block-tree mutations and restartable control flow.
- `md_process_line`, `md_start_new_block`, `md_end_current_block`: preserve
  recognition decisions, replace temporary byte-stream storage with staged
  persistent records.
- `md_enter_child_containers` / `md_leave_child_containers`: mutate block
  structure and parser frames, not paired output records.
- `md_collect_marks`, `md_analyze_marks`, `md_analyze_inlines`, extension mark
  resolvers: use bounded piece-aware access and direct annotation ownership.
- Link/reference attributes: source ranges and stable definition identities,
  not scratch-allocated strings or pointers packed into dummy marks.
- Table processing: retain rows/cells/alignment and source mappings directly.
- Footnote processing: retain definition bodies/dependencies and resolve active
  display order without moving source blocks or replaying callbacks.
- Normal/verbatim/code block processing: create mappings for only the bytes
  whose interpretation differs from ordinary source text.
- Label normalization/equality: preserve MD4C's equivalence rules while owning
  compact normalized keys in durable storage.

### Remove and replace, rather than wrap

- `MdEventCallback`, userdata and callback-specific timing.
- `MdEvent` as an intermediate representation.
- `md_emit`, `md_emit_block`, `md_emit_span`, `md_emit_text`.
- ENTER/LEAVE/TEXT callback-emission macros.
- The packed `block_bytes` stream and container opener/closer replay records.
- `md_process_all_blocks` as a replay-to-consumer pass.
- `md_process_inlines`' event output loop; replace it with resolved-annotation
  construction, keeping its normalization rules.
- `MdParser`, its public callback parse entry point, and its private arena.
- Pointer-bearing dummy marks and the title-pointer release stack.
- Source mirror, plain-text runs, style counters, layout-owned incremental
  parsing, and grapheme checkpointing.
- ASCII/UTF-16 build variants; the supplied interface specifies UTF-8 bytes.

The existing document-global `max_ref_def_output` counter needs a deliberate
replacement. It currently makes later reference recognition depend on earlier
expanded output. This parser stores attribute ranges rather than repeatedly
expanding referenced strings, so that output-expansion budget is not the right
safety mechanism. Use checked allocation/work bounds without silently changing
link interpretation based on unrelated earlier paragraphs.

## 10. Completion gates

All belong to the one replacement implementation, not a later feature phase.

- CommonMark fixtures and independent expected-output tests.
- Every supported extension and its option combinations, including precedence
  differences such as underline versus underscore emphasis.
- Incremental versus fresh-parse equality after edits to every syntax class.
- Definition duplication, unresolved dependencies, footnote activation,
  recursive references, ordering and distant invalidation.
- Piece repartitioning invariance, including boundaries at every byte of short
  fixtures, embedded NUL, malformed UTF-8 and split CRLF.
- Exact source ranges and mappings, escaped delimiters, multiline attributes,
  synthetic whitespace/newlines and two-codepoint entities.
- Range-query completeness, enclosing effects and zero-width boundary cases.
- Allocation failure before commit, valid-state preservation, stale ID checks,
  overflow handling and repeated-edit allocation reuse.
- No callback/event adapter, no source-dependent lazy query, no hidden scratch
  acquisition, and no retained source/scratch pointer.

## 11. Remaining permissions needed

1. Review of the proposed extension metadata/options and error/boundary
   semantics before further edits to `md_interface.h`. Caller scratch and
   arena/result ownership are already reflected in the interface.
2. Permission to read the actual Arena/ScratchArena declarations outside this
   folder, or copies of those declarations provided inside this folder. Do not
   invent allocator APIs from their type names.

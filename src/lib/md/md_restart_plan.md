# Fresh parser design discussion: CommonMark rules and incremental state

Status: design proposal, not an implementation commitment. This supersedes the earlier implementation approach, not the user-owned public interface. No parser or interface changes accompany this document.

Source: the downloaded [CommonMark Spec.html](CommonMark%20Spec.html), **CommonMark 0.31.2 (2024-01-28)**. All explanatory/rule text, including Appendix A, was reviewed; this is not a claim to have run all its example tests. Section numbers below identify the normative rules. The appendix supplies a parsing strategy, not an additional syntax requirement.

Code-block content dedenting/normalization is deliberately outside this discussion. Indentation that determines *which block exists* still matters.

## 1. The model

Use three distinct concepts:

1. **Source order and anchors:** where lines/chunks and syntax occur in the editable document.
2. **Parse state:** exact open-container context, current leaf mode, pending constructs, and inline delimiter/bracket stacks.
3. **Resolved syntax:** the block tree and inline effects produced by parsing, with source boundaries and necessary semantic attributes.

Raw syntax candidates are not the resolved tree. A backtick run can be an opening fence, closing fence, inline delimiter, or ordinary content. Unmatched delimiters also matter after edits. We must either retain the relevant raw candidates or rescan their source region during update; storing only successful delimiter pairs is insufficient.

A tree helps identify ownership and reuse. It does **not** prove an edit stops affecting the document at the old node's end.

### Two grammar layers

- **Blocks:** one line at a time, with an exact stack of open containers and a current leaf mode.
- **Inlines:** over a complete inline-bearing leaf, not independently over each physical line. Code spans, emphasis, and links may span lines within that leaf.

Block structure takes precedence over inline structure (§3.1). An inline code span cannot swallow a newly recognized block boundary.

Appendix A's block algorithm is especially important: match existing containers, consider new block starts, then append content. Do not immediately close an unmatched container before deciding whether the line is a legal lazy paragraph continuation.

## 2. Block rules and their consequences

### Physical lines and character classes — §§2.1–2.3

- LF, CR, and CRLF terminate lines. EOF can terminate a final unterminated line.
- A blank line contains only ASCII spaces/tabs, or no characters. This is not the same as arbitrary Unicode whitespace.
- Tabs affect structural indentation with four-column tab stops. They are not globally converted to spaces.
- U+0000 is replaced with U+FFFD; that is a rendering/source-mapping issue, not a structural delimiter.
- An edit can create or split CRLF across the edit boundary. Rescan the affected physical line boundary and its immediate neighbor when necessary.

Retain line lengths/endings and source-relative positions; compute structural columns separately from byte positions.

### ATX headings — §4.2

- At an eligible content-line start, after at most three spaces of indentation: 1–6 unescaped `#`, followed by space/tab or EOL.
- Seven opening hashes, or `#word`, are not headings in CommonMark.
- Optional closing hashes require preceding space/tab and only space/tab after the hash run. The closing run need not match the opening run's length.
- Heading content is inline-parsed. A heading may be empty and can interrupt a paragraph.

Retain the heading level and opening/optional closing marker ranges. Recognition is relative to the content start after container prefixes, not necessarily physical byte column zero. No persistent open-heading state is needed beyond this line.

### Setext headings — §4.3

- A nonempty paragraph-like sequence of one or more lines, followed by a line containing only a run of `=` or `-`, with at most three spaces of indentation and optional trailing spaces/tabs.
- Internal spaces/tabs in the underline are not allowed. A one-character underline can suffice.
- `=` means level 1; `-` means level 2.
- The preceding text must otherwise be a paragraph. The underline does not make arbitrary blocks into headings.
- It promotes the **whole eligible paragraph**, not just its final physical line.
- The underline cannot be a lazy continuation line of a quote or list item.
- Setext interpretation takes precedence over thematic-break interpretation when both are possible.

Keep an open/pending paragraph, its start anchor, and eligibility under the actual container context. An edit to an underline or separating blank line can require revisiting preceding paragraph text. A checkpoint at only the changed line is insufficient unless it retains that pending state.

### Thematic breaks — §4.1

- At most three spaces of indentation, then at least three matching `*`, `-`, or `_`, with spaces/tabs permitted between and after them; no other content.
- Can interrupt a paragraph.
- Setext takes precedence where applicable; otherwise thematic break takes precedence over interpreting the same line as a list item.

This is a one-line decision with a dependency on pending paragraph state for dashes. A character-presence bit alone cannot classify it.

### Fenced code — §4.5

- Opening fence: at least three consecutive backticks or tildes, after at most three spaces of indentation. Do not mix the two characters.
- A backtick fence's info string cannot contain backticks. A tilde fence does not have that restriction.
- Closing fence: same character, run length **at least** the opener's, at most three spaces of indentation, and only spaces/tabs after the run.
- Closing indentation need not equal opening indentation.
- Code fences do not nest. Apparent Markdown inside the body is literal content unless it meets this closing rule.
- Can interrupt a paragraph.
- Missing closer consumes the remainder of the enclosing container or document; there is no fallback to interpreting the opener as paragraph text.

Retain opener and closer anchors, fence character and length, enclosing container context, and opening indentation for later content handling. Retain or rescan candidate fence runs in the affected region, including runs that did not previously close the block.

Deleting an old closer may make a later opener become the new closer. It can change subsequent blocks all the way to EOF. Conversely, editing ordinary body text while preserving the line/container/fence state need not force scanning the entire code block again.

### Indented code — §4.4

- Four or more spaces of indentation in the relevant container context, but it cannot interrupt an existing paragraph.
- It may contain indented chunks separated by blank lines.
- A nonblank insufficiently indented line ends it; leading/trailing blank lines are not automatically part of its content.
- List containment is resolved before interpreting remaining indentation as code indentation.

Retain the leaf mode and pending blank-line run. Detailed removal of indentation from rendered content is deferred, not these structural conditions.

### Raw HTML blocks — §4.6

Seven distinct modes, not a generic balanced-tag stack:

1. `<pre`, `<script`, `<style`, `<textarea` with the specified following whitespace/EOL/`>` boundary, case-insensitive. Ends on a line containing any of the four corresponding closing tags, not necessarily the matching name.
2. `<!--` through a line containing `-->`.
3. `<?` through a line containing `?>`.
4. `<!` followed by an ASCII letter, through a line containing `>`.
5. `<![CDATA[` through a line containing `]]>`.
6. An opening/closing tag prefix from the fixed §4.6 block-tag set with the required boundary, through the line before a blank line.
7. A complete open tag other than the four special names, or a complete closing tag, alone except for following spaces/tabs, through the line before a blank line.

- Starts allow at most three spaces of indentation.
- Types 1–6 may interrupt paragraphs; type 7 may not.
- Types 1–5 include the whole terminating line, even text after the terminator.
- The enclosing container or EOF can terminate an otherwise unclosed block.
- Apparent new HTML starts inside an active HTML block do not change its mode.

Retain the exact mode and container context. Blank lines are significant terminators for only some modes. No HTML DOM construction is necessary.

### Paragraphs and blanks — §§4.8–4.9

- A paragraph comprises nonblank lines not claimed by other blocks.
- Later paragraph lines may have large indentation: indented code cannot interrupt a paragraph.
- An interrupting block start or blank line closes the paragraph, subject to setext promotion/reference-definition extraction.
- Blank lines affect list looseness and delayed code-block boundaries, even when they have no visible output.

Keep paragraph start/pending state and relevant blank-run state. Blank lines cannot simply be discarded from an incremental index.

### Block quotes — §5.1

- A quote marker is `>` after at most three spaces of indentation, with an optional following space of indentation.
- Quotes can contain arbitrary blocks and nest.
- A marker may be omitted only for a **paragraph continuation**, not for arbitrary lines inside the quote.
- This laziness can omit multiple nested quote markers.
- An unmarked blank line separates quotes; marked blank lines can remain inside a quote.
- Quote starts can interrupt paragraphs.

Keep an exact container stack and whether the deepest open leaf can accept lazy paragraph continuation. A count of `>` characters or a single `inside_quote` flag is not enough.

### List items and lists — §§5.2–5.3

- Bullets: `-`, `+`, `*`. Ordered markers: 1–9 ASCII digits followed by `.` or `)`.
- Marker width and following indentation determine the item's required continuation indentation. The normal following padding is 1–4 columns; code-first and initially blank items have separate rules.
- Empty items are legal, but cannot interrupt a paragraph. An initially blank item can begin with at most one blank line before its content.
- An ordered list interrupting a paragraph must start at number 1; other start numbers are allowed where no paragraph is being interrupted.
- Lazy continuation applies only to paragraph continuation text.
- A thematic break wins over an ambiguous list marker line.
- Lists group items with the same bullet character, or the same ordered delimiter. Marker changes can start another list.
- The first ordered number sets the list's start; subsequent source numbers do not control numbering.
- List looseness depends on blank separation between items, or between blocks directly inside an item. A blank inside a nested quote/code block/sublist does not automatically loosen an outer list.

Each open item needs its indentation/marker context; each list needs its type/start and an aggregate summary of looseness contributions. Updating that summary should touch the changed item/separator and ancestors, not rescan every item.

A new blank can alter the rendering attributes of an entire list without requiring every unchanged item's inline syntax to be reparsed.

### Reference definitions — §4.7, label rules in §6.3

- A label followed by `:`, a required destination, and optional title, starting after at most three spaces of indentation.
- Whitespace between components has specified line-ending limits; titles can extend over lines but not blank lines.
- Empty destination must be written explicitly as `<>`.
- Cannot interrupt a paragraph. Consecutive definitions can precede ordinary text without intervening blank lines.
- Definitions can occur in containers, but their scope is the whole document.
- The first definition of a normalized label wins.
- Appendix A proposes extracting leading definitions when a paragraph closes; this avoids pretending that every `[...]:` encountered mid-paragraph starts a definition.

Maintain a definition index and reverse dependencies from normalized labels to inline-bearing leaves. Include failed lookups: adding a definition can turn previously literal `[label]` text into a link far away. Keep duplicate definitions so removing the winner can activate the next one.

An edit to the destination of an existing winning definition may only require updating an attribute location. Adding/removing a winning label can change inline grouping and require reparsing dependent leaves. Treat those as different invalidation classes.

## 3. Inline rules and their consequences

### Code spans — §6.1

- Maximal backtick runs are delimiters; opener and closer lengths must be **equal**, unlike fenced blocks' greater-than-or-equal closing rule.
- Code spans can cross physical lines within the containing inline-bearing leaf, but cannot cross block boundaries. Each delimiter run itself is consecutive backticks on a line.
- Unmatched runs are literal text. Do not discard them permanently from update consideration.
- Inside a code span, escapes/entities/Markdown are literal. Backslashes cannot escape a prospective closing run inside code.
- Code spans, inline HTML, and autolinks have equal precedence and are recognized in source order. Do not parse all code spans globally before recognizing earlier HTML/autolink constructs.
- Line endings become spaces; one leading/trailing space is removed only when both are present and the content is not all spaces.

Record run lengths and pair them with a bounded scan/cache or per-length occurrence index. Do not scan the same suffix afresh for each unmatched opener. CommonMark does not prescribe the old fork's small fixed backtick-run limit.

### Emphasis / strong — §6.2

- Delimiter runs are `*` or `_`; escaped delimiters cannot act as syntax.
- Opening/closing eligibility depends on the Unicode whitespace/punctuation classes of neighboring code points. Unicode punctuation here includes both `P` and `S` categories.
- Left-flanking: the following character is not whitespace, and either it is not punctuation, or the preceding character is whitespace/punctuation. Right-flanking is the mirror condition.
- `*` opens/closes on left/right flanking respectively. `_` additionally restricts intraword use: an opening run that is also right-flanking requires preceding punctuation; a closing run that is also left-flanking requires following punctuation.
- Match the same delimiter character from distinct runs. If either delimiter can both open and close, their run lengths cannot sum to a multiple of three unless both lengths are multiples of three.
- Consume two delimiters for strong when both sides permit it, otherwise one for emphasis. Leftover run characters can participate in another enclosing match or remain literal.
- Empty emphasis is invalid. Earlier overlapping matches win; for a shared closer the later opener wins. Links/code/HTML grouping takes precedence over emphasis.

Use the appendix's delimiter stack and its `openers_bottom` bounds, keyed by character, closing-run length modulo 3, and whether the closer can also open. Successful resolution removes intervening delimiter candidates from further pairing. This establishes nesting during parsing, instead of repeatedly searching finished annotations for crossings.

Inline effects can span lines, so a stack reset at each physical EOL would be wrong. The containing inline leaf is the natural correctness boundary for reparsing emphasis.

### Links and images — §§6.3–6.4

- `[` or `![` opens a candidate; `]` triggers resolution of the nearest appropriate bracket opener.
- Inline link syntax must have `(` immediately after the text's `]`.
- A destination is either angle-bracketed with no line endings/unescaped angle delimiters, or an unbracketed sequence with no spaces/ASCII controls and balanced or escaped parentheses.
- A title may use single quotes, double quotes, or parentheses; it can span lines, not blank lines. Parenthesis titles do not permit unescaped nested parentheses. Component whitespace has the spec's one-line-ending limits.
- Full `[text][label]`, collapsed `[label][]`, and shortcut `[label]` references have defined precedence. No separating whitespace is permitted between bracket groups. An explicit undefined reference label must not silently become a shortcut for the preceding text.
- Labels contain at most 999 characters, no unescaped brackets, and at least one character other than space/tab/line ending. Normalization uses Unicode case folding and trimming/collapse of spaces, tabs, and line endings—not entity decoding or parsed inline appearance.
- Link text supports inline content and balanced brackets. Links cannot contain links; successful inner links deactivate relevant earlier link openers. Images differ: their descriptions may contain links.
- Code spans, HTML, and autolinks bind more tightly than bracket grouping; bracket grouping binds more tightly than emphasis when a link actually resolves.

Keep an exact bracket stack with active/inactive opener state, delimiter-stack boundaries for link text, and label dependencies. No scan of all previously emitted link nodes should be necessary to decide whether a link can open.

### Angle autolinks — §6.5

- URI form: `<scheme:...>`, scheme length 2–32, starting with an ASCII letter and followed by ASCII letters/digits/`+.-`.
- URI body excludes ASCII control characters, space, `<`, and `>`.
- Email form uses the specified ASCII local-part/domain grammar and supplies a `mailto:` prefix. This is not arbitrary real-world email validation.
- Backslash escapes do not operate inside autolinks.

Recognize bounded candidates on `<` in inline mode, with an exact recognizer, in source order with code/HTML. Bare `https://...` and `www...` recognition is an extension, not this rule.

### Inline raw HTML — §6.6

- Recognize syntactically valid open/closing tags, comments, processing instructions, declarations, and CDATA. Custom tag names are allowed; tag names must begin with an ASCII letter.
- Attribute names, quoted/unquoted values, and allowed intervening line endings have explicit grammar. A simple scan to the next `>` is insufficient because quoted values can contain `>`.
- No matching-tag or DOM tree is required.
- Backslash escapes do not escape HTML attribute delimiters. Entity spellings are preserved in raw HTML.
- Current comment syntax includes the short forms `<!-->` and `<!--->`, in addition to ordinary terminated comments.

Use small exact lexical states for tag/attribute/quote/comment forms, not general substring searches from every `<`. Cache failed scans or otherwise bound repeated lookahead over malformed input.

### Escapes, entities, and line breaks — §§2.4–2.5, 6.7–6.9

- Backslash escapes only ASCII punctuation. A backslash before other characters stays literal, except the EOL hard-break rule.
- Escaped punctuation and decoded entity characters must not be reinterpreted as structural Markdown delimiters.
- Named references require a valid HTML5 name and semicolon; decimal references require 1–7 digits; hexadecimal references require 1–6 digits. Invalid scalar values become U+FFFD. Code remains literal.
- A non-final line ending within inline content becomes a hard break when preceded by two or more spaces or the appropriate unescaped backslash, except in code spans/HTML tags.
- Other inline line endings become soft breaks. End-of-block trailing markers do not produce a final hard break.
- Otherwise the input is literal text.

Retain source mappings and compact decoded semantic data where required by the source-free query API. These transforms must remain subordinate to the surrounding lexical mode.

## 4. Anchoring in an editable document

### Proposed positional index

A balanced ordered sequence of source lengths, grouped into bounded-size leaves, with aggregate byte lengths above them:

- Stable internal IDs identify retained line/chunk records.
- Syntax anchors refer to a stable record plus a local byte offset and an explicit boundary bias where needed.
- Absolute offsets are computed from prefix lengths when locating an edit or materializing query output.
- An insertion changes the touched record(s) and aggregate lengths along the tree path, not every later syntax record.
- Split/merge/deletion of a record has explicit anchor repair for affected syntax. Stable IDs do not make those operations free.
- Long physical lines may occupy several storage chunks; the storage granularity need not force a one-megabyte indivisible allocation.

A plain absolute-offset array would require shifting an entire suffix after length-changing edits. Parent-relative offsets alone also do not solve updates to every later sibling in a large flat container.

The semantic tree and positional index have different jobs. They may share storage where useful, but nesting alone does not provide efficient order-statistics updates.

### Content hashes

Do not identify nodes by a hash of their content:

- Repeated identical text occurs at different locations and in different contexts.
- The same bytes can mean paragraph text, code, or HTML depending on preceding state.
- Collisions cannot be allowed to change parsing correctness.

Hashes can accelerate exact key lookup or provide a negative/equality precheck with verification. Reference-label lookup is a good hash-map use. The edit API already guarantees that source outside the replacement is unchanged, so it supplies suffix correspondence without hashing the entire document again.

Do not use `MdPiece` array positions or byte pointers as durable identities: the borrowed view can be repartitioned at every call.

## 5. Restart and convergence

1. Locate the edit through the positional index and repair affected physical-line/chunk boundaries.
2. Find an earlier valid checkpoint. For setext, reference-prefix recognition, or inline changes this may mean the containing/pending paragraph's beginning, not merely the changed line.
3. Resume the exact block state machine over the affected source.
4. Reuse an unchanged suffix only where its incoming interpretation state is equivalent and all relevant pending work is accounted for.
5. Resolve affected inline leaves using the completed reference map, and invalidate remote leaves whose definition availability changed.
6. Propagate changed lengths and semantic summaries only through affected index/tree paths.

Checkpoint state includes the relevant open container frames, leaf mode, fence/HTML parameters, paragraph eligibility/pending-definition state, and blank-line conditions. Open-stack snapshots must share unchanged frames; copying the full ancestor stack for every line can itself become expensive.

Separate **future interpretation state** from **output aggregates**. A changed list-looseness contribution may require updating a list attribute but not reparsing the unchanged suffix. Conversely, matching only a coarse `paragraph` enum does not prove convergence while unresolved delimiter/definition work differs.

Use safe completed-inline-leaf boundaries for inline reuse. Reparsing one affected paragraph linearly is acceptable as an initial design boundary; repeatedly searching that paragraph quadratically is not. Reusing within an open paragraph requires a stronger continuation-state argument than reusing opaque code-body lines.

Worst-case propagation to EOF is legitimate after edits to fences, containers, or HTML mode. The goal is to avoid unrelated work when state really converges, not to promise that every edit is constant-time.

## 6. Stack / bitset / Bloom filter

Use an **exact stack** for open constructs. It must preserve ordering, delimiter lengths, container indentation, and opener activity.

A Bloom filter cannot answer those questions. A counting Bloom filter can only be a probabilistic membership prefilter; collision positives still require exact resolution, and counters/deletion add bookkeeping.

For the small fixed set of syntax character/kind classes, an exact bitset is cheaper and more useful. Per-chunk masks can say that a region contains no backticks, brackets, or newlines and allow skipping irrelevant input. Masks must include boundary-sensitive candidates correctly; a token can cross a chunk/piece boundary.

Such a mask is an optimization of the scanner, **not** the parser state or a proof that a previously paired delimiter still pairs after an edit.

## 7. What happens on `md_get`

There are two meanings of “create blocks on get”:

- **Create the public `MdNode` result records on demand:** yes. Traverse resolved retained syntax intersecting the requested range, include required enclosing effects, and materialize absolute offsets/attributes.
- **Decide the grammar or pair delimiters on demand:** not with the current contract. `md_get` receives no source view, no source bytes may be retained, and source-dependent parsing is specified to finish during create/update. It would also make repeated viewport queries redo parsing and complicate dependencies outside the viewport.

No need to cache a giant, globally shifted public-result array. Retain compact resolved syntax sufficient to construct those records without reading source. Query ordering and nesting should come from that structure; do not repeatedly discover nesting by comparing every output interval against every other interval.

## 8. Extensions and unresolved design choices

The downloaded spec does **not** define `[[wiki links]]`, tables, task items, strikethrough, math, underline, spoilers, superscript/subscript, highlight/insert, admonitions, footnotes, or permissive bare autolinks. `[[` is not a special CommonMark opener.

These need their own explicit rule/precedence/state inventory from the owned extension behavior before the full grammar is finalized. They cannot all be treated as interchangeable delimiter pairs. Examples: tables can reinterpret a preceding line; footnotes introduce global definitions, reference order, and numbering; task items attach metadata to list items.

Recommended decisions to agree before new implementation:

1. CommonMark 0.31.2 is authoritative for core grammar; the fork remains a performance baseline and extension reference, not an authority for its existing core bugs.
2. Positional sequence index plus exact parser checkpoints and resolved syntax—not content hashes as identity and not a Bloom filter as state.
3. Grammar decisions during create/update; result-record construction during get, preserving the current interface unless the user explicitly chooses otherwise.
4. Affected paragraphs may be reparsed linearly; list/code container size must not automatically become the update work boundary.
5. Benchmark locality and adversarial delimiter scaling immediately when the new state machine is runnable, alongside spec conformance, rather than only after building the full parser.

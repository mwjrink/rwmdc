# RWMD editor implementation

The approved storage/window design is implemented. The frozen design artifact is
`docs/plans/edit-buffer-render-window.html`; this file describes the current code.

## Modules

- `src/main.c`: editor controller, queued command ordering, file/dirty state,
  selection/caret/scrollbar orchestration, event-driven rendering, metrics and the
  retained synthetic benchmark mode.
- `config.h`: pointer-free typed configuration, strict line/value parsing and
  temporary arena-backed file loading with a load-time font path consumer.
- `markdown/document.h`: aligned active/before/after byte storage, UTF-8/grapheme
  navigation, transactions, undo/redo, change deltas and streamed atomic file I/O.
- `markdown/parser.h`: owned single-header MD4C-derived engine, offset events,
  resettable scratch arena and parser phase/allocation profiling.
- `markdown/md_context.h`: retained slice-relative blocks, runs and inline delimiter
  records plus sparse grapheme checkpoints.
- `markdown/entities.h`: named/numeric entity decoding for display/source mapping.
- `markdown/layout.h`: delta-updated parser mirror, invalidation, visible-window
  layout, grapheme-safe hit/caret stops, wrap affinity and approximate scrolling.
- `markdown/font.h` / `slug.h`: compact font cache and buffer-backed Slug packing.
- `markdown/render.h` and `assets/shaders/text_slug.*`: device-address publication,
  rectangles, glyphs, synthetic emphasis, caret, and Slug coverage.
- `os/input.h` / window backends: queued UTF-8/key/pointer/scroll events, text
  conversion, repeat, focus and asynchronous clipboard protocols.
- `gfx/`: Vulkan device/queue/swapchain/frame slots and buffer/device-address helpers.
- `lib/kb/kb_text_shape.h`, `kb_gsub_plan.inc`, `kb_gsub_stream.inc` and
  `kb_profile.h`: isolated owned shaping fork, canonical indexed execution,
  and optional nested RDTSCP instrumentation; not used by the editor yet.
- `bench/shaping*`: separate upstream/owned/HarfBuzz adapters, equivalence runner
  and matched SSE2/AVX2 profiling/benchmark builds.

## Document invariants

`EditBuffer` has the approved 60-byte active payload and signed relative cursor
in its first aligned cache line. On the current target the complete object is
128 bytes. The before vector occupies its prefix; the after vector occupies its
tail. Logical bytes are before + active + after. Documents are limited to
INT32_MAX bytes and allocation growth is checked.

The cursor may lie outside active after navigation. Navigation changes only its
position, not source storage. A mutation materializes that position once; active
append/tail-delete operations take the local fast path. Large edits operate on
bulk spans instead of repeatedly filling the small array.

`document_spans` and `document_read` expose logical ranges. Borrowed spans expire
at the next storage mutation; history and parser caches never retain them. Undo
payloads are copied before reallocation/materialization, including when an input
aliases document or history storage.

UTF-8 is validated on load and insertion. Grapheme navigation uses utf8proc, with
ASCII fast paths and conservative context replay for non-ASCII hard-line prefixes.
CRLF pairs remain intact. User-visible caret positions are normalized; replacement
carets normalize forward when insertion merges graphemes.

Two identities serve different purposes:

- `Document.revision` increases for every replacement, undo and redo. Consumers
  use it to detect invalidated source views.
- `Document.state_id` identifies an undo history state. Returning to a saved state
  through undo/redo clears dirty status; replacing a discarded branch receives a
  different identity even at the same history depth.

Every mutation publishes `last_edit` (offset, removed length, owned inserted span,
revision). The controller forwards it to layout immediately, even when many input
commands are coalesced before a single render.

Load failure retains existing document state. Save streams logical spans into a
same-directory temporary, preserves permission bits, fsyncs/closes it, then renames.
It does not fsync the parent directory, so atomic replacement is not a promise of
power-loss durability. An unsaved close requires explicit second confirmation.

## Markdown context and visible layout

`parser.h` is an owned fork of MD4C master commit
`00788b157b2b6ef3efd8659b66f6521229f382d7`, with its MIT notice retained.
`RWMD_MARKDOWN_IMPLEMENTATION` compiles the engine in the application's translation
unit; there is no external MD4C library, cached parser object, or legacy callback ABI.
`md_parser_parse` emits `MdEvent` records with half-open UTF-8 byte ranges relative
to its input slice. Blocks carry physical source extents; spans also carry exact
opening/content/closing ranges. Text is either a source range or an explicitly
normalized codepoint/repeat with its consumed source range. No source strings or
temporary attribute pointers escape through the callback contract.

`MdParser` is zero-initialized and owns pointer-stable scratch chunks. Allocation
headers support sized reallocation; the latest allocation can grow in place.
Success and callback cancellation both reset scratch, retaining capacity for reuse.
Separate instances can parse from inside callbacks; same-instance reentry is rejected.
`md_parser_destroy` releases retained chunks outside an active callback. Persistent
index arrays live outside scratch and remain valid after reset.

The retained contiguous source mirror is patched with each edit delta; it is not
flattened from the edit buffer on every keystroke. Its byte suffix can still move
on insertion, and that cost is measured separately.

Safe plain-text edits within a top-level reference-free paragraph, including tail
appends and coalesced same-region edits, reparse into a separate active index.
The baseline prefix stays start-anchored; the suffix stays EOF-anchored. Views apply
one shared byte delta instead of shifting every trailing block/run/span/checkpoint
or splicing the tail arrays per character. A successful full reparse establishes
a new baseline. Region switches, delimiters, newlines, references, nested context
and uncertain batches conservatively reparse the document. These scans are timed,
not disguised as local work.

Layout consumes retained runs starting near a source anchor and creates only the
viewport plus overscan. It does not calculate global pixel height. Giant paragraphs
and large blank-line tails use local work/checkpoints instead of wrapping all
preceding text. Allocation capacities survive snapshot rebuilds.

The snapshot owns positioned glyphs, decorations, visual lines and source/caret
stops. Inline markup is hidden, entities are decoded, and stops are normalized
across parser-run boundaries. Visual source coverage is distinct from editable
line-end stops. Soft-wrap ambiguity is represented by a byte plus upstream affinity,
resolved against the current snapshot rather than a stale line index.

The editor preserves spaces and displays soft breaks as actual visual lines.
`MD_FLAG_PRESERVEBLANKLINES` produces source-aware blank blocks. A trailing LF/CRLF
adds one empty caret row without duplicating parser-generated line endings.
Large interior and trailing blank/space runs remain seekable with bounded layout.
Blocks shorter than the checkpoint stride skip the otherwise pointless Unicode
checkpoint pass; long blocks retain Unicode-aware boundaries and an ASCII fast path.

The viewport is source-anchored with local pixel translation. Estimates affect the
scrollbar, not anchored text. Scrolling within prepared coverage changes only the
translation; leaving coverage rebuilds a local snapshot. Byte-based thumb placement
is explicitly clamped when the final laid-out row reaches the visible bottom.

Navigation and editing remain source-based. Hidden Markdown delimiters still have
source positions; this is not a separate rich-text schema that automatically repairs
formatting around every range edit. Layout currently wraps at grapheme boundaries,
not a full typographic word-breaking/shaping engine.

## GPU ABI and publication

Push constants are exactly 32 bytes:

- byte 0: window-header VkDeviceAddress;
- byte 8: font-header VkDeviceAddress;
- byte 16: scroll translation (vec2);
- byte 24: local caret position (vec2).

The 64-byte, 16-byte-aligned window header contains glyph/rectangle addresses,
counts, viewport scale, text color and caret parameters. The 40-byte font header
contains immutable glyph/curve/band addresses and bounds counts. Static assertions
verify the CPU ABI; shader references use matching explicit alignment.

`GlyphDrawCmd` and `GpuRect` are 24 bytes. Glyph indexes reserve high bits for
synthetic bold/italic. Normal glyphs retain the previous Slug appearance. Rectangles
render behind text, and the caret renders last. GPU_RECT_FIXED ignores text scrolling
for the scrollbar. Selection/caret/header changes do not require glyph-list copying.

Font data is now all buffers: 40-byte glyph metadata, 12-byte packed half-coordinate
curve records, and linear band headers/indexes. Texture locations, images, samplers,
font descriptors and texture-addressing configuration are removed. Stable root
selection, nonzero winding and analytic grayscale coverage remain unchanged.

Vulkan enables bufferDeviceAddress, storage/device-address buffer usage and memory
allocation flags. Host-visible buffers remain mapped/coherent and prefer device-local
memory when supported. CPU pointers are never passed to shaders.

Each of two frame slots owns its buffers and cached revision/header. `start_frame`
waits its fence before writes or capacity replacement. Counts and addresses are
updated together when buffers grow; previous in-flight storage cannot be destroyed.
The first frame batches immutable font uploads; staging retires after the upload
slot completes. Shutdown drains the device before resource destruction.

## Input and scheduling

Backends append events until the controller consumes them. Text offsets index an
owned queue buffer, avoiding dangling pointers after growth. The controller copies
each event record before invoking APIs that may append more events.

Wayland uses xkbcommon/compose and compositor repeat settings. X11 uses state-aware
XKB symbols for commands and XIM committed text (or compose fallback) for text.
XKB map/new-keyboard events refresh cached mappings; XIM text output cannot overwrite
command identity. No physical-keycode-to-character approximation is used.

Clipboard completions use WINDOW_PASTE, distinct from keyboard WINDOW_TEXT. A paste
request pauses later queued input commands until its completion is applied at the
request position. Rendering, native polling, resize, blink and transfer progress
continue. Empty/error completions unblock without deleting the selection. This
preserves paste→type→save order even for asynchronous multi-chunk transfers.

Editor rendering is event-driven. The idle wait includes native/clipboard readiness
and repeat/blink deadlines. Input edits are applied before layout; one updated window
is published after the batch unless a hit-test or visual navigation requires an
up-to-date intermediate layout. Benchmark/finite-frame modes explicitly redraw
continuously for measurement.

Parser profiles separate structural scanning/reference work from inline processing
and event delivery. Both are inclusive of callbacks; optional `--profile-parser`
callback clocks measure the nested retained-index construction cost. Index
finalization, grapheme checkpoints, cache publication, source-mirror movement,
edit invalidation and visible layout have separate counters. Syntax-update samples
include all engine invocations needed for an update, including local/full fallback.
Allocation statistics count requests, arena chunk growth and actual reallocation
copies, with scratch peak and retained capacity. GPU publication/recording CPU time
is distinct from device timestamps. Detailed callback timing adds observer overhead.

## Owned shaping experiment

The immutable KB 2.25 baseline is in `bench/vendor`; the owned implementation is
in `src/lib/kb`. No runtime editor callsite has been migrated to either shaper.
The normal build and `just check` do not acquire a HarfBuzz dependency.

The owned changes preserve the exercised glyph/source/position outputs:

- Separate all-applicable lookup bits from default-enabled lookup bits. Explicit
  enables are intersected with glyph applicability before candidate admission; an
  off-only configuration avoids loading the additional bitmap.
- Generate exact ASCII Unicode properties from upstream with
  `bench/gen-kb-ascii.c`, retaining the full Unicode path for other codepoints.
  This cache does not bypass ASCII ligature processing.
- Initialize canonical input slots directly rather than construct and copy a full
  temporary glyph. Reuse the already-cached sequential lookup count.
- Detect ordered GPOS buckets with independent reductions. For disordered buckets,
  compact tombstones into reusable arrays, merge existing sorted runs stably,
  and update glyph back-pointers only when publishing the final order. Key
  selection uses the XOR/mask idiom; Clang produces conditional moves.
- Compile contextual GSUB 5/6 and GPOS 7/8 formats 1/2/3 (including extension
  subtables) into exact font-local symbol classes and ordered candidate buckets.
  Warm matching uses fixed-depth SIMD columns rather than raw contextual rules.
- Compile GSUB read/write dependencies into ordered execution stages. Native and
  compiled actions mutate the same canonical glyph records; stage/native admission
  is conservative and shared symbol-position indexes are repaired locally.
- Build glyph-to-stage default/possible admission rows once per configuration.
  Glyph configurations precompute wholly disabled stages. Runtime ORs those rows
  into occurrence bits; live feature values and exact flag requirements remain
  authoritative at each action. No lookup-to-stage scatter is rebuilt per glyph.
- Store canonical glyphs in stable array slots, with compact next/previous slot
  links and a recycled-slot stack. Ref minus one directly names the record;
  deletion leaves a tombstone, and reordering changes links rather than records.
  Native action frames, GPOS attachments/buckets, and context windows retain
  surviving identities. Borrowed surviving records remain at the same address
  until backing allocation growth. Fixed buffers never fall back to allocation;
  full clears retain capacity without scanning the array.
  `kb_glyph_storage.inc`, `kb_glyph_actions.inc`, `kb_execute_op.inc`, and
  `kb_script_shape.inc` separate storage, native actions, phase execution and
  script reordering. x64 records are 104 bytes; no GSUB row representation remains.
- Reserve for the maximum bounded nonbinary lookup expansion, not merely the
  number of feature overrides. This fixes an ASan-reproduced upstream overflow.

`KBTS_PROFILE=1` enables scope instrumentation around font/config construction,
shaping, normalization, GSUB/GPOS, input/output, bucket operations, substitution,
context matching and coverage checks. Detail stages separate lookup decoding,
filtered window gathering and comparisons. Compiled rule visits count active
candidate lanes; compared words include padded predicate checks. Six counters
distinguish simple/chained contextual formats. `kb_profile.h` documents their
boundaries. Compiled forward gathering combines input and lookahead timing.

Level 1 omits per-glyph/per-lookup timestamps; level 2 includes them. Work counters
are collected in those levels. Level 3 disables work/context counters and measures
coarse stages plus entire nonempty lookup batches. `KB_PROFILE_CORE_ONLY` removes
non-core scope code before any TLS access and rejects non-core runtime levels.
Disabled macros evaluate no arguments.
GCC/Clang cleanup attributes unwind lexical exits, returns and outward gotos.
Header-local TLS binds the active profile to its adapter TU. CPU migration
invalidates the affected timing intervals and ancestors. Setup profiles are
reported separately and counters reset after warm-up. Calibration/overhead
measurement happen outside active scopes; longjmp is not supported.

Core timing separates GSUB stages, GPOS metrics/fixup and GPOS queue reset.
Caller-owned arrays sized to the font's actual GSUB/GPOS lookup counts hold
total/sort/application counters. They share the normal cleanup stack and direct
counter-target accounting; a child lookup part subtracts from its parent total,
which subtracts from the surrounding stage. Stream setup/emission and GPOS queue
release remain stage residual.
Reset discards bindings; the adapter rebinds and clears its arrays after warm-up
and releases them after profiling. Raw lookup types retain extension wrappers.
The report lists all nonzero lookup records and per-shape totals. Core-only
measurements must still be paired with an uninstrumented control; no observer
overhead is silently subtracted.

Production benchmarks compile the scopes away. Earlier direct chained-ID matching
and predecoded-cache experiments informed the current compiler but are not parallel
runtime paths. There is no fallback to the old contextual interpreter.

### Compiled contextual representation

`kb_context_compile.inc` normalizes native-endian rules, interns glyph predicates,
partitions glyphs by exact membership, and emits per-subtable programs. Boundary
symbol zero is distinct from glyph zero and class zero. Dispatch by the first
symbol proves the anchor predicate, which is omitted from test columns. Bucket
windows retain exact backward/forward bounds. A paged classifier and 1/2/4-byte
dispatch indices avoid full-width tables where unnecessary; outcome references
use 16 bits when possible. Compact tests are two bytes (signed probe, predicate);
wide tests preserve longer contexts and larger predicate sets without a fixed cap.
Programs with at most 32 local predicates gather per-symbol membership bit masks,
avoiding a second dependent predicate-table load. Masks use packed nibbles for
at most four predicates (including ALWAYS_TRUE), otherwise 8/16/32-bit entries.
The generic global-predicate path remains available for larger predicate sets.

`kb_context_match.inc` gathers canonical glyphs in logical order for GPOS and
ordered native actions, retaining logical distances and stable identities.
Constant GSUB chains gather from those same slots. Initialization and substitution
keep cached symbols current. AVX2 evaluates eight candidate lanes, SSE2 four;
singleton buckets vectorize across stages.
Short paths are ALWAYS_TRUE-padded. Every stage runs for the initial active lanes,
even after failure; inactive bucket lanes cannot win. Priority occupies low mask
bits, so a nonzero mask plus `ctz` selects the winner. Final AVX2 assembly contains
`vpgatherdd`, vector shifts/conjunctions, mask extraction and `tzcnt`; the stage
loop has no failed-candidate exit. Constant GSUB stages avoid per-anchor parent
lookup unpacking; ordered native action execution remains separate.

`kb_context_fuse.inc` proves constant single-substitution outcomes across complete
input symbol domains, then composes locally justified rewrites. Earlier subtables,
rules and zero-record blockers participate in the proof; an unknown higher-priority
match prevents fusion. There are no hardcoded font, lookup or glyph IDs. Runtime
fusion additionally requires physical adjacency, matching lookup eligibility and
feature values. Isolated/check-only calls cannot consume future work.
Unproved cases retain ordinary ordered actions.

The bundled font's cache has 166 active contextual programs, 51 symbols and 816
outcomes. Its 472 native flat subtable indices map through a byte table to 167
dense descriptors, including a shared empty descriptor. Each descriptor is
64 bytes on x86-64, down from 80; native 16-bit backtrack bounds are retained
without truncating the potentially larger forward window or stage count.
Temporary local-to-global predicate mappings live outside descriptors and are
discarded after fusion. All native-flat consumers, including fusion proofs over
earlier subtables, resolve through the same mapping.

Final repacking interns byte-identical whole test, bucket, dispatch and local-mask
tables, with hash matches verified byte-for-byte. The runtime retains direct
pointers into shared immutable data; there are no dictionary lookups or dependent
suffix-node walks. Compact test arrays retain their trailing 32-bit gather guard.
This sharing differs from the earlier suffix-DAG experiment, which added links
and was larger than duplicated columns.

Program descriptors plus dispatch mapping occupy 11,160 bytes (formerly 37,760);
predicate storage is 5,609 (formerly 37,224); test columns are 4,312 (formerly
7,740); bucket/dispatch tables are 8,249 (formerly 16,082). The full allocation
is 56,159 bytes, down from 125,791. The hot prefix is 32,652 bytes; cold
candidate-outcome references, outcome records and fused glyphs follow it.
`COMPILED` reports unique table storage, `program_bytes`, and `hot_bytes`.
Fused results remain 394 directly lowered outcomes, including 159 compound chains.

### Canonical GSUB execution

`kb_gsub_plan.inc` retains the fixed-depth compiled matcher and dependency proofs.
Native actions, feature boundaries and ordered dependencies keep their original
lookup semantics. Execution uses the canonical `kbts_glyph` array directly:
there are no hot-row buffers, pending replacements, unchanged spans, source-origin
transport or full-stream representation bridges.

`kb_gsub_stream.inc` owns only scratchpad-derived indexes: stage-admission bits,
native slot bits, exact shared symbol-slot bits and conservative symbol presence.
Immutable default/possible glyph-to-stage rows belong to the configuration;
whole-stage exclusions belong to the glyph configuration. Runtime only combines
these rows with live occurrences. Native and symbol rows clear lazily on first
admission. Native-only plans allocate no symbol-position matrix; short native-only
operations scan their live range instead of building indexes.
Hot admission rows use the actual merged-stage word count, not the conservative
lookup-count upper bound reserved by the caller-owned sizing API.

Same-length writes, insertion and deletion repair affected slots locally. No
structural mutation triggers a full-run index rebuild. Forward/reverse native
continuations reload candidate bits after writes. Physically ordered slots use
bitmap enumeration; reordered slots use logical traversal within the same executor.
Constant actions assemble a candidate word once because they only write their
consumed prefix. Live feature/flag checks remain authoritative.

Complex-script clusters are logical range views. Removing every member leaves
an insertion gap bounded by the unchanged outside neighbors. Popping a range
restores that view, including on error unwind: it never compacts a suffix.
Normalization, reordering, GPOS and output all traverse the same canonical slots,
skipping tombstones but retaining live missing-glyph records with glyph ID zero.

GPOS keeps its existing buckets and algorithms. Its metrics pass assigns logical
ordinal sort keys and reconstructs the last attached-child bound; updates scan
only through that bound.
This preserves anti-topological direct-child propagation and reattachment without
repeatedly scanning a known-unattached suffix. GPOS is nonstructural; the bound is
reconstructed before a subsequent shaping pass.

Config sizing includes immutable plans and proof workspace. Scratch growth is
checked and uses the existing allocator, including fixed arenas; destruction
releases the reusable indexes. The canonical output is borrowed through existing
iterators, with no producer packing pass. The benchmark's consumer projection
remains separately timed.
The font caches the immutable maximum matcher window once during compilation.
Derived index/cmap allocations align typed data while retaining the original
allocator pointer; self-owned configurations and scratchpads likewise free their
original allocation rather than an aligned interior pointer.

### Prepared input and font mapping

The context prepares nonoverlapping runs over existing input with a `u32`
boundary array of length `RunCount + 1` and a separate metadata array. The terminal
boundary is the input count, including empty input; repeated terminal boundaries
preserve existing empty hard-line runs. Output counts and joining-context extents
are metadata, not additional input-span boundaries. Feature ranges do not split
runs. Graphemes are not split by a script transition, and joining reads can cross
a font boundary without emitting the context glyphs twice. The existing resolver
is retained; this is not a new paragraph-bidi implementation.

Configuration caching includes language. Script selection uses the applicable
script then DFLT, never an unrelated first script; an intentionally absent
LangSys stays absent. Required features are included, deduplicated and ordered
with optional features and cannot be disabled by an explicit zero override.

Every font has a 256-byte ASCII cmap. Dense, valid format-12 maps with at least
4,096 groups can additionally use deduplicated 32-codepoint pages, bounded by
the selected raw cmap's size; sparse maps keep range lookup. Page storage and its
allocator are font-owned, while language/script plans remain config-owned.
The raw selected cmap is retained. Compilation validates ordering and exact
glyph IDs, and allocation failures release temporary/resident storage.
Format-2 lead-byte aliases and format-4 zero-entry delta handling are corrected.
The scalar UTF-8 decoder is retained because the classification-LUT experiment
was slower. No experimental decoder mode remains.

### Compilation and scratch lifetime

`kb_compiled_context.h` defines internal shared types. Compilation owns one
aligned persistent allocation and releases temporary arena storage. Constructors
compile automatically; manually loaded/placed blobs require `kbts_CompileFont`
before any shape-config sizing or construction. Compilation is idempotent after
success and must finish before sharing the font. `kbts_FreeFont` releases the
compiled-context and optional cmap caches through their retained allocators,
independently of raw blob ownership.

Scratchpad sizing includes the largest contextual window plus SIMD guards.
Config-time Indic virama localization also needs compiled scratch state: sizing
reserves its temporary peak without executing substitutions; placement borrows
the not-yet-filled config tail, then reuses it for permanent matrices. A contextual
`locl` fixture reproduces the former null-scratch failure under ASan/UBSan.
`kb_context_test.c` also covers priority/blockers, class-zero boundaries, SIMD
bucket tails, wide probes/predicates, ignored marks, fusion guards, sparse native
indices, empty contextual dispatch, and mask-width boundaries. Allocation failure
injection includes every raw-cache, proof and final-repacking allocation. Tests
exercise final resident caches, not just intermediate compiler output, and run in
`just check` without HarfBuzz.
Stream regressions additionally cover writes distinct from read extents,
self/cross-lookup dependencies, blockers, overwritten compound-output interiors,
resizing followed by dependent actions, nested inserted outputs, ignored-mark
ligature metadata and reverse read-after-write behavior. A real-font sanitized smoke
run checked allocation/growth failure, fixed arenas, warm reuse, empty GSUB/GPOS
tables and balanced destruction.

Initial compiled-LUT alternating AVX2 4 KiB controls measured 410.7–410.9 µs shaping core before
compilation versus 360.8–364.7 µs after; total input/core/output fell from
473.8–475.0 to 428.6–433.0 µs. Setup rose from 1.38–1.48 to 8.09–8.61 ms.
Checksums matched and these warm runs requested no allocations. Direct RDTSCP
attributed about 275 µs to GSUB application; instrumented total time was 464 µs
(roughly 7–8% observer overhead). No sampling profiler was used for this attribution.

After table compaction, alternating controls measured 359.8–366.7 µs core for the
pre-compaction LUT versus 374.2–374.7 µs for the compact LUT. Total time was
427.9–434.9 versus 442.3–442.8 µs; setup was 8.10–8.29 versus 7.82–7.98 ms.
Checksums matched; warm allocations remained zero. Compaction is retained for its
55% allocation reduction, not claimed as a speed improvement. A 32 KiB table
prefix still competes with roughly 480 KiB of glyph records and lookup queues.
The compact direct-RDTSCP run attributed about 281 µs to GSUB application and
25 µs to sorting (467 µs instrumented total). Further performance work should
separate context gathering, SIMD selection and action/rebucketing costs.

The subsequent surrounding-code pass only split `kbts__SkipGlyph` into an inline
ordinary-glyph check and the unchanged slow filter body. Three paired controls
improved core time by 0.7–0.9%; tables, selector and glyph layout are unchanged.
That pass's final broad profile attributed 279 µs to GSUB application. Scoped PMU counts
show 6.99 million instructions per shape and approximately 23% no-retire cycles
waiting on a load; per-helper RDTSCP timing proved too intrusive for a reliable
finer split. `readme.md` records the workload and measurement qualifications.

The historical explicit-span scheduler was a performance regression: three
alternating uninstrumented AVX2 controls averaged 395.5 µs core before versus
432.4 µs after (9.3% slower), and 465.0 versus 503.1 µs total. Setup averaged
8.19 versus 8.20 ms. Inputs were 4 KiB, 16 variants, default features, CPU 0,
160 iterations × 5 batches. Checksums matched and warm allocation requests were
zero. This measurement does not include an implicit-pass-through alternative.

The verifier exercises five feature modes, ASCII including controls/NUL,
Unicode/combining cases, bucket-boundary lengths and deterministic repeated edits.
Adapters accept explicit script/language/direction. `shaping-stress` adds an
eight-font, six-script matrix with correctly tagged runs and UTF-8-safe edits.
The default matrix covers 9,760 cases and checks exact owned/upstream output.
It does not test paragraph bidi, font fallback or every possible shaping context.

The JSONL report records inputs before execution, font identities, run properties,
feature modes, output/coverage summaries and complete representative mismatches.
HarfBuzz differences are overlapping positional diagnostics, not correctness
verdicts or a suppression list. Missing nominal non-joiner glyphs are reported
separately from missing joiners. The reference adapter's nonbinary mode uses
caller-owned storage to avoid upstream's size-calculation defect; its header is
unchanged. Existing HarfBuzz behavior differences remain deferred.

File mapping/hash-prefaulting and font/config creation are reported independently
from warm shaping; these are not cold-disk measurements. No disk LUT persistence
or exhaustive all-font-shapes test is implemented in this pass.

### Canonical cutover verification and remaining gate

The current cutover passes `just check`, 555 exact owned/upstream cases on each
of SSE2 and AVX2, and 9,760 multi-font/script stress cases under SSE2 ASan/UBSan.
Additional sanitized smokes cover prepared empty/grapheme-safe runs, covered
Hangul/Lao behavior, range mutation/reuse/error unwind and dense-cmap ownership.
Anti-topological attachment, reattachment and cluster suffix preservation have
observable permanent regressions. Existing paragraph-bidi and source-map
limitations are not expanded into new conformance claims.

Three matched AVX2 4 KiB code repetitions measured 334.453 µs core and
389.746 µs total, versus 448.754/522.057 µs at task entry and
370.717/439.091 µs for the preserved pre-span executable. Every fixed workload
has matching checksums and zero warm allocation requests. The source-level
operator witness removes all row/span transport, reduces stream scans from
five to one and bitmap clears from 45,568 to 8,208 bytes. Its live requested
heap payload excluding raw font data falls by 213,174 bytes; this is not RSS.

**The complete performance gate is still blocked.** Compared with pre-span,
4 KiB prose is 7.7% slower, Devanagari 12.6% slower and combining-heavy input
75.5% slower; short-script regressions also remain. Larger Arabic and Thai and
all code sizes improve, but they do not cancel those failures. The retained
implementation is translation-free, not a claim that every acceptance gate
passed. `readme.md` records all workloads, phase timings, scoped/whole-process
counter qualifications, memory/decoder decisions and verification boundaries.

## Configuration lifetime

This pass exposes configuration parsing/loading only; `src/main.c` is intentionally
unchanged. `AppConfig` contains five flag/enum bitfields, three preallocation sizes,
a font-size scalar and four packed RGBA colors. It contains no strings or pointers.

`config_load(&values, &scratch, path, consume_font, userdata)` reads one stable file
into the existing arena, parses from defaults, and restores the incoming arena
length/checkpoint on success or failure. It does not allocate per key or retain
the input buffer. `config_parse` is the allocation-free counterpart for a caller's
writable buffer with one extra byte for its terminator.

Only the final `font.path` is consumed, and only after every line validates. The
consumer sees all final scalar values, even those appearing after the path line.
It must finish using both pointers before returning and allocate loaded resources
outside config scratch. A consumer is required when a path is specified; failure
is reported rather than ignored. Consumer failure leaves the scalar output intact;
the consumer is responsible for cleaning up any partially loaded resource.

## Evidence and boundaries

`just check` runs source/property and controller regressions under ASan/UBSan without
opening a Vulkan device. It covers random edits, aliases, undo state, file roundtrips,
parser delimiter/physical source ranges, CRLF/NUL normalization, arena reuse after
abort, nested parser instances, EOF-anchored suffix edits and undo/redo, context
reparsing, entities/graphemes, bounded windows, anchors, affinity, whitespace rows,
scrollbar EOF and queued paste order.
Configuration regressions additionally cover strict syntax, numeric overflow,
color/comment ambiguity, duplicate precedence, deferred resource consumption,
unchanged output on failure and scratch reuse across successful/failed loads.

Runtime checks exercised actual Wayland typing and Unicode save, undo-to-savepoint,
self clipboard and 267,800-byte external paste ordering, X11 keyboard/mouse navigation,
GPU growth beyond65,536 glyphs/256 rectangles, shrinking, caret-only and empty frames.
The unstyled BDA Slug capture matched all370,000 checked baseline content pixels.

Full font shaping/fallback, Wayland IME preedit, embedded media, optional Markdown
extensions, PRIMARY selection and drag/drop remain outside this implementation.
Structural reparsing and I/O are not guaranteed below1ms. CPU/GPU timings do not
measure photons or promise one-refresh input latency on every platform.

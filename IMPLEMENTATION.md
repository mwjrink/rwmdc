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
- `lib/kb/kb_text_shape.h` / `kb_profile.h`: isolated owned shaping fork and
  optional nested RDTSCP instrumentation; not used by the editor yet.
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
  enables are intersected with glyph applicability before queue insertion; an
  off-only configuration avoids loading the additional bitmap.
- Generate exact ASCII Unicode properties from upstream with
  `bench/gen-kb-ascii.c`, retaining the full Unicode path for other codepoints.
  This cache does not bypass ASCII ligature processing.
- Initialize pooled input glyphs directly rather than construct and copy a full
  temporary glyph. Reuse the already-cached sequential lookup count.
- Detect ordered buckets with independent reductions. For disordered buckets,
  compact tombstones into reusable arrays, merge existing sorted runs stably,
  and update glyph back-pointers only when publishing the final order. Key
  selection uses the XOR/mask idiom; Clang produces conditional moves.
- Reserve for the maximum bounded nonbinary lookup expansion, not merely the
  number of feature overrides. This fixes an ASan-reproduced upstream overflow.

`KBTS_PROFILE=1` enables scope instrumentation around font/config construction,
shaping, normalization, GSUB/GPOS, input/output, bucket operations, substitution,
context matching and coverage checks. Level 1 omits per-glyph/per-lookup scopes;
level 2 includes them. GCC/Clang cleanup attributes unwind lexical exits, returns
and outward gotos. Header-local TLS binds the active profile to its adapter TU.
CPU migration invalidates the affected timing intervals and ancestors. Calibration
and overhead measurement happen outside active scopes; longjmp is not supported.

Production benchmarks compile the scopes away. Native profiling of the original
default path showed high instruction volume with low branch-miss rates, so changes
target unnecessary work as well as branchless/vectorizable execution. A trial of
masked contextual gather bounds did not establish a useful gain and was removed.
The remaining lookup/context interpreter is still the dominant cost.

The verifier exercises five feature modes, ASCII including controls/NUL,
Unicode/combining cases, bucket-boundary lengths and deterministic repeated edits.
It checks exact equality against upstream, not general shaping conformance:
all supplied runs are Latin/en/LTR, and missing font coverage remains possible.
The reference adapter's nonbinary mode uses explicit caller-owned storage to
avoid upstream's size-calculation defect; the reference header is unchanged.

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

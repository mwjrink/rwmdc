# rwmd — Markdown editing with Slug

A native Markdown editor and rendering benchmark in C/Vulkan. The document stays
in CPU memory; a derived visible-window snapshot is drawn with Slug-style analytic
coverage. There is no raster/MSAA/supersampling backend or general 3D engine.

## Build and launch

Linux dependencies: Clang/LLVM with LTO, `just`, `glslang`, Vulkan headers/loader,
Wayland development files, utf8proc, and xkbcommon. The owned Markdown parser is
compiled from `markdown/parser.h`; no MD4C package or separate object is needed.
X11 builds also require libX11. Vulkan 1.3 dynamic rendering, synchronization2, swapchain presentation, and
buffer-device-address support are required. No mesh shaders, ray tracing, raw input
devices, or libevdev are used.

```sh
just build
./rwmd path/to/document.md
./rwmd --file path/to/new-document.md --font-pt 14 --fullscreen
just run                         # untitled sample document
./rwmd --help
```

A nonexistent file opens as an empty document and is created on save. Invalid
UTF-8 or other load failures are reported without overwriting the file. An
untitled sample needs a file path on launch to enable saving; no save-as dialog
is provided yet.

Run from the repository root so the bundled font and shaders resolve. Generated
xdg-shell sources are checked in. The shader compiler clears `LD_LIBRARY_PATH`
for its invocation to avoid mixing system glslang with incompatible SDK libraries;
the application uses the normal Vulkan loader environment.

Other targets:

- `just build-debug` → `rwmd-debug`, with Vulkan synchronization validation.
- `just build-sanitize` → `rwmd-sanitize`, with AddressSanitizer, UBSan and validation.
- `just build-x11` → `rwmd-x11`, the same editor on X11/XWayland.
- `just check` → deterministic configuration, document, parser, layout and controller regressions under
  sanitizers. These tests do not open a display or Vulkan device.

## Configuration parser

`src/lib/grim/config.h` provides the parser/schema and file-loading API. It is
**not wired into editor startup or runtime settings yet**; there is no `--config`
option or automatic config discovery in this pass.

```ini
image_cache.enabled = no
image_cache.gpu = no
cursor.shape = line
binds.find = ctrlf
buffer.prealloc_before = 32MB
buffer.prealloc_after = 32 MB
buffer.prealloc_undo = 32000000B
font.path = 'assets/fonts/JetBrainsMonoNerdFontMono-Regular.ttf'
font.size = 14
font.ligatures = n
colors.bg = #000014
colors.font_fg = 255 255 255 255
colors.comment_fg = 0.1,0.2,0.3,0.4
colors.block_comment_bg = #1F2226FF
```

Rules:
- Only lines whose actual first byte is lowercase ASCII are parsed; all other
  lines, including indented lines and whole-line comments, are ignored.
- Keys are exact `group.key` names. Spaces or tabs are required around `=`.
  Unknown keys, missing values, overflow and malformed values fail loudly with
  file/line/column diagnostics. Last assignment wins.
- Booleans accept `true/yes/y` or `false/no/n`. Enumerations are `line/block`
  and `ctrlf/slash`.
- Byte sizes accept bare integers or uppercase `B`, `KB`, `MB`, `GB`, optionally
  separated by whitespace. Units are decimal; `32MB` equals `32000000`.
- Font size is a positive decimal representable as `f32`; no signs or exponents.
- Colors accept `#RRGGBB`, `#RRGGBBAA`, or three/four components separated
  consistently by commas or whitespace. Any decimal point selects normalized
  0–1 components; otherwise components are integers in 0–255. Missing alpha is
  opaque. Values are rounded into the renderer's packed RGBA `u32`.
- Trailing comments require whitespace before `#`. Paths may be unquoted or
  single/double quoted, with no escaping, interpolation or expansion. Quote paths
  containing a whitespace-prefixed `#`.

The final `AppConfig` is pointer-free (36 bytes on this target). `config_load`
uses caller-provided arena scratch and rewinds it before returning, including
on errors. A `font.path` value requires a load-time consumer: after the entire file
validates, that consumer receives the final path and scalar settings, loads its
resource into separate storage, and discards the path. Neither the file buffer
nor a path pointer survives configuration loading. Existing scalar output remains
unchanged on failure. Defaults are disabled feature bits, line cursor, Ctrl+F,
32 MB per preallocation, size 14, and the current dark/white/gray palette.

## Editing controls

- Type UTF-8 text; Enter inserts a newline and Tab inserts a tab.
- Left/Right move by source graphemes; Ctrl+Left/Right move by whitespace-delimited words.
- Up/Down, Page Up/Down, Home/End use visual lines. Ctrl+Home/End go to document ends.
- Shift extends selection. Click places the caret; drag or Shift-click selects.
- Ctrl+A selects all; Ctrl+C/X/V copy/cut/paste through the system clipboard.
- Ctrl+Z undoes; Ctrl+Shift+Z or Ctrl+Y redoes; Ctrl+S saves.
- Wheel scrolling and dragging the approximate scrollbar are independent of the caret.
- Escape clears selection/dragging rather than closing the editor.
- Ctrl+Q or WM close requests exit. Unsaved changes require a second close request
  to discard; undoing back to the saved history state clears the dirty marker.

The editor is input-driven, not an uncapped redraw loop. It wakes for input,
clipboard progress, key-repeat deadlines, resize, and caret blink. A finite
`--frames N` run redraws continuously for verification/profiling and exits after
that many frames.

The owned parser is based on MD4C master (pinned provenance in `parser.h`).
Callbacks carry input-slice byte ranges, including inline delimiters, not borrowed
text pointers. The editor preserves source spaces, blank rows and soft breaks,
including trailing empty caret rows at EOF. Headings, lists, quotes, code, emphasis and
links are styled; bold/italic use documented synthetic weight/shear with the
bundled regular font. Images show alt text; HTML is literal text, never executed.
Optional Markdown extensions and embedded-media rendering are not enabled.

## Document and window model

`document.h` implements the approved 60-byte active insertion array, normal before
vector, and tail-aligned after vector. Cursor movement does not move text. A moved
edit position is materialized only when mutation requires it. Bulk edits bypass
repeated small-buffer flushing. Logical reads expose up to three borrowed spans.

Undo records own their byte payloads. Each mutation increments an invalidation
revision and emits an edit delta; a separate undo-state identity supports savepoint
tracking. Files preserve UTF-8 bytes, NULs and original line endings. Saving writes
and fsyncs a temporary file, then renames it atomically; parent-directory fsync is
not currently performed, so power-loss rename durability is not guaranteed.

`layout.h` keeps a contiguous parser mirror updated from edit deltas—not flattened
anew on every keystroke. Safe top-level paragraph edits reparse into a separate
active slice. Prefix records stay anchored to the start; suffix records resolve
relative to EOF through one shared offset. Typing does not rewrite or shift every
trailing block, run, span or grapheme checkpoint. Switching edit regions, structural
changes, references and uncertain context use a full syntax parse. Cursor motion
does not reparse. This does not measure global pixel height.

Parser scratch uses pointer-stable arena chunks, reset after success or cancellation
and reused by subsequent parses. Retained layout indexes own separate reusable
storage. Small blocks need no internal grapheme-checkpoint scan.

Layout rebuilds the visible region plus overscan into reusable glyph, line,
decoration and source/caret-stop arrays. Giant paragraphs use local layout and
sparse grapheme checkpoints for approximate seeks. Scrolling inside prepared
coverage, caret movement, selection and blink do not rebuild the glyph snapshot.
Viewport anchoring uses document bytes plus local pixel position; scrollbar size
and position are approximate, with explicit EOF clamping.

## GPU publication

Push constants are 32 bytes: GPU window-header address, GPU font-header address,
scroll translation, and local caret position. CPU pointers never cross this boundary.

All font data is buffer-backed: compact glyph metadata, packed-half quadratic
curves and linear band/index arrays. Slug's unstyled root/coverage calculations
are preserved. There are no sampled font textures, samplers or descriptor sets.

Two frame slots own mapped glyph, rectangle and header buffers. A slot's fence
must complete before writes or capacity growth. Unchanged glyph revisions are
reused; decorations/header/caret update independently. Rectangles draw behind text;
the caret draws above it. Fixed-coordinate rectangles implement the scrollbar.
Static font data uploads once; staging retires after its upload slot completes.

## Benchmarks and diagnostics

```sh
# Previous plain-text rendering baseline, independent of Markdown parsing:
just benchmark --font-pt 14 --fullscreen --frames 10000 --warmup 1000
./rwmd --benchmark --scene grid --columns 256 --rows 256 --update-every-frame

# Load/layout/render a real document for a finite run:
./rwmd path/to/document.md --frames 1000 --font-pt 14
# Include nested callback/index-construction timing (adds per-event clock overhead):
./rwmd path/to/document.md --profile-parser
```

Benchmark mode defaults to 2,000 measured and 200 warmup frames, 1920×1080 and
32-pixel ascent-to-descent font height. Editor mode defaults to 14 pt at 96 DPI
(18.667 pixels/em). `--font-size` instead specifies ascent-to-descent pixel height.
The last size option wins. `--animate`, `--scene`, and `--update-every-frame` are
synthetic benchmark controls, not editor input simulation.

Synthetic benchmark output reports CPU/GPU/wall percentiles and 1 ms exceedances.
The editor separates document mutation, source-mirror updates, invalidation/anchors,
parser block/reference scanning, inline/event processing, index finalization,
grapheme checkpoints, cache publication, visible layout, and GPU upload/recording
CPU work. Full/local invocation counts, parsed bytes, arena growth/peak/capacity and
reallocation-copy bytes are reported. `--profile-parser` additionally times callback
construction inside parser phases; nested times must not be added together.
Per-event clocks can materially slow a small parser workload, so they are off by default.

GPU time is not input-to-photon latency. Structural reparses and large pastes may
exceed 1 ms even when Slug rendering does not; these costs are reported separately.

The GPU-address conversion was compared against the earlier 14 pt Slug capture:
all 370,000 checked content pixels matched exactly. At 4K with 4,000 plain-text
draws, an observed GPU mean was about 0.063 ms. Real native-input tests preserved
UTF-8 and ordering through a 267,800-byte clipboard paste followed by typing/save.
These are observations, not hardware-independent guarantees.

Vulkan allocation counters are not physical residency and exclude swapchain and
driver internals. `last_upload_bytes` includes actual glyph/rectangle/header writes.
Do not compare sanitizer builds or overlapping GPU workloads with release timings.

## CPU shaping experiments

`src/lib/kb/kb_text_shape.h` is an isolated performance fork of KB 2.25.
`bench/vendor/kb_text_shape.h` preserves upstream commit
`b07373ded395b61d33a290181b957994db8edcfb`. Neither is wired into the editor yet.

### Caller-owned KB memory

KB takes ordinary writable pointers. The caller allocates and releases memory;
KB has no heap calls, allocator callbacks, or public memory descriptor. Each
placement function documents the size query its buffer must satisfy. Writable
buffers must not overlap and must remain valid while their objects are in use.

Font setup uses `kbts_LoadFont` to query native-blob sizes and, when needed,
`kbts_PlaceBlob` to write that blob. Query `kbts_SizeOfCompiledFont` and
`kbts_SizeOfFontCompileScratch`, then call `kbts_CompileFont`. The font borrows its
native blob and compiled output; compilation scratch can be reused immediately.
File loading belongs to the caller.

For an already compiled font, query `kbts_SizeOfShapeConfig` and
`kbts_SizeOfShapeConfigScratch`, then place the configuration into separate output
and construction-scratch pointers. Font and config compilation optionally return
the actual retained byte count through `OutputUsed`; their size queries are
construction upper bounds, not resident-memory measurements.

For direct shaping, the caller chooses a maximum **intermediate glyph count**:

```c
kbts_un output_bytes = kbts_SizeOfGlyphStorage(glyph_capacity);
kbts_un scratch_bytes = kbts_SizeOfShapeScratchpad(config, glyph_capacity);
/* Caller obtains output_memory[output_bytes] and scratch_memory[scratch_bytes].
 * Check nonzero bounds and allocation success before placement. */
kbts_glyph_storage storage;
int initialized = kbts_InitializeGlyphStorage(&storage, output_memory,
                                             glyph_capacity);
kbts_shape_scratchpad *workspace =
    kbts_PlaceShapeScratchpad(config, scratch_memory, glyph_capacity);
/* Check initialized/workspace; push glyphs and call kbts_ShapeDirect.
 * Reset storage to shape another input using the same buffers. */
```

The scratch query covers the complete operation within that glyph limit,
including repeated shapes; it is not merely an initial bookkeeping size.
Normalization and substitution can expand intermediate data, so input length
is not itself a safe glyph limit. Exceeding the glyph limit returns a shaping
error, never a heap fallback. Supplying fewer bytes than the published bound
violates the pointer API's precondition.

For automatic segmentation or manual runs, place a context into
`kbts_SizeOfShapeContext()` bytes and push already compiled fonts. Query
`kbts_SizeOfContextScratch(context, language, codepoint_capacity, glyph_capacity)`.
Pass the resulting scratch pointer, a destination fitting
`kbts_SizeOfGlyphStorage(glyph_capacity)`, and both semantic capacities to
`kbts_ShapeBegin`. Re-query after changing the font set. Per-pass input,
configuration caches, runs and runtime workspace live in scratch; the next begin
invalidates previous output. Font and feature stack values remain context state.
`kbts_ResetGlyphStorage` reuses direct storage without releasing backing memory.
Release caller buffers only after their objects are no longer needed.

### Canonical correctness and performance suite

`bench/shaping-suite.json` defines 152 named cases: English/Latin, Arabic,
Hebrew, Hindi/Devanagari, Bengali and Thai; eight fonts; code, prose, programming
and text ligatures, literal Markdown source, and combining-heavy text; and 64,
512, 4,096 and 65,536-byte input budgets. Arabic and Hebrew run RTL. Default
features cover every length. Feature overrides cover short and 4 KiB cases;
the six dedicated ligature/Markdown workloads exercise default/off/explicit.
Inputs are presegmented script runs, not paragraph-bidi or Markdown layout tests.

The driver uses Python 3.9+'s standard library, Clang, `pkg-config`, HarfBuzz
development files, the bundled JetBrains Mono font and seven Noto regular TTFs
under `/usr/share/fonts/noto`. Change the Noto location with `--font-dir`.
Missing selected fonts fail explicitly; `--allow-missing-fonts` is an explicit,
recorded exclusion, never a substitution or a passed test.

```sh
just shaping-suite list
just shaping-suite check --isa avx2 --isa sse2 --output /var/tmp/kb-check
just shaping-suite bench --cpu 0 --output /var/tmp/kb-bench

# Select named cases rather than editing a temporary benchmark script:
just shaping-suite check --case '*-default-64'
just shaping-suite bench --case 'arabic-*' --isa avx2 --repetitions 3

# Use a single case from the benchmark's hotspot ranking:
just shaping-suite profile --case arabic-default-65536 --isa avx2 \
  --engine owned --kind sample --output /var/tmp/kb-arabic-sample
just shaping-suite profile --case arabic-default-65536 \
  --engine harfbuzz --kind counters
just shaping-suite profile --case arabic-default-65536 \
  --engine owned --kind core
```

`check` compares every output glyph's ID, source offset, two advances and two
offsets over 16 changing variants and two passes per case. The 92 English/code,
ligature, Markdown and combining cases require exact HarfBuzz equality
(`harfbuzz_required: true`). The remaining 60 complex-script cases require
original KB equality and retain HarfBuzz differences as diagnostics. The
nonselected reference remains visible; neither glyph nor source differences
are discarded. `bench` also gates equivalence against the case's selected reference.

HarfBuzz uses `HB_BUFFER_CLUSTER_LEVEL_CHARACTERS` to match KB's per-character
source provenance, rather than its default grapheme-cluster grouping. This
setting is recorded in metadata. Glyph IDs and all four geometry fields remain
exact comparisons; matching a short script sample is not general conformance.

The owned normalizer preserves nominal mappings instead of substituting
canonical singleton aliases (`;`/Greek question mark and `K`/Kelvin sign).
It orders marks before recomposition, refreshes the composed base, respects
canonical blocking, and ends fraction digit runs at intervening text. Original
KB retains these bugs, so equality with it is not the Latin correctness oracle.
The synthetic normalization regression also preserves missing-glyph fallback.
The GSUB gate also honors explicit feature enables such as `frac=1` outside
automatically detected ranges; per-character disables retain precedence.

The legacy `shaping-verify` and `shaping-stress` tools retain broader feature,
mutation and API diagnostics. Their original-reference mode can now reject
intentional normalization corrections. Legacy stress also has a pre-existing
Arabic joiner warmup discrepancy (seed 1801614451, joiners ordinal 2); a canonical
`check` pass is not a claim that the broader legacy stress suite passes.

`bench` runs all three engines serially, rotating engine order across three
repetitions and reversing case order on alternating repetitions. Each invocation
warms all 16 variants twice and records five batch means. This warmup protocol
is retained from the allocator-based baseline, where one pass left additional
workspace growth in the first measured combining-heavy pass. Default iterations per
batch are 1,000 / 200 / 30 / 1 for the four lengths; `--iterations`, `--batches`,
`--repetitions` and `--warmup-passes` override them. Repeat `--isa` to measure
both SSE2 and AVX2. The default selects AVX2 when available, otherwise SSE2;
installed HarfBuzz retains its own build and dispatch.

Each new result directory contains the manifest/selection, source and font
SHA-256 hashes, CPU/affinity and compiler metadata, exact build/run commands,
raw stdout/stderr and incremental JSONL records. Final `results.json`,
`summary.csv` and `comparisons.csv` retain phases, repeat distributions, setup,
memory usage and checksum-qualified ratios. Unequal-output timings never become
equivalent-work speedups. Batch-mean percentiles are not individual-shape latency
percentiles. HarfBuzz memory accounting is unknown, not zero.

`RESULT` records use schema 3 (`CHECK` and `SAMPLE` remain schema 1). Their typed
`memory` object distinguishes:

- `caller_buffers`: owned KB reports native-blob bytes; compiled-font and
  shape-config output bounds, actual retained bytes and construction-scratch
  bounds; glyph-config bytes; and destination and shaping-scratch bounds.
- `legacy_allocator_requests`: original KB's setup/warmup cumulative requests and
  requested bytes, plus measured request/byte/free deltas. These are allocation
  traffic, not retained memory.
- `not_instrumented`: HarfBuzz; no invented zero values.

The Linux benchmark caller obtains buffers from the library's size queries
during setup, using virtual mappings without resizing during shaping. Its glyph
limit is `max(256, codepoint_count * 8)`, a corpus policy rather than a theorem
about arbitrary font expansion. Scratch size is calculated from that limit and
the config, not guessed in bytes. Bounds are not RSS; accounting excludes the
host adapter, comparison-output arrays and profile bookkeeping. CSV columns
retain the buffer fields and prefix reference-allocator fields with `legacy_`.

Timings include input preparation, shaping and consumer output copying; setup,
file I/O and output hashing are outside those timers. Setup is engine
construction, not first-result latency: HarfBuzz can defer work to shaping.
Profiles run separately and calibrate toward `--seconds` (default five).
`sample`/`counters` require Linux `perf` and user-PMU permission and cover the
whole process, including warmup/setup/hash work. `core` compares an identical
production control against KB's TSC stage/lookup observer; TSC ticks are not
hardware core cycles. Unsupported counters, timeouts and incomplete runs fail
visibly. No CPU-frequency lock or system isolation is imposed.

Omit `--output` for a unique directory under `/var/tmp`; an explicit output
must not already exist. `--timeout` bounds each subprocess, not the whole suite.
The legacy builder also supports `SHAPING_OUTPUT_DIR` (otherwise `$TMPDIR` or
`/tmp`) and `SHAPING_BUILD_ONLY=1`; the canonical driver uses both.

### One-off benchmarks and stress diagnostics

```sh
just shaping-bench avx2 --size 4096 --features default
just shaping-bench sse2 --size 4096 --features default
just shaping-bench avx2 --features explicit
just shaping-bench avx2 --font /path/to/font.ttf --corpus prose
just shaping-core avx2 --engine owned --size 4096
just shaping-core sse2 --engine owned --size 4096
just shaping-profile avx2 --engine owned --profile coarse
just shaping-profile avx2 --engine owned --profile detail --size 1024
just shaping-verify avx2
SHAPING_SANITIZE=1 just shaping-verify avx2
just shaping-stress avx2 --report /tmp/shaping-stress.jsonl
SHAPING_SANITIZE=1 just shaping-stress avx2 --report /tmp/shaping-stress-asan.jsonl
just shaping-profile avx2 --engine owned --profile detail \
  --font /usr/share/fonts/noto/NotoSansArabic-Regular.ttf \
  --script Arab --language ar --direction rtl --corpus script
```

The two KB adapters use identical `-O3 -march=x86-64` flags; AVX2 adds only
`-mavx2`. No AVX-512 requirement is introduced. The installed HarfBuzz library
retains its own build and runtime dispatch. Benchmarks pin a permitted CPU, reuse
objects and buffers, rotate engine order, and shape sixteen changing-input
variants. Input, shaping, output, checksums and typed memory usage are reported.
Percentiles describe batch means, not individual input latency.

Default means no redundant feature overrides. `off` disables `calt/liga/clig`;
`explicit` enables them. `optional` enables `frac/ss01/cv01`, while `alternate`
requests `aalt=2`. One-off verification defaults to original KB;
`--reference harfbuzz` selects HarfBuzz instead. The selected reference gates
glyph IDs, per-character byte sources, advances and offsets exactly, while the
other reference is diagnostic. Use the canonical suite for its per-case policy.

The stress suite requires the bundled font and Noto Sans/Serif, Arabic, Hebrew,
Devanagari, Bengali and Thai regular TTFs. `--font-dir DIR` changes the default
`/usr/share/fonts/noto` directory. Missing fonts fail explicitly; no downloads or
silent fallbacks occur. Script/language/direction are set for each presegmented
run. `--stress-count`, `--stress-length` (codepoints), `--stress-scale` and `--seed`
control deterministic phrase, combining-mark, joiner and operator/ligature cases.
The default matrix runs 9,760 cases across eight fonts and five feature modes.

JSONL reports retain font hashes, exact input strings, run properties, nominal
coverage, output checksums, first differences and representative full glyph arrays.
Owned/upstream differences fail the run. HarfBuzz differences are categorized
positionally as count, glyph, geometry and source-map differences, not automatically
declared bugs. Existing HarfBuzz discrepancies are preserved for later work.

The fork adds coverage-aware feature overrides, a generated ASCII property LUT,
direct glyph initialization, canonical indexed GSUB execution, and
stable contiguous bucket merging for GPOS. Clang emitted SSE2/AVX2 for the
ordered-bucket scan and `cmov` for merge selection without explicit SIMD intrinsics.

Initial-pass 4 KiB code-corpus batch medians on this workstation, instrumentation off:

| Font / mode | Upstream KB | Owned KB |
|---|---:|---:|
| Bundled JetBrains Mono, AVX2 defaults | 596 µs | 520 µs |
| Bundled JetBrains Mono, SSE2 defaults | 585 µs | 533 µs |
| Bundled JetBrains Mono, AVX2 explicit enables | 12,334 µs | 553 µs |
| Noto Serif, AVX2 defaults | 357 µs | 274 µs |

The current contextual matcher compiles GSUB/GPOS rules into font-local symbols,
priority-ordered candidate buckets and fixed-depth SIMD test columns. Short paths
use ALWAYS_TRUE padding; stages do not stop when a candidate fails. A nonzero
winner mask selects its earliest rule with a trailing-zero count. The old direct
chained-ID matcher and contextual interpreter have been removed from the owned fork.
Proven single-substitution chains become compound outcomes, including the bundled
font's three-position `<=>` rewrite. Feature boundaries, blockers and ignored
glyphs remain semantically significant; unsafe fusion uses ordinary ordered actions.

Initial compiled-LUT paired 4 KiB code-corpus runs (before table compaction),
CPU 0, AVX2 defaults, 16 changing inputs, 100 iterations × 7 batches, instrumentation off:

| Measurement | Pre-LUT owned | Compiled owned |
|---|---:|---:|
| Shaping-core batch median | 410.7–410.9 µs | 360.8–364.7 µs |
| Input + core + output batch median | 473.8–475.0 µs | 428.6–433.0 µs |
| One-time setup | 1.38–1.48 ms | 8.09–8.61 ms |

This is about 11–12% less core time and 9% less total time, not a 10 µs bound.
All four paired runs produced checksum `12ad74c63c50c10a` and requested no warm
allocations. Cached symbol initialization adds input work; compilation adds setup
work. Varied stress workloads can still grow the reusable sort workspace.

The initial compact cache for bundled JetBrains Mono contained 51 symbols, 166
active contextual programs and 816 outcomes. Native subtable indices map to dense
64-byte program descriptors, with one shared empty program. Local membership
masks use 4, 8, 16 or 32 bits per symbol. Identical mask, dispatch, bucket and test
tables share storage; this needs no runtime dictionary lookup or suffix traversal.
Compilation-only predicate mappings are discarded after fusion.

| Resident component | Before compaction | Compact |
|---|---:|---:|
| Program descriptors and mapping | 37,760 B | 11,160 B |
| Predicate tables | 37,224 B | 5,609 B |
| Test columns, including guards | 7,740 B | 4,312 B |
| Buckets and symbol dispatch | 16,082 B | 8,249 B |
| Complete cache | 125,791 B | **56,159 B** |

The hot matching prefix is **32,652 bytes**, just below 32 KiB; cold outcome
references, actions and fused glyphs follow it. This is a capacity measurement,
not a guarantee of L1 residency: glyphs and stream buffers compete for cache space.
The 394 lowered outcomes, including 159 compound chains, are unchanged.

Compaction is a memory win, not a measured speed win. Final alternating controls
measured 359.8–366.7 µs core before compaction versus 374.2–374.7 µs after;
total input/core/output was 427.9–434.9 versus 442.3–442.8 µs. Setup was
8.10–8.29 versus 7.82–7.98 ms. Checksums matched and warm allocation requests
were zero. The smaller representation is retained for its footprint. These
historical figures precede the GSUB stream cutover below.

### Canonical shaping pipeline

Font construction establishes actual blob extents and one immutable checked
lookup/subtable view. Extension types/directions and GDEF glyph/attachment classes
are compiled once. Context compilation groups coverage/class sources, retains
logical rules and ordered dispatch through fusion, and emits one interned resident
cache; there is no intermediate full cache to decode and repack.

The exact classifier remains font-global. Configurations accumulate ordered roots
per semantic stage and derive their reachable closure, admission rows and window
bounds from required/default/optional/nested consumers. Stage boundaries use
`u32`; a selected language is not restricted to 32 font features. Temporary proof
storage, conservative construction bounds and packed resident size are distinct.

Normalization, compiled/native GSUB, GPOS and output share one stable-slot
`kbts_glyph` array. Deletion leaves tombstones; insertions recycle slots; logical
links carry order. Placement fixes the slot capacity; neither records nor metadata
move afterward. There is no prepared-glyph mirror, span translation or cold
synchronization. Links also carry the direct-child attachment index; the public
glyph-storage sizing query includes records, links, free slots and alignment.

Native and symbol-position indexes have independent workspace partitions. A
bounded active-range probe—not total storage size—selects the small native path.
Readable word extent is separate from reserved capacity.
Consecutive GSUB feature operations share one indexed interval without merging
baked stages or changing lookup order; every non-GSUB opcode remains a barrier.
Bulk construction hoists interval masks and shares admission rules with live
mutation updates, directly over the existing glyph records.
Mutations repair live consumers, not completed native rows. Compiled matching
retains its fixed-depth all-stage SIMD contract and original lookup priority.

GPOS uses one contiguous queue representation, tombstones and reusable same-entry
merge scratch. Membership indices are republished after movement. One attachment
owner propagates `NO_BREAK`; insertion sorting searches first and splices once.
Context actions resolve against the current live sequence with the parent's
filtering policy, and child GPOS lookups stop at their first successful subtable.

Prepared runs retain **`u32 boundaries[n+1]`** and separate metadata. The sentinel
equals the prepared-input count, including empty input. Immutable sorted,
last-wins feature sets retain explicit zero and nonbinary values. Preparation
resolves shape/glyph configurations and carries exact font mapping in the existing
input record; execution does not compile configurations. Context execution uses
the destination and scratch pointers supplied for the current `ShapeBegin` cycle.

Fonts retain the 256-byte ASCII cmap and existing dense/sparse cmap layouts.
Iterators borrow canonical records in logical order; the benchmark's final
consumer projection remains explicit. This library is still isolated from the
editor's renderer.

### Historical simplification cutover: work and ownership

This section predates the caller-owned memory interface above. Allocation counts,
heap ownership and placement signatures below describe that earlier revision.

The implementation and acceptance ledger are in
[`docs/plans/shaping-simplification.md`](docs/plans/shaping-simplification.md).
These are operation/allocation counts, not timing improvements:

| Exercised work | Frozen owned control | Simplified |
|---|---:|---:|
| Predicate full-domain source visits, bundled font | 4,054,092 | 84,966 class decodes |
| Additional grouped-source work | — | 84,966 class-link writes; 1,273 coverage members |
| High-level cmap API queries, 139,392 output glyphs | 278,792 | 139,396 |
| Shape-config searches, 768 prepared runs | 768 | 8 |
| Glyph-config searches, same workload | 139,392 | 8 |
| Unicode decomposition queries, same single-font workload | 329,476 | 329,474 |
| First execution allocation requests, 96 runs | 197 | 9 |
| Sort splices, sorted/reverse/duplicate-key 130-slot inputs | 16,705 | 257 |
| Evaluated source-level link reads, same sorting cases | 84,109 | 18,317 |

Global symbol refinement, predicate hashing, dispatch emission and merge scratch
still do real work; they are not relabeled zero. The sort comparison complexity
is unchanged. A one-glyph native range at physical slot 8,192 performs one active
probe and no index build/allocation, exactly like the one-glyph standalone case.
After bounded warmup, 100 native/context/native cycles at that slot allocate
nothing; contextual rows still clear their necessary readable extent.

Memory is not uniformly smaller. The bundled Latin heap configuration retains
883,247 bytes; fixed placement advertises 1,468,343 bytes, and split-allocator
construction requests 2,351,590 bytes. The selected-root bound replaces the former
925-entry temporary bound with 198 eligible references. Canonical glyphs shrink
104→96 bytes, but prepared input grows 48→72 bytes and shared font facts add
resident storage. The exercised two-config high-level workload retains 11,195,952
bytes versus 8,314,593 in the frozen control, including fonts and arena high-water storage.
Repeated warmed states allocate nothing; newly encountered capacity requirements
can still grow the workspace. No overall speed or memory reduction is claimed.

The completion audit found and fixed an arena-growth failure that lost ownership
of earlier allocations. Public fail-each input/preparation/execution scenarios
preserved sticky errors and balanced destruction. At that revision,
`kbts_PlaceShapeConfig` gained a fifth `MemorySize` argument and rejected short
buffers before writes. The current API instead takes bounded region descriptors;
the upstream vendor remains unchanged.

Coverage now discovers normalization alternatives only when nominal mapping is
missing. The single-font decomposition count above removes the pre-audit doubling.
The real two-font fallback workload still performs 9,956 decomposition queries
versus 6,920 in the frozen control: necessary coverage certification is not free.
No overall normalization-work reduction is claimed for fallback-heavy input.

Final project checks and focused sanitized SSE2/AVX2 regressions pass. Both ISAs
pass the 555-case default and 64 KiB exact verifiers. Complete sanitized stress
exercises all 9,760 cases per ISA: 9,423 remain exact and 337 differ intentionally
after live-target/Indic-boundary fixes, plus five Arabic warmup differences.
The strict comparator is unchanged and reports failure on those differences;
the plan ledger explains every difference and retains complete tuple reports.
The rapid original TODO closure was not fully justified. Section 9.6 records the
subsequent 139-item audit, code fixes, newly supplied proofs and source-matched
final replays rather than treating delayed bookkeeping as acceptance evidence.

### Historical stable-slot and static-admission correction

The measurements and verification in this section predate the simplification
cutover above; its older sizes and timing gate are retained as historical evidence.

The compact-array cutover violated the no-suffix-copy requirement. It is replaced
by one canonical stable-slot array, tombstones and logical slot links—not another
glyph representation or converter. Deletion, insertion, movement, reversal and
range restoration leave surviving records in place. Growth alone can relocate
the backing allocation; references remain stable.

Lookup-to-stage relationships are now configuration-owned default/possible
admission rows. Actual run occurrence bits stay dynamic. Review additionally
identified and corrected:

- wholly disabled stages still entering execution;
- repeated font-static maximum-window scans during scratchpad creation;
- unused symbol-position allocations on native-only plans;
- missing alignment and original-pointer ownership in derived allocation paths;
- admission rows using the lookup-count upper bound instead of actual merged stages.

The bundled plan has 38 merged stages, so hot admission rows use one `u64` per
glyph per policy, rather than three. Caller-owned configuration sizing still
reserves a conservative upper bound: **1,277,128 bytes** for the bundled config.
This is a space-for-cold-precomputation tradeoff, not a claim of reduced memory.
Canonical glyphs remain 104 bytes, plus 12 bytes of links/free-slot metadata per
reserved slot. Font-cache window metadata adds eight bytes to the packed cache.

Verification includes:

- `just check` under ASan/UBSan;
- 555 exact owned/upstream cases on SSE2 (sanitized) and production AVX2;
- 9,760 exact sanitized stress cases across eight fonts and five feature modes;
- full nonterminal-range deletion/refill, reordered bounded contextual matching,
  live glyph ID zero, survivor-address stability and unaligned allocator regressions;
- real-font normalization at a full 64-slot capacity: combining-mark decomposition
  and consecutive Thai sara-am grow to 128 slots and match upstream exactly.

HarfBuzz differences remain diagnostic, not a conformance claim. Review found no
remaining canonical-representation or static-ownership violation in the inspected
paths; it does not certify unrelated inherited shaping behavior.

**That historical strict performance gate was not met.** Final paired measurements cover
17 cases, three rotated control-order repetitions and five batches on CPU 0:
153 uninstrumented AVX2 runs. Values are medians of three batch-mean medians,
in microseconds for input + core + output. Each batch uses 1,000 iterations,
except 64 KiB and 4 KiB Arabic/Devanagari inputs, which use 100. All per-case
checksums match across controls and every measured warm allocation count is zero.

| Workload | Pre-span control | Before these fixes | Stable slots + cold admission | Change vs before |
|---|---:|---:|---:|---:|
| Code, 64 B | 8.283 | 7.225 | 7.338 | +1.6% |
| Code, 4 KiB | 447.110 | 396.843 | 405.214 | +2.1% |
| Code, 64 KiB | 7822.006 | 6795.379 | 7051.514 | +3.8% |
| Prose, 64 B | 2.629 | 2.921 | 2.959 | +1.3% |
| Prose, 4 KiB | 185.544 | 197.446 | 200.354 | +1.5% |
| Prose, 64 KiB | 3030.595 | 3255.542 | 3288.413 | +1.0% |
| Arabic, 64 B | 4.175 | 5.899 | 6.023 | +2.1% |
| Arabic, 4 KiB | 4797.991 | 1414.240 | 1446.858 | +2.3% |
| Devanagari, 64 B | 8.546 | 11.636 | 11.038 | -5.1% |
| Devanagari, 4 KiB | 823.497 | 941.498 | 783.502 | -16.8% |
| Thai, 64 B | 0.807 | 1.348 | 1.410 | +4.6% |
| Thai, 4 KiB | 171.895 | 67.370 | 71.298 | +5.8% |
| Combining-heavy, 64 B | 2.510 | 3.205 | 3.031 | -5.4% |
| Combining-heavy, 4 KiB | 994.913 | 1719.720 | 305.581 | -82.2% |
| Code, features off, 4 KiB | 160.365 | 176.401 | 175.032 | -0.8% |
| Noto prose, 4 KiB | 376.940 | 954.150 | 330.828 | -65.3% |
| Noto prose, features off, 4 KiB | 317.670 | 317.444 | 335.409 | +5.7% |

Code/prose use bundled JetBrains Mono; script/combining/Noto prose use the
corresponding Noto families. Features are default unless marked off.
Structural-deletion workloads improve substantially, but other paths regress;
neither these results nor the older pre-span comparison satisfy a universal 2%
no-regression threshold. A contiguous-slot traversal experiment was measured and
removed because it made code slower. No experimental execution mode remains.

Local raw evidence is retained in
`/tmp/rwmd-stable-slots-final-measurements.json`,
`/tmp/rwmd-stable-slots-final-stress.jsonl` and
`/tmp/rwmd-stable-slots-normalization-smoke.json`.

### Historical canonical cutover measurements and acceptance

**Performance acceptance remains blocked.** The translation-free core is verified,
and every measured workload improves over the task-entry executable. The 4 KiB
code workload also improves over the preserved pre-span control. However, prose,
short complex-script inputs and combining-heavy input still exceed the approved
2% pre-span regression threshold. These are not accepted as universal speed wins.

Three alternating-order repetitions used Clang 22.1.8, `-O3 -DNDEBUG -mavx2`,
CPU 0, default features, 16 changing input variants and five batches. Values below
are means of the three uninstrumented batch medians, in microseconds. Runs used
1,000 iterations per batch, except 64 KiB inputs and 4 KiB Arabic/Devanagari,
which used 100. Byte sizes are exact for ASCII and maxima for UTF-8 patterns.
No repetitions were discarded. All matched output checksums agree; every fixed
workload requested zero warm allocations.

| Workload | Pre-span total | Task-entry total | Canonical total | vs task entry | vs pre-span |
|---|---:|---:|---:|---:|---:|
| Code, 64 B | 8.174 | 9.165 | 7.190 | -21.6% | -12.0% |
| Code, 4 KiB | 439.091 | 522.057 | 389.746 | -25.3% | -11.2% |
| Code, 64 KiB | 7821.637 | 9568.089 | 6797.235 | -29.0% | -13.1% |
| Prose, 64 B | 2.657 | 4.307 | 2.922 | -32.2% | +10.0% |
| Prose, 4 KiB | 184.951 | 324.388 | 199.237 | -38.6% | +7.7% |
| Prose, 64 KiB | 3041.024 | 5262.352 | 3274.596 | -37.8% | +7.7% |
| Arabic, 64 B | 4.237 | 9.822 | 5.868 | -40.3% | +38.5% |
| Arabic, 4 KiB | 4773.884 | 4767.218 | 1405.718 | -70.5% | -70.6% |
| Devanagari, 64 B | 8.585 | 20.927 | 11.732 | -43.9% | +36.7% |
| Devanagari, 4 KiB | 826.121 | 1774.545 | 929.878 | -47.6% | +12.6% |
| Thai, 64 B | 0.808 | 1.380 | 1.332 | -3.5% | +64.9% |
| Thai, 4 KiB | 172.055 | 298.179 | 67.662 | -77.3% | -60.7% |
| Combining-heavy, 64 B | 2.524 | 4.544 | 3.167 | -30.3% | +25.5% |
| Combining-heavy, 4 KiB | 993.279 | 2434.185 | 1742.730 | -28.4% | +75.5% |

Code/prose use the bundled JetBrains Mono font. Script cases use the corresponding
Noto Sans Arabic, Devanagari and Thai fonts with `Arab/ar/rtl`, `Deva/hi/ltr` and
`Thai/th/ltr`. Combining input uses Noto Sans, `Latn/en/ltr`, repeating
`Ạ́ ā́ ë ô ñ q̣̇ 1⁄2 `. Full commands, font hashes, all repetitions and output
checksums are retained in `/tmp/rwmd-final-measurements.json`.

The 4 KiB code breakdown, with checksum `fcc4f084efe04ef2`:

| Phase | Pre-span | Task entry | Canonical |
|---|---:|---:|---:|
| Input | 61.137 µs | 67.384 µs | 49.529 µs |
| Core | 370.717 µs | 448.754 µs | 334.453 µs |
| Output projection | 7.242 µs | 5.904 µs | 5.764 µs |
| Total | 439.091 µs | 522.057 µs | 389.746 µs |
| Setup | 7.863 ms | 8.272 ms | 7.674 ms |

Phase medians need not sum to the independently measured total median.
Individual total medians were 437.526/439.757/439.991 µs pre-span,
521.364/520.536/524.272 µs at task entry and
390.361/390.985/387.893 µs for the canonical core.

Whole-process user-only PMU counts, divided by 5,000 measured shapes, include
setup, warm-up, input and output; they are **not scoped core counts**. Execution
events have three repetitions; memory events have two. Every event reported
100% running time, without multiplexing.

| Event per measured shape | Pre-span | Task entry | Canonical |
|---|---:|---:|---:|
| Instructions | 8.786 M | 10.860 M | 8.840 M |
| Cycles | 2.363 M | 2.792 M | 2.109 M |
| Branches | 1.515 M | 1.780 M | 1.421 M |
| Branch misses | 1,244 | 2,452 | 1,564 |
| L1D loads | 2.832 M | 3.336 M | 2.799 M |
| L1D load misses | 145,172 | 157,048 | 111,635 |

A separate fixed 4,096-codepoint ASCII-operator witness counted source-level
identity-helper entries and stream work, not hardware loads. Core entries fell
from 66,723 to zero; input still has 4,096 identity-helper entries and 4,096 cmap
queries. Core cmap queries were zero in both versions. Per shape:

| Scoped core work | Task entry | Canonical |
|---|---:|---:|
| Row materialization | 49,152 B | 0 B |
| Span initialization | 65,536 B | 0 B |
| Row emission | 49,152 B | 0 B |
| Pending replacements | 28,632 B | 0 B |
| Bitmap clears | 45,568 B | 8,208 B |
| GSUB stream scans | 5 | 1 |

This witness had no canonical structural moves or index rebuilds. Its scan count
does not include every normalization/GPOS pass. Live requested heap payload,
excluding the raw font file, fell from 2,985,196 to 2,772,022 bytes. Records remain
104 bytes. These figures exclude stack-resident font structs, allocator bookkeeping
and RSS effects. Diagnostic probes used `-O1 -DNDEBUG -mavx2`; paired core observer
overhead averaged +3.6% at entry and +1.5% after the cutover, with variation.
None of these counters is present in the production timing executable.

The decoder experiment retained scalar decoding: ASCII measured about
1.26–1.39 ns/byte versus 1.88 ns/byte for the 16-byte classification LUT; mixed and
Arabic inputs also favored scalar decoding. Valid scalars, all one/two-byte inputs
and two million malformed longer samples agreed.

The dense-map experiment used
[Noto Sans CJK SC](https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf).
Its raw selected cmap is 190,084 bytes; the accepted 32-codepoint page cache adds
166,768 resident bytes, rather than replacing the raw table. The isolated optimized
4,096-glyph pipeline measured 171.8–173.1 µs with ranges versus 132.4–132.8 µs with
pages. Every valid Unicode scalar matched the raw lookup. Sparse Noto Sans retained
ranges: a 23,552-byte page prototype produced only about 0.1% pipeline improvement
while adding setup cost. Short/large Lao and CJK source-control probes also matched
checksums, but used `-O1` and are diagnostic, not substitutes for the production
acceptance table; the preserved pre-span CLI does not expose those scripts.

Final verification: `just check`; 555 exact owned/upstream cases on each of SSE2
and AVX2; all 9,760 multi-font/script stress cases under SSE2 ASan/UBSan; and
sanitized high-level run, covered Hangul/Lao, cluster-range reuse/error-unwind and
dense-cmap lifetime smokes. An additional permanent regression covers
shrink/grow and suffix identity/source preservation. The high-level script smoke
matches entry behavior except the intentional grapheme-boundary correction;
inherited Hangul/Lao source-map behavior is not claimed fixed. HarfBuzz differences
remain diagnostics. Variable-length stress can still allocate on later growth;
zero warm allocation claims above apply to the listed fixed workloads only.

No second shaper, converter fallback or experimental runtime mode remains.
Further acceptance requires resolving the measured pre-span regressions, not
relabelling task-entry gains as a full pass.

### Historical span-scheduler measurements

The following measurements are historical, before this canonical cutover.

Three final alternating controls used AVX2, CPU 0, 4 KiB code inputs, 16 variants,
default features and 160 iterations × 5 batches, instrumentation off:

| Mean of three batch-median results | Previous scheduler | Explicit spans |
|---|---:|---:|
| Shaping core | 395.5 µs | 432.4 µs |
| Input + core + output | 465.0 µs | 503.1 µs |
| Setup | 8.19 ms | 8.20 ms |

This is **9.3% slower in the core**, not a speed improvement. Individual core
results were 391.1–399.4 versus 418.4–439.4 µs. Every checksum matched
(`2a0ac2660638dc00`), and warm allocation requests remained zero. The implicit
pass-through encoding is not implemented or measured.

Verification passes 555 exact owned/upstream cases on each of SSE2 and AVX2,
9,760 multi-font/script cases, sanitized stream regressions, and real-font
allocation-failure, growth, fixed-arena, warm-reuse and empty-table smoke checks.

At that historical cutover, rule eligibility used a configuration-owned SoA column of `u32` requirements
in sequential lookup order, padded to 32 entries, plus a bitmap identifying
nonzero requirements. Candidate admission broadcasts one glyph's flags and
compares eight rule masks per AVX2 batch or four per SSE2 batch using
`(flags & required) == required`; other architectures use a scalar path.
Unconstrained candidates bypass comparison. Requirements include the configured
normal/USE filtering policy, while live admission checks preserve ordered writes,
feature overrides and nonbinary values. Eligibility itself changes only rule columns.
The bundled configuration adds **808 bytes**, with no additional scratch arrays.

Three longer alternating AVX2 controls on CPU 0 used the same 4 KiB code workload
and 16 variants, with default features and 1,000 iterations × 9 batches.
Mean batch-median core time changed from **414.3 to 425.5 µs (+2.7%)**; total
input/core/output changed from **482.1 to 494.0 µs**. Individual core results were
412.6–416.1 versus 423.1–429.1 µs. Checksums matched (`6e9a49b6b80ddaf2`) and warm
allocation requests remained zero. This representation change is not a speed win.
Shorter 160 × 5 controls also showed no aggregate gain: optional-feature code
583.6→604.7 µs and Noto Sans Arabic 5,327.6→5,426.6 µs, with more run-to-run variation.

An uninstrumented `perf record -e cycles:u -F 997 --call-graph dwarf,8192` run
collected 2,613 samples over 5,000 measured shapes. Whole-process self attribution
was 45.6% in `kbts__ExecuteOp` (including inlined stage execution/gathering),
19.6% in compiled candidate selection, 11.8% in candidate admission and stage-bitmap
seeding, and 2.3% in span emission. These are sampling shares, not additive nested
RDTSCP timings or isolated flag-comparison costs. AVX2 and SSE2 disassembly confirms
the explicit broadcast/AND/equality/movemask sequence.

That eligibility cutover passed `just check`, both SIMD eligibility regressions,
555 exact owned/upstream cases per ISA (AVX2 sanitized), and all 9,760 stress cases.

### Historical compact-array cutover and ExecuteOp profile

At that historical cutover, glyph storage removed next/previous links and pooled
glyph nodes. `kb_glyph_storage.inc` used a geometrically grown dense array, an identity-to-position
map, and a recycled-identity stack. Insertions and deletions compact with `memmove`;
script reordering uses whole-record swaps, range moves and reversal. Cluster
processing selects an index range instead of detaching a linked sublist.
Nested GSUB frames, GPOS attachments, buckets and gathered windows carry `u32`
identities. Borrowed record pointers expire at the next storage mutation.
Fixed buffers never fall back to allocation; full clears reuse storage in O(1).

On x64, glyph records shrink from **120 to 104 bytes** and hot GSUB rows from
**16 to 12 bytes**. The position map and recycled-ID stack add eight bytes per
reserved glyph slot. Setup/warm-up allocation requests for the bundled 4 KiB
workload fall from 194 to 77, but cumulative requested bytes rise from 4,022,608
to 4,375,352 because geometric growth copies and replaces backing allocations.
These requested-byte totals are not resident-memory measurements.

Three alternating uninstrumented AVX2 controls used CPU 0, the same 4 KiB code
inputs and 16 variants, default features, and 1,000 iterations × 9 batches:

| Mean of three batch medians | Linked storage | Compact array |
|---|---:|---:|
| Input preparation | 68.83 µs | 75.97 µs |
| Shaping core | 464.31 µs | 496.04 µs |
| Output iteration | 7.61 µs | 5.99 µs |
| Input + core + output | 540.74 µs | 577.87 µs |

The core is **6.8% slower**, not faster; paired increases range from 2.9% to
12.7%. Checksums match (`6e9a49b6b80ddaf2`), with zero warm allocation requests.
The compact representation removes links but has not removed identity-map
resolution from every mutation-capable scan.

An uninstrumented cycles sample collected 2,900 samples with no loss:
47.5% of whole-process self samples land in `kbts__ExecuteOp`, 18.7% in compiled
candidate selection, and 10.8% in candidate admission. `ExecuteOp` includes
inlined constant-stage execution, context gathering, eligibility checks and
native-stage reconciliation; its symbol share is not switch-dispatch overhead.
Sampled admission work concentrates on candidate enumeration and stage-bitmap
writes, not just the SIMD flag comparisons.

A separate core-only RDTSCP profile reports, per shape: GSUB **398.9 µs**
(81.6% of instrumented shaping time), normalization **59.9 µs**, GPOS metrics
**17.3 µs**, and post-GPOS fixup **12.3 µs**. GSUB contains **300.7 µs** in lookup
batches and **98.2 µs** in setup/emission/residual work; these are subdivisions,
not additional totals. Lookup 404 is the largest batch at **47.6 µs** per shape.
Detailed counters over 48 shapes retain the pre-cutover work totals:
1,218,522 rule visits, 3,906,390 predicate words, 24,135 sequence matches and
28,410 substitutions. The compact LUT remains 56,151 bytes, 32,644 hot.

That profile identified GSUB admission/stage bookkeeping and normalization's
identity-resolving scans and recomposition-parent work as targets, rather than
the dispatcher. The canonical cutover above addresses those paths.
The cutover passes `just check`, sanitized SSE2/AVX2 context regressions,
555 exact owned/upstream cases per ISA, and all 9,760 multi-font cases under
ASan/UBSan, including geometry and source maps. Storage regressions cover
growth with aliased input, compaction/reordering identities, continuation and
attachment preservation, allocation failure and fixed-buffer exhaustion.
An additional sanitized differential smoke run passes 648 Khmer, Myanmar,
Tibetan and Javanese/USE cases using both dynamic and unaligned fixed storage.
A further 168 Hangul cases match the missing-glyph fallback; the available font
lacks their Jamo/syllables, so those cases do not verify covered Hangul composition.

Profiling is compile-time optional: `shaping-bench` contains no RDTSCP scopes.
`shaping-profile` uses serialized RDTSCP, nested inclusive/exclusive **TSC ticks**,
CPU-migration checks, calibrated tick frequency and measured observer overhead.
TSC ticks are not core clock cycles. Detailed per-glyph/lookup scopes substantially
perturb execution; do not use their wall times as production benchmarks.

Detailed profiles report compiled candidate visits, predicate checks (including
padding), filtered context-window gathering, and contextual-format counts. Raw
contextual rules are decoded during font compilation, not warm matching.
Setup and measured profiles are separate, with counters reset after warm-up.
Tiny functions can be dominated by observer overhead; prefer the core-only lookup
timers and an uninstrumented control for the main breakdown.

`shaping-core` enables level 3 with `KB_PROFILE_CORE_ONLY`: tiny helper timers and
work/context bookkeeping compile out before TLS access. It times complete nonempty
GSUB/GPOS lookup batches, with nested sort/application parts, and lists every
nonzero lookup by raw OpenType index/type, ordered by total ticks. Glyph matching,
mutation and rebucketing remain combined inside application. Queue release and
stage-loop bookkeeping are reported as residual time, not assigned to lookups.

A pre-compaction 4 KiB bundled-font core-only run attributed approximately 275 µs to GSUB
lookup application, 44 µs to normalization, 25 µs to sorting, 21 µs to initial
bucketing, and 13 µs to metrics/final positioning. Lookup #131 application took
10.5 µs per shape. These are instrumented figures: its 464 µs total was about
7–8% above the paired uninstrumented controls, with the same checksum and no CPU
migrations. The lookup timers use direct RDTSCP, not statistical sampling.
`shaping-profile --profile core` retains compiled micro-hooks; use `shaping-core`
for the lower-overhead path.
The compact-cache core-only run measured 281 µs of GSUB application and 25 µs
sorting, with a 467 µs instrumented total. Do not compare instrumented totals to
production totals without accounting for observer overhead.

A surrounding-code pass kept the compact LUT and selector unchanged, splitting
the ordinary no-filter check into an inline `kbts__SkipGlyph` wrapper around the
unchanged slow filter. Three paired AVX2 4 KiB code runs (200 iterations × seven
batches, CPU 0) measured core times of 381.531/378.248, 380.900/378.128 and
379.759/376.586 µs before/after: a modest 0.7–0.9% improvement. Checksums matched
and warm allocation requests remained zero.
The final core-only profile attributed 279 µs to GSUB application, 44 µs to
normalization, 24.5 µs to sorting, 20.7 µs to initial bucketing and 13.4 µs to
metrics/final positioning, within a 395 µs instrumented core.
Separate pinned, user-only PMU groups scoped to warm `kbts_ShapeDirect` counted
6.99 million retired instructions and 1.86–1.89 million cycles per shape, about
0.045% branch mispredictions, and 23% of cycles with no retirement while waiting
on a load. The L2 data-request group missed less than 1% of requests. These are
hardware-event counts, not an exact division into removable bottlenecks.
Fine-grained RDTSCP scopes inflated core time to 1–2 ms; they were rejected for
production cost percentages. Matching, mutation and rebucketing therefore remain
combined in the trustworthy broad timing breakdown.

Font mapping/hash-prefaulting, font/LUT construction and warm shaping are reported
separately. The load measurement is not a cold-disk benchmark. Disk LUT persistence
and exhaustive font/sequence conformance testing remain deferred.

Font constructors compile automatically and own their blob storage; native input
is copied. Manual aligned-native `kbts_LoadFont` borrows caller storage;
raw-font `kbts_PlaceBlob` produces complete serialized bytes before compilation.
Manual callers must call `kbts_CompileFont` before creating shape configs.
`kbts_FreeFont` releases compiled ownership without freeing a borrowed blob.
Focused sanitized fixtures cover matching/priority, filtering and live actions,
feature/config lifetime, script transformations, placement, odd allocators and
allocation failures. Current differential results, intentional upstream
differences and complete-corpus evidence are recorded in the simplification
ledger; the historical exact-match reports above do not describe every bug fix.

KB's upstream nonbinary-feature size helper underallocates when one override
expands into multiple lookup entries. The fork fixes that bound. For `alternate`
verification only, the immutable reference adapter uses KB's caller-owned
placement API with sufficient space instead of exercising that known overflow.
The upstream warning against untrusted font files still applies.

## Boundaries

The font cache still contains printable ASCII plus `.notdef`. Unicode source is
stored, navigated and saved losslessly, but full shaping/fallback and combining-
mark appearance are not implemented. There is no font hinting or ClearType RGB
subpixel AA. Fractional grayscale edge coverage is intentional.

This is a rendered Markdown source editor, not a separate rich-text schema.
Editing source ranges can alter hidden markup. Visual lines currently wrap at
grapheme boundaries rather than using a full typographic word-breaking engine.

Wayland uses xkbcommon/compose for text; text-input/IME preedit/candidate protocols
are not implemented. X11 accepts committed UTF-8 from available XIM. Clipboard
support is CLIPBOARD, not PRIMARY or drag-and-drop. Paste completion is ordered
before subsequent queued typing/save commands without blocking rendering.

A document currently must fit within INT32_MAX bytes. A conservative parser mirror
means ordinary text edits can still move its suffix in memory; this is separate
from the deferred edit buffer. Initial parsing and structural invalidation may
scan the document, but visible layout does not require global height measurement.

The workstation's Vulkan loader reports duplicate installed layer manifests.
Earlier sanitizer runs also identified 183 bytes in unloaded NVIDIA driver
shutdown stacks; no blanket suppression was added. The existing `lsan.supp`
contains targeted external-library suppressions for diagnostic use.

See `IMPLEMENTATION.md` for interfaces and ownership, and
`docs/plans/edit-buffer-render-window.html` for the approved design. Reference
Slug shaders and licenses remain in `examples/Slug/`.

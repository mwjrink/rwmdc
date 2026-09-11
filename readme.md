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
HarfBuzz development files are required only for these benchmark commands:

```sh
just shaping-bench avx2 --size 4096 --features default
just shaping-bench sse2 --size 4096 --features default
just shaping-bench avx2 --features explicit
just shaping-bench avx2 --font /path/to/font.ttf --corpus prose
just shaping-profile avx2 --engine owned --profile coarse
just shaping-profile avx2 --engine owned --profile detail --size 1024
just shaping-verify avx2
SHAPING_SANITIZE=1 just shaping-verify avx2
```

The two KB adapters use identical `-O3 -march=x86-64` flags; AVX2 adds only
`-mavx2`. No AVX-512 requirement is introduced. The installed HarfBuzz library
retains its own build and runtime dispatch. Benchmarks pin a permitted CPU, reuse
objects and buffers, rotate engine order, and shape sixteen changing-input
variants. Input, shaping, output, checksums and allocator calls are reported.
Percentiles describe batch means, not individual input latency.

Default means no redundant feature overrides. `off` disables `calt/liga/clig`;
`explicit` enables them. `optional` enables `frac/ss01/cv01`, while `alternate`
requests `aalt=2`. Verification compares the owned/upstream glyph IDs, byte source
offsets, advances and offsets exactly. HarfBuzz differences remain diagnostic,
including the existing semicolon/Greek-question-mark discrepancy.

The fork adds coverage-aware feature overrides, a generated ASCII property LUT,
direct initialization of pooled glyphs, and stable contiguous bucket merging with
branchless key selection. Clang emitted SSE2/AVX2 for the ordered-bucket scan and
`cmov` for merge selection without explicit SIMD intrinsics.

Observed 4 KiB code-corpus batch medians on this workstation, instrumentation off:

| Font / mode | Upstream KB | Owned KB |
|---|---:|---:|
| Bundled JetBrains Mono, AVX2 defaults | 596 µs | 520 µs |
| Bundled JetBrains Mono, SSE2 defaults | 585 µs | 533 µs |
| Bundled JetBrains Mono, AVX2 explicit enables | 12,334 µs | 553 µs |
| Noto Serif, AVX2 defaults | 357 µs | 274 µs |

The default-path improvement is modest compared with the override-path fix;
this does not establish a 10 µs shaping bound. Warm runs made no allocator
requests. The additional applicability bitmap and reusable sort workspace trade
some per-font memory for reduced work.

Profiling is compile-time optional: `shaping-bench` contains no RDTSCP scopes.
`shaping-profile` uses serialized RDTSCP, nested inclusive/exclusive **TSC ticks**,
CPU-migration checks, calibrated tick frequency and measured observer overhead.
TSC ticks are not core clock cycles. Detailed per-glyph/lookup scopes substantially
perturb execution; do not use their wall times as production benchmarks.

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

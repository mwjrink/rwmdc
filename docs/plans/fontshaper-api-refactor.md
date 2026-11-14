# Fontshaper API and execution refactor

## Goal

Make the common operation **“here is a complete UTF-8 block; shape it”**.
The library owns its setup choreography and returns completed results. The
application supplies ownership, fonts, limits and per-call options—not a recipe
for assembling the shaper's internal subsystems.

This is an API/execution refactor, **not an application wrapper around the old
public calls**. Follow the lifecycle precedent in `input_linux.h` and the render
context/state constructors. Preserve Unicode behavior and the setup optimizations
already implemented in `fontshaper.h`.

Related measurements: [font-shaping-optimizations.md](font-shaping-optimizations.md).
The ASCII spike remains experimental; integrating it is not a prerequisite.

## Current ownership: caller-supplied storage (API version 5)

This supersedes the allocator-based ownership implemented during steps 2–3.
There is only one normal creation path; the old allocator constructor/type and
library destructor have been removed, not retained alongside it.

```c
fontshaper_un bytes = fontshaper_SizeOfState(&info);
void *memory = arena_alloc(arena, bytes); // Caller chooses allocation policy.
fontshaper_state *state = fontshaper_CreateState(memory, bytes, &info);
// Shape/reuse normally. Reclaim memory through the caller's arena/heap when done.
```

- The size query uses compiled font tables plus the descriptor's limits/cache
  budget. It is deterministic, but not a constant-expression `sizeof` macro.
- The query includes padding for any base alignment. Creation aligns the state
  and partitions all glyph/scratch/cache storage inside that one block.
- No public allocator callbacks, stored allocator, backing-release callback,
  ownership pointer/size or library destroy operation. Only the internal storage
  pointers/bounds needed to reuse the state are retained.
- Query returns zero for invalid configuration/overflow. Creation returns NULL
  for invalid configuration, NULL memory or insufficient size, without touching
  the destination on failure. Descriptor/font list/font data must not overlap it.
- The caller owns backing lifetime. Keep it live and unmodified while state or
  results are used; individual free, arena reset, pooling and reinitialization
  are caller policy. Font pointers are copied, compiled fonts remain borrowed.
- Creation revalidates/recalculates the bound before writing. Thus benchmark
  setup now includes query + caller allocation + creation, rather than just the
  earlier one-allocation constructor. It is startup work, never shaping work.

`tests/fontshaper_owned_context_test.c` now tests caller-owned storage despite
its historical filename: deterministic queries, invalid/overflow descriptors,
undersized/invalid creation with untouched destination, exact-size blocks at
every base alignment, guard bytes through shaping, copied font lists and reuse
of the same block without destruction. Existing multilingual/options/failure/
passive-reader checks are retained. `--managed`/`--batch` benchmark modes use
caller-owned blocks; historical allocator support exists only in the benchmark's
pre-v4 oracle branch. Output field `backing` replaces the old `owned` byte count.

Validation: caller-storage, retained-range and context regressions pass GCC and
Clang ASan/UBSan. All 64 baseline fingerprints and the Unicode/manual-run replay
match; persistent-cache usage is stable in each timed batch. Full results:
`/tmp/fontshaper-caller-storage-results.json`. No claim of new Unicode conformance
or a performance improvement is made for this ownership change.

The count-returning shape/flat whole-output traversal discussed subsequently is
a separate consumer-interface change; this patch changes storage ownership only.
Editor migration remains deferred at the user's request. The historical step
measurements below describe their original APIs, not a second supported owner.

### Naming (version 5)

There is one mutable **state**, not a context/state pair. The opaque type is
`fontshaper_state`, its creation descriptor is `fontshaper_state_info`, flags are
`fontshaper_state_flags`/`FONTSHAPER_STATE_FLAG_*`, and API discovery uses
`FONTSHAPER_STATE_API_VERSION`. Public arguments/iterator state pointers use
`State`. Expert bookkeeping-only helpers are `SizeOfStateHeader`/
`PlaceStateHeader`; scratch/cache helpers use `State` as well. No old context
aliases remain in the production header. Historical-header aliases are confined
to test-oracle support, and historical test filenames remain unchanged.

## Agreed decisions

- One creation operation should establish a usable context/state. The caller
  must not separately place cache blocks, glyph storage and scratch or discover
  their initialization dependencies.
- Query the required bytes, then create in one caller-supplied block. The caller
  chooses its allocator/arena; the library never allocates/releases backing or
  retains allocator callbacks. Do not add a parallel allocator-owned constructor.
- Keep immutable font/prepared data distinct from mutable shaping work. A
  context/state split should represent actual lifetimes, not add empty objects.
- The primary entry point accepts a complete buffer and a byte length. It must
  not feed each character through the incremental public API.
- Incremental begin/append/end can remain for advanced use, sharing internal
  primitives with the whole-buffer path.
- Completion executes shaping. Traversal only reads completed results.
- Iterators are acceptable over the glyph-slot pool and its logical links. They
  should be thin advancement operations, not perform shaping or allocation.
- Do not require a flattened glyph copy merely to make the API look simpler.
  Preserve internal free slots, stable identities and links where useful.
- Per-call options do not persist as a feature stack into subsequent calls.
- Preserve source/cluster mappings, font identity, scripts, directions and
  word/grapheme/line-break metadata. No silent ASCII-only simplification.

## Public contract to work toward

Names below are a design sketch, not a finalized ABI.

```c
typedef struct ShapeOptions {
    fontshaper_direction direction; // DONT_KNOW: infer paragraph direction.
    fontshaper_language  language;  // DONT_KNOW: default language selection.

    const fontshaper_feature_override *features; // NULL/0: standard features.
    u32 feature_count;
} ShapeOptions;

// State was fully initialized during creation.
ShapeResult result = shape(state, text, byte_length, (ShapeOptions){0});
```

`text` is a byte range, not necessarily a null-terminated string. `{0}` selects
inferred direction, default language selection and standard features. Language
selection is not language detection. Fonts, allocation and capacity budgets
belong to creation/configuration, not `ShapeOptions`.

### Responsibilities

| Operation | Responsibility |
| --- | --- |
| Create context/state | Validate configuration; partition and initialize all storage inside the caller block |
| Whole-buffer `shape` | Prepare input in bulk, segment, shape all runs, finalize results and return status |
| Incremental Begin | Reset per-operation state, keeping allocations and reusable configs |
| Incremental append | Accept input and explicitly supported setting changes |
| Incremental End | Finish segmentation and execute all remaining shaping |
| Result traversal | Read stored run/glyph records and advance logical links only |

### Result and lifetime rules

- A result exposes status/counts and read-only access to retained runs/glyphs.
- All runs' output remains available after completion, until the next operation
  on that state or backing reclamation/reset. Iteration does not overwrite earlier
  runs, consume the result irreversibly, or depend on which run was read first.
- A glyph exposes its font identity, glyph ID, advances/offsets and unambiguous
  UTF-8 source mapping. Do not require the normal consumer to call
  `ShapeGetShapeCodepoint` for every glyph just to recover a byte offset.
- Preserve many-to-one/one-to-many cluster semantics; one source offset is not a
  claim that every glyph corresponds to exactly one source character.
- Run descriptors retain script, direction, paragraph direction and break flags.
  Boundary metadata remains accessible independently of glyph traversal.
- Positions remain in font units. Width-dependent wrapping, pixel scaling and
  final mixed-direction line layout are separate concerns in this refactor.
- Traversal does not allocate, execute GSUB/GPOS, mutate shaping state or uncover
  deferred shaping failures. Shaping errors are reported at completion.
- Define ordinary capacity failure/retry behavior explicitly. Do not silently
  truncate output or present partially shaped data as a successful result.
- Retaining unchanged paragraphs beyond the next call is an editor ownership/
  caching decision; shaping state is reusable workspace, not a document cache.

## Work ordering: combine changes that touch the same machinery

Use the following implementation batches rather than independently adding a
constructor wrapper, then a result wrapper, then another whole-buffer wrapper.
Within each batch, use reviewable edits and keep a runnable check. Benchmark
between batches, not only after the final migration.

### 1. Settle ownership and result storage together

- [x] Specify the creation descriptor: borrowed compiled fonts, input/glyph
  limits, persistent-cache budget and allocator/backing-storage ownership.
- [x] Decide whether shared context plus exclusive mutable state is useful in
  the actual callers. Keep lazy mutable caches state-local by default; do not
  introduce shared-cache locking to justify a new context abstraction.
- [x] Specify output lifetime and failure/retry behavior before changing code.
- [x] Select retained-output storage: keep completed run ranges in a slot pool,
  or retain separate output storage if the working engine requires it. Inspect
  the existing active-range machinery before introducing another representation.
- [x] Account for all retained paragraph output as well as intermediate glyph
  expansion. The old per-run capacity and memory bounds cannot simply be reused
  as a promise for arbitrarily many retained runs.
- [x] Establish a current output/performance baseline and keep it as an oracle
  while the execution boundary moves.

**Why together:** current `ShapeRun` both shapes and clears/reuses the preceding
run's glyph workspace. Eager completion is impossible without deciding where
those completed glyphs survive. This decision also determines constructor sizes.

#### Step 1 decisions and evidence

**Status: complete.** This step establishes the contract and verifies storage
feasibility; it does not introduce the new production API or move execution yet.

##### Creation and ownership

Use **one opaque reusable shaping state**, not a new shared context plus a
second mutable object. The existing compiled fonts already provide the useful
immutable/shared layer. Actual application callers use one `TTFont` workspace
sequentially for content spans, bullets, task markers and replacement glyphs;
they do not currently need shared mutable config caches or concurrent states.
Independent states may borrow the same compiled fonts later without new locking.

Use a size query and caller-storage constructor taking the following descriptor.
The original allocator policy was superseded by the ownership amendment above.

```c
typedef struct ShapeCreateInfo {
    fontshaper_font *const *fonts; // Existing stack order/fallback semantics.
    u32 font_count;
    u32 input_capacity;           // Decoded input codepoints, excluding sentinel.
    u32 glyph_capacity;           // Global simultaneous live slots; see below.
    fontshaper_un cache_bytes;    // Bounded persistent config storage; 0 disables.
} ShapeCreateInfo;
```

- Query the total bound, then pass one caller-owned block and its size to
  creation. Alignment slack is included. All internal partitions are initialized
  by the library; no allocator or ownership/release policy is retained.
- The caller allocates/releases backing, including arena reset or individual
  free. Storage must not be reclaimed while state/results remain in use.
- Copy the ordered font-pointer list into state. The descriptor and pointer
  array need survive only creation; each font object, native blob and compiled
  backing remain borrowed, immutable and at stable addresses while state is used.
  Loading/compiling the fonts remains separate from creating shaping workspace.
  In particular, create state after a returned-by-value `TTFont` reaches its
  stable location; do not capture a font-loader local's address.
- Require a nonempty valid compiled font stack and positive input/glyph limits,
  within existing engine/index limits. Check all size/alignment arithmetic.
  Query failure returns zero; creation failure returns NULL without writing.
- Fixed limits and font stack do not change on the normal API. Create a
  replacement state to change them; do not add grow/reconfigure calls now.
- Persistent configs and copied feature keys belong to this exclusive state.
  `cache_bytes == 0` uses per-operation config storage and its larger scratch
  bound; it does not disable shaping features. Cache budget is not a promise that
  every possible language/style combination will fit persistently.
- Construction reserves scratch for the configured fonts/capacities, including
  supported script/language choices, rather than assuming the default language
  bound covers every later `ShapeOptions.language`. Step 2 must establish this
  conservative font-wide bound; no caller re-query or new allocator call is
  permitted during shaping. Internal bounded-arena allocations remain allowed.
- Do not retain the superseded allocator-owned constructor alongside caller
  storage. Existing low-level placement helpers implement internal construction.

##### Retained storage and traversal

Keep **one canonical slot/link/free-list pool for the whole operation**. Completed
runs stay linked in its prefix; the next run is mapped/shaped in the active
suffix. Reuse the execution workspace between runs, not their glyph records.
No second flattened glyph representation or mandatory glyph-record copy is
required by this choice.

The feasibility check is `tests/fontshaper_retained_ranges_test.c`. It uses the
unchanged production `ShapeRun` driver but selects an empty writable suffix
before each run, so its existing clear operation leaves completed records alone.
It verifies matching legacy output and byte-for-byte stability of completed
canonical records after later runs, including reused free slots, Arabic, Indic
clustering, combining marks, controls, trailing empty runs and manual boundaries.
Both enabled/disabled `liga` settings, persistent/nonpersistent configs and
repeated operations are exercised.

Important implementation consequences for step 2:

- Store **first slot + final glyph count** in each run descriptor/iterator.
  Appending a later run changes the earlier tail's `Next` from END to the next
  run's first glyph. A saved END sentinel alone therefore does not bound a run.
  A remaining-count check plus link advance stays thin and permits independent,
  repeated traversal without modifying the storage's active range.
- Set run writable bounds directly. Do not wrap a run with
  `fontshaper__PushGlyphRange`: it asserts no range is active, and Indic shaping
  already uses that non-nestable mechanism for internal clusters/syllables.
- Publish first/count after shaping, since insertion, deletion and reordering
  can change the first glyph. Zero-glyph/terminal runs get count zero.
- Keep engine codepoint indices and stable attachment references intact. Finalize
  a separate public source-byte-offset field/view at completion; do not repurpose
  an index still needed by the engine or require per-glyph public lookup calls.
  Font identity can come from the run; no duplicate per-glyph font pointer is
  needed merely to support nested run/glyph traversal.
- The feasibility test's explicit count-bounded rereads are a check of this
  representation, not an application wrapper or the eventual public API.

##### Capacity and memory accounting

`glyph_capacity = G` now bounds **all simultaneously live canonical slots**:
completed runs' glyphs plus the current run's mapped input and temporary expansion.
Deleted slots are immediately reusable; final glyph count alone is insufficient
to predict the peak. `input_capacity = N` bounds decoded input, not bytes/glyphs.

The test proves the distinction: sixteen manually separated `fi ` spans succeed
with the old per-run capacity of eight, but correctly exhaust a single retained
pool of eight. Keeping earlier runs must not silently make the pool unbounded.
The new contract intentionally changes this capacity meaning; migration must
budget for the whole operation, not copy existing per-run limits blindly.

The backing size must include, with checked arithmetic/alignment:

1. State bookkeeping and copied font references.
2. Input records plus sentinel and per-operation feature snapshots.
3. Up to `N + 1` run descriptors, including empty terminal runs, and boundaries.
4. `G` canonical glyph records, links, free-slot indices and public source metadata.
5. Exactly the configured persistent cache budget.
6. A shared maximum construction/execution workspace sized for `G` and all
   supported font/script/language configurations. The engine currently requires
   scratchpad glyph capacity to cover the entire storage capacity, even while
   shaping only its active suffix.
7. When persistence is disabled, the per-operation config payloads/keys otherwise
   held in the cache.

Preserve the existing setup/config reuse, but recalculate bounds after changing
record layouts. Historical per-run scratch numbers are not new-constructor
memory promises. No per-run backing allocation, hidden growth or mandatory output
copy is required by the retained-pool design. This does not promise zero extra work:
source metadata finalization and run bookkeeping still have a cost to measure.

##### Lifetime and failures

- `shape`/incremental End return an explicit status and a completed result only on
  success. Results are state-owned read-only views, with no separate free.
- Input bytes and per-call option arrays need survive only the call; copy any
  feature keys retained in the cache. Results do not borrow the input string.
- A new shape/Begin invalidates the previous result immediately, including if
  the new operation subsequently fails. Destruction also invalidates it. Reads
  alone do not invalidate anything and may be repeated in any run order.
- Failed operations expose no successful partial result. Distinguish invalid
  arguments, input-capacity exhaustion, glyph-capacity exhaustion and cache
  exhaustion in public status, rather than requiring callers to infer the cause
  from a generic sticky out-of-memory flag. Unsupported/malformed compiled-font
  state remains a separate initialization/engine error.
- Ordinary operation failures do not permanently poison the state. The next
  Begin clears per-operation errors/storage. After a failed cache build, clear
  the state's whole config cache before the next operation: simpler and safer
  than rolling back partially linked entries. No transparent retry or mid-call
  eviction is required. A successful warm call retains its configs.
- Retrying smaller input can succeed on the same state. Retrying a cache-failing
  call may succeed after the cache reset if old entries caused exhaustion; an
  operation whose own configs exceed the budget needs a replacement state with
  a larger cache, or persistence disabled. Do not promise every retry succeeds.
- The new whole-buffer API uses unsigned byte lengths/source offsets consistent
  with the app's `u32` buffers, with explicit engine-limit checks internally.
  Preserve current malformed UTF-8 skipping/normalization behavior. There is a
  known source-mapping exception to legacy equivalence: plain `ShapeUtf8` does
  not advance generated source IDs over skipped invalid bytes. Whole-buffer
  source offsets must use actual positions in the original buffer, not preserve
  that incorrect offset calculation. Keep this correction explicit in step 3's
  tests; do not confuse it with a change in decoding policy.

##### Reproducible baseline

Run the production-only runner; it does not build or promote the ASCII spike:

```sh
python tests/fontshaper_api_baseline.py --output /tmp/fontshaper-api-baseline.json
python tests/fontshaper_api_baseline.py --check tests/data/fontshaper_api_baseline.json
clang -O2 -I src tests/fontshaper_retained_ranges_test.c -lm -o /tmp/fontshaper-ranges
/tmp/fontshaper-ranges
```

`tests/data/fontshaper_api_baseline.json` pins input strings, font/header/harness
hashes, compiler/flags, capacity/budget choices, complete-output fingerprints and
timings. The Unicode/manual-input replay additionally fingerprints font/mapping
identity, glyphs and all existing run/break metadata, including malformed input.
Fingerprints are regression oracles; intentional source-mapping changes above
need explicit normalization/review, not blindly replacing golden hashes.

The production header matches Git commit
`4c74e406b9a1ab1f2838e51784d0895caf4f74ef`, SHA-256
`895f73ec8af35158856cd2d3abfbadab86b4cd4d39b9cf7e7a172bbfa57c0aec`.
That committed source can regenerate detailed old output for a failing fingerprint.

Clang 23.1.4, `-O3 -march=native`, Ryzen 9 7950X3D, CPU 5; five warm
batches. All cases use input/glyph capacities 8,192 and an 8 MiB cache budget.
Default-feature warm medians:

| Font | 249-byte paragraph | 999-byte block | 4,014-byte block (16 paragraphs) |
| --- | ---: | ---: | ---: |
| Noto Sans | 24.18 µs | 95.14 µs | 400.37 µs |
| Inter | 31.21 µs | 122.93 µs | 504.43 µs |
| Noto Serif | 24.02 µs | 94.18 µs | 386.58 µs |
| JetBrains Mono | 21.38 µs | 80.37 µs | 346.73 µs |

These timings include **legacy UTF-8 preparation + deferred run shaping + flat
copy/source lookup + the existing simple English wrapper**, not pure engine time
or final Unicode layout. The 4K case is one block with paragraph separators,
not an extrapolation or sixteen separate API calls. The future API comparison
must keep equivalent output consumption/layout work when claiming a speedup.

The JSON separately records font loading/compilation, legacy test-fixture setup
and the first cold paragraph. Those are individual cold observations, not warm
medians or a prediction of the new constructor's cost. Setup currently allocates
and resizes two scratch buffers for the legacy lifetime fixture; only one is
used by each timed paragraph. Recorded scratch/storage sizes are allocation
bounds, not RSS or total font memory. At capacity 8,192, current scratch bounds
are 7.19/4.08/4.75/5.04 MiB (Sans/Inter/Serif/Mono); glyph storage is about
0.94 MiB separately, plus the 8 MiB cache budget.

Validation: legacy context regression and retained-range feasibility checks pass
with GCC and Clang ASan/UBSan. The baseline contains 64 timed/fingerprinted
font/input/feature cases, and every warm batch checks stable cache usage.

### 2. Refactor creation, eager execution and passive traversal as one batch

Touch the context/state structures, memory layout, constructors and run driver
once for this ownership change.

- [x] Move required cache/scratch/glyph-storage initialization into creation.
  Preserve bounded caller-owned memory and warm config reuse without public
  cache/scratch setup choreography.
- [x] Keep language/font/capacity-dependent sizing and invalidation internal.
  Either reserve for supported configurations or return an explicit capacity
  error when a change exceeds the state budget—never silently overrun storage.
- [x] Extract run execution from the public `ShapeRun` traversal operation.
- [x] Make completion execute all runs and retain their output using step 1's
  storage choice, preserving joining context and writable run boundaries.
- [x] Make run iteration enumerate stored descriptors; keep glyph iteration a
  thin logical-slot/link advance. No shaping or heap allocation while reading.
- [x] Finalize public source mappings and metadata during completion so readers
  do not reconstruct implementation-specific codepoint indices.
- [x] Return completion errors directly. Ordinary insufficient-capacity handling
  and state reuse must follow the contract chosen in step 1, rather than exposing
  undocumented sticky-error/reconstruction requirements.
- [x] Validate alignment, overflow, initialization failure and cleanup/arena
  lifetimes as part of the constructor change.

**Acceptance:** constructing a usable state requires no manual assembly of its
subsystems. After completion, every run can be read repeatedly with unchanged
output, no engine execution, no allocation and no deferred error reporting.

#### Step 2 implementation and checks

**Status: complete (historical API; allocator ownership superseded by version 4
above).** Implemented in `src/lib/fontshaper/fontshaper.h`:

- `fontshaper_CreateShapeContext(&context, &info, allocator)` returns status and
  partitions one aligned allocation; `fontshaper_DestroyShapeContext` releases
  it. The font list is copied, not its borrowed compiled fonts. No allocation
  callbacks during shaping or traversal.
- `fontshaper_ShapeBegin(context, direction, language)` is now the three-argument
  owned-state operation. The old buffer-taking operation is explicitly named
  `fontshaper_ShapeBeginWithMemory` for expert/test callers; it shares the same
  underlying reset/execution machinery, not a second shaping engine.
- Constructor bounds enumerate each Unicode script's selected native GSUB/GPOS
  language-tag union plus the default language systems. Script/Indic3/DFLT
  selection is shared with config construction. Unknown tags are covered by the
  default case; no sizing query or backing growth occurs in the hot path.
- `fontshaper_ShapeEnd` returns status after executing every run into retained
  slot ranges. `ShapeRun` is only a read cursor; `ShapeGetRun` supports indexed,
  repeatable reads. `ShapeRunCount`/`ShapeGlyphCount` describe successful completed
  output and return zero while building or after failure.
- Glyph iterators now contain storage, current slot and remaining count only;
  unused KB position-accumulator fields were removed. Earlier run iterators do
  not spill into later runs, including when slots were reused or reordered.
- `glyph->SourceIndex` exposes the resolved input source ID without a codepoint
  lookup. It reuses the normalization-only `Decomposition` storage after shaping,
  so canonical glyph size/slot stride did not grow. It must not be read as an
  output source ID during shaping or through the standalone direct engine.
  Custom-ID bits and engine codepoint indices remain separate/intact. The legacy
  skipped-invalid-byte offset correction remains explicitly deferred to step 3.
- Owned states report distinct input/glyph/cache capacity failures, hide partial
  results and recover at the next Begin. Failed cache construction resets the
  cache before reuse; successful configs remain warm. Feature stacks reset for
  owned calls, while expert placement retains its legacy between-pass behavior.
- Cache rebinding is rejected during input and invalidates completed expert
  results. Owned-state fonts/budgets cannot be rebound through expert setters.

Validation commands:

```sh
clang -O2 -I src tests/fontshaper_owned_context_test.c -lm -o /tmp/fontshaper-owned
/tmp/fontshaper-owned
python tests/fontshaper_api_baseline.py --managed \
  --check tests/data/fontshaper_api_baseline.json \
  --output /tmp/fontshaper-api-step2-results.json
```

Owned-context, retained-range and existing context regression tests pass with
GCC and Clang ASan/UBSan. Coverage includes single-allocation/free accounting,
misalignment and allocation/size failures, cached/noncached configs, Arabic/
Devanagari/mixed text, changing languages/features, indexed forward/reverse
rereads without state mutation, and input/glyph/cache error recovery. All 64
baseline output fingerprints and the full metadata replay still match; cache
usage remains stable in every warm batch. Test shims allow the pre-refactor
header/spike to keep compiling as oracles without changing the parked spike.

Performance is **not claimed as an optimization yet**. A full rerun had a
nonstationary outlier (one case's batches rose from about 6.4 to 13.2 µs), so the
following control rebuilt the pinned pre-step-2 header and current managed
header with the same benchmark source/flags. Three alternating runs per variant,
five warm batches each, CPU 5; median of 15 batch measurements per variant:

| Font / input / overrides | Old | New | Change |
| --- | ---: | ---: | ---: |
| Noto Sans / ordinary / default | 24.71 µs | 25.29 µs | +2.35% |
| Inter / ordinary / default | 31.70 µs | 32.58 µs | +2.76% |
| Inter / 4K / liga off | 518.66 µs | 537.86 µs | +3.70% |
| Noto Serif / combining / liga off | 6.13 µs | 6.26 µs | +2.18% |
| Noto Serif / 4K / default | 391.74 µs | 426.23 µs | +8.80% |
| JetBrains Mono / ordinary / default | 21.68 µs | 22.39 µs | +3.27% |
| JetBrains Mono / 4K / default | 352.31 µs | 369.22 µs | +4.80% |

Output fingerprints matched in every paired run. Timings still include source
resolution, flat copying and the same simple English wrapper. The new benchmark
reads the finalized source field rather than redundantly resolving it again.
The refactor adds validation/result bookkeeping and retains all runs; the
slot-addressed GSUB indexes can span the retained prefix. Their active-run
rebasing is a possible later profile-driven optimization, not a second engine
or a reason to abandon retained output. Do not attribute the entire measured
increase to that index without profiling. Step 3's batch preparation is pending.

Paired raw timings: `/tmp/fontshaper-step2-paired.json` (temporary). To rebuild
that control, extract the baseline commit's header and compile the existing
benchmark both ways:

```sh
git show 4c74e406b9a1ab1f2838e51784d0895caf4f74ef:src/lib/fontshaper/fontshaper.h \
  > /tmp/fontshaper-before-api-step2.h
clang -O3 -march=native -ffunction-sections -fdata-sections -DBENCH_CAPACITY=8192 \
  -I src '-DFONTSHAPER_TEST_HEADER="/tmp/fontshaper-before-api-step2.h"' \
  tests/fontshaper_ascii_spike_bench.c -Wl,--gc-sections -lm -o /tmp/shaper-old
clang -O3 -march=native -ffunction-sections -fdata-sections -DBENCH_CAPACITY=8192 \
  -I src -DFONTSHAPER_BENCH_MANAGED tests/fontshaper_ascii_spike_bench.c \
  -Wl,--gc-sections -lm -o /tmp/shaper-new
# For each font/input: taskset -c 5 /tmp/shaper-{old,new} FONT INPUT_FILE OVERRIDES
# OVERRIDES is 0 (default) or 1 (liga off/kern on); compare records, not timing lines.
```

At capacities 8,192 and an 8 MiB cache, one owned backing allocation bounds about
16.20/13.08/13.76/14.04 MiB for Sans/Inter/Serif/Mono, excluding font data. Glyph
storage remains the baseline 983,053-byte bound; scratch increases for retained
run descriptors/all-language coverage. These are allocation bounds, not RSS.
Individual constructor observations were roughly 0.10–0.13 ms, separate from
font loading/compilation and lazy first-pass config construction; the old fixture
allocated/resized two scratch buffers, so do not treat its setup time as an
apples-to-apples constructor comparison.

The editor/direct-engine callers are intentionally not migrated in this step.
Whole-buffer batching/options remain step 3; application migration is step 4.

### 3. Add the real whole-buffer path and per-call options together

Touch UTF-8 decoding, input population and feature preparation once.

- [x] Implement the complete-buffer operation as a first-class library path,
  using shared internal primitives—not a loop over public append functions.
- [x] Validate `text`/byte-length combinations, options and capacities once at
  the operation boundary. Use a byte-length type that does not require callers
  to manually adapt the existing signed-int UTF-8 interface.
- [x] Determine/reserve input storage in bulk. A byte count bounds codepoint
  count, not glyph expansion; do not equate those capacities.
- [x] Resolve/canonicalize feature overrides once, preserving values, ordering
  independence and last-wins duplicate semantics.
- [x] Decode directly into input storage with byte-source mappings; avoid
  repeated public-entry, feature-stack and manual-segmentation bookkeeping.
- [x] Avoid extra passes/copies unless required by sizing or correctness; do not
  promise a speedup merely because the public call sequence becomes shorter.
- [x] Invoke the common eager-completion machinery and return a completed result.
- [x] Keep incremental append support on the same underlying preparation and
  completion engine. Confine its custom user-ID and manual-run machinery to
  that advanced path instead of taxing the normal operation unnecessarily.
- [x] Ensure options apply only to this call. No need for application push/pop
  cleanup and no feature leakage into a later default call.

**Acceptance:** the normal call is `shape(state, text, byte_length, options)`;
there is no application string-builder sequence, extra source-ID lookup loop or
hidden post-call execution. Compare whole-buffer and incremental output.

#### Step 3 implementation and checks

**Status: complete.** Public API version 3 adds the normal operation:

```c
fontshaper_shape_options options = {0};
fontshaper_shape_error error = fontshaper_Shape(context, text, byte_length, options);
// On success, ShapeRunCount/ShapeGetRun and glyph iterators read completed output.
```

The existing state plus returned status is the result; there is no second result
allocation or mandatory flattened glyph copy. `byte_length` is `fontshaper_u32`;
NULL/0 is empty, NULL/nonzero is invalid, and embedded NUL is an input codepoint.
Every call invalidates the previous result, including argument/capacity failures.
Default options infer paragraph direction, select default language systems (not
language detection), and use standard features.

`fontshaper_Shape` reserves geometric input blocks once, including the sentinel,
using `min(byte_length, input_capacity)` as a conservative codepoint upper bound.
It then decodes once directly into those blocks through a block cursor. It does
not count/decode twice, call public per-character append functions, copy through
an application buffer, allocate backing memory, or confuse decoded-input capacity
with the independent whole-operation glyph/expansion capacity. Multibyte and
skipped-invalid input can have more bytes than the input-codepoint limit and
still succeed. The same streaming Unicode breaker, normalization-aware font
coverage/fallback, run preparation and eager End engine remain in use.

Feature canonicalization is shared with advanced incremental input and runs
once per whole-buffer call. It sorts effective tags, preserves signed/nonbinary
values and last-wins duplicates, interns transient values, and copies persistent
keys. Up to `FONTSHAPER_MAX_SIMULTANEOUS_FEATURES` distinct tags are supported;
more raw entries are allowed when duplicates reduce to that bound. Options do
not mutate a persistent feature stack or borrow caller arrays after return.

**Intentional mapping correction:** IDs in this path are actual original byte
positions, including skipped invalid bytes. For `80 61 80 62` the decoded inputs
are `a` at byte 1 and `b` at byte 3, not legacy plain-append IDs 0 and 1. Decoder
consumption/skipping and scalar-validation behavior are otherwise unchanged;
this does not introduce a stricter UTF-8 decoder or replacement-character policy.
Legacy append retains its documented mapping caveat. The regression oracle for
malformed bulk input uses advanced `ShapeUtf8WithUserId(..., SOURCE_INDEX)`,
which already accounts for all consumed bytes, rather than changing goldens to
hide the intentional difference.

`tests/fontshaper_owned_context_test.c` now compares glyphs, font identities,
source/engine indices, positions, run bounds/scripts/directions/flags, every
codepoint's complete break metadata and canonical feature values against
incremental execution. Covers cached/noncached multilingual/mixed input,
explicit/inferred direction, language changes, controls, embedded NUL, malformed
and nonscalar input, block boundaries through exact input capacity, capacity and
cache failures/retry, duplicate/reordered features, source/option array lifetimes,
poisoned scratch, and passive rereads. GCC and Clang ASan/UBSan pass, as do the
existing context and retained-range tests. These are regression checks, not
independent Unicode/OpenType conformance certification.

The production-only baseline runner's new `--batch` option exercises this API:

```sh
python tests/fontshaper_api_baseline.py --batch \
  --check tests/data/fontshaper_api_baseline.json \
  --output /tmp/fontshaper-api-step3-results.json
```

All 64 complete-output fingerprints match the original valid-input baseline;
the existing Unicode/manual-run replay remains unchanged, and every timed batch
checks stable cache usage. Original golden hashes were not replaced. `--managed`
still measures the incremental owned-state route, providing a direct control.
The ASCII spike remains parked and is not built by this runner.

Paired step-2 incremental versus step-3 whole-buffer measurements rebuilt both
headers using the same current C benchmark, compiler/flags and CPU 5. Three
alternating runs per variant, five warm batches each, median of 15 measurements:

| Font / input / overrides | Step 2 | Step 3 | Change |
| --- | ---: | ---: | ---: |
| Noto Sans / ordinary / default | 25.04 µs | 24.15 µs | −3.56% |
| Inter / ordinary / default | 31.97 µs | 31.01 µs | −3.02% |
| Inter / 4K / liga off | 524.83 µs | 509.10 µs | −3.00% |
| Noto Serif / combining / liga off | 6.19 µs | 6.09 µs | −1.59% |
| Noto Serif / 4K / default | 419.62 µs | 408.80 µs | −2.58% |
| JetBrains Mono / ordinary / default | 22.12 µs | 21.14 µs | −4.44% |
| JetBrains Mono / 4K / default | 364.52 µs | 353.38 µs | −3.06% |

Each pair's complete records match. Source finalization, output consumption/flat
copy and the same simple English wrapper remain in both timed scopes; these are
not engine-only or final Unicode-layout timings. Raw control results/header
hashes are in `/tmp/fontshaper-step3-paired.json`; the temporary step-2 header is
`/tmp/fontshaper-before-api-step3.h`. Rebuild using step 2's control commands with
`-DFONTSHAPER_BENCH_MANAGED` in both variants and add
`-DFONTSHAPER_BENCH_BATCH` only for step 3.

The full run versus the historical baseline had median −0.67%, range
−4.87% to +8.51%; the matched paired control above is preferable to attributing
all historical timing differences to this batch. Default 4K observations were
404.28/508.92/406.47/349.48 µs (Sans/Inter/Serif/Mono). Earlier retained-prefix
GSUB index costs were not changed or hidden. Constructor/backing bounds are
unchanged from step 2; no memory/RSS improvement is claimed. Setup and cold config
construction remain reported separately by the runner.

Editor/font ownership and application-call migration are still step 4.

### 4. Migrate callers, remove public choreography and measure

- [ ] Migrate the editor and font setup together where ownership changes demand
  it; reuse its allocator/arena conventions rather than add a parallel allocator.
- [ ] Check every shaping caller: ordinary text, list/task markers, replacement
  text and any remaining direct-shaping paths. Do not fix only the main paragraph
  path or accidentally change source mapping/normalization contracts.
- [ ] Migrate regression and benchmark harnesses to the new completed-result
  lifecycle. Keep the old implementation available as a test oracle only as
  long as it is needed.
- [ ] Remove or internalize obsolete public setup/execution glue after callers
  migrate. No permanent application wrapper preserving the old choreography;
  do not gratuitously remove a useful expert direct-engine API.
- [ ] Update header documentation and examples to lead with whole-buffer usage.
- [ ] Rebenchmark steady-state latency, creation/cold costs and memory bounds,
  including retained multi-run output and repeated result traversal.

## Checks to carry through all batches

Extend the existing small standalone tests and benchmark runner; no new test
framework is needed.

- Compare glyph IDs, font identity, advances/offsets, source/cluster mappings,
  run ordering/boundaries, directions, scripts and complete break metadata.
- Cover English prose, code, ligatures, combining marks, Arabic, Devanagari,
  mixed scripts/fonts/directions, controls, empty input and existing malformed
  UTF-8 behavior. Do not silently change normalization or invalid-input policy.
- Test default options after overridden calls, equivalent feature orders,
  duplicate tags, changed values and caller feature-array lifetime/reuse.
- Test multiple runs: earlier glyphs survive later shaping; traversal can be
  repeated and performs no allocations or shaping. Rendering only the first
  run must not be necessary to discover an error in the last run.
- Test capacities, expansion, allocation/cache exhaustion, error recovery,
  construction failure, destruction and buffer lifetime with ASan/UBSan.
- Preserve warm scratch-bound/config caching. Repeated identical calls should
  not rebuild configs, allocate repeatedly or grow memory indefinitely.
- Measure realistic paragraph sizes and a roughly 4,000-character multi-paragraph
  workload, with creation/cold setup reported separately from warm shaping.
- Report actual time and memory changes, not assumed API speedups. Agreement
  with the old implementation is regression evidence, not independent Unicode
  or OpenType conformance certification.

## Deliberately out of scope

- Promoting the ASCII spike or writing a new ASCII-specific break engine.
  Omitting break processing saved roughly 35%; preserving it left only about
  3–5% on ordinary paragraphs in the current experiment.
- Making a flat glyph array mandatory, replacing slot/link storage just to avoid
  iterator syntax, or performing final renderer-specific layout in the shaper.
- Width-aware wrapping (still tracked in the optimization plan), document-level
  paragraph invalidation/caching, or a complete Unicode bidi line-layout redesign.
- Shared mutable-cache synchronization, speculative extensibility or duplicate
  allocator and fixed-memory ownership systems without a caller requirement.

## Source/change discipline

**Step 1 resolution:** no modular Fontshaper sources or amalgamation generator
are present in this checkout. Retain the existing vendored-header workflow for
steps 2–3: `src/lib/fontshaper/fontshaper.h` is the working production artifact,
and the ASCII spike is not its source of truth. Do not regenerate from an older
external snapshot or claim the local changes were synchronized to modular files.

`provenance.json` retains the original extraction/source pins and separately
records the current vendored artifact under `local_patch`. Preserve the original
upstream/snapshot pins and update the local header hash when making production
changes; do not silently present the historical modular-source hashes as current.

Preserve unrelated work. Re-read current callers before each batch, use explicit
`if`/`else` rather than ternary operators, and do not mechanically rename the
existing API while leaving execution and ownership semantics unchanged.

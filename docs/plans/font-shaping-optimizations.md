# Font-shaping optimization plan

## Goal

Expose a paragraph-oriented API with UTF-8 or codepoint input, a line width,
caller-owned scratch/output buffers, and reusable prebuilt context data. Keep
Unicode shaping support rather than committing to an English-only engine.

Use Astra's direct engine underneath the context API. Move invariant setup out
of the per-paragraph path, then add fast paths where measurements justify them.

**Commitment: work through at least optimizations 1–5 below.** Keep edits atomic,
preserve shaping behavior, and measure after each meaningful change.

Code-size and time estimates are rough implementation-and-test estimates, not
promises. Performance benefits are hypotheses unless backed by the measurements
below. Code estimates exclude substantial benchmark/test additions.

## Baseline evidence

Compared the repository's Astra `fontshaper.h` with KB `kb_text_shape.h` v2.28e,
using Clang 23, `-O3 -march=native`, on a Ryzen 9 7950X3D with CPU affinity.
Font loading and initial configuration were outside the shaping timings. Inputs
were warm, repeated paragraph-sized samples, not million-character stress tests.

Times below include input conversion, shaping, copying glyphs into a flat output
array, and the same simple English word-wrapper at 640 px with 18 px text. Direct
paths include the editor's `utf8proc_decompose` step. Context paths include
segmentation. Values are medians of five timed batches, in microseconds.

| Font / input | Codepoints | Astra direct | KB direct | KB context | Astra context |
| --- | ---: | ---: | ---: | ---: | ---: |
| Noto Sans Nerd: heading | 54 | 4.0 | 4.5 | 7.0 | 1,128 |
| Noto Sans Nerd: ordinary paragraph | 249 | 16.9 | 24.1 | 34.6 | 1,142 |
| Noto Sans Nerd: long paragraph | 915 | 54.4 | 108.1 | 145.4 | 1,198 |
| Inter Nerd: ordinary paragraph | 249 | 22.1 | 41.1 | 51.1 | 722 |
| Inter Nerd: long paragraph | 915 | 81.6 | 253.6 | 265.4 | 766 |
| Noto Serif Nerd: ligature-heavy prose | 241 | 13.8 | 18.0 | 27.8 | 536 |

All four paths produced matching glyph IDs, advances, offsets, and source-index
checksums across 33 font/input combinations. This establishes agreement between
the implementations, not independent Unicode/OpenType conformance.

Key findings:

- Astra's direct path generally outperformed KB's direct path.
- Astra's `ShapeBegin` calls `SizeOfContextScratch`, which evaluates configuration
  bounds across every script for the font stack. Per-pass configuration caches
  are also reset. A Noto Sans context profile attributed about 92% of sampled
  cycles to `fontshaper__PlaceShapeConfig`, plus 3.7% to feature-tag conversion.
  This is evidence for targeting setup first, not a predicted 92% speedup.
- External utf8proc processing accounted for about 2.4% of the sampled Astra
  direct path. Removing Unicode decoding is not the obvious first optimization.
- Noto Serif Nerd exercised real standard ligatures: 241 codepoints became 211
  glyphs. The installed Inter Nerd has contextual alternates and discretionary
  ligatures, but no standard `liga` feature. An explicit `dlig` test reduced
  81 codepoints to 75 glyphs with matching outputs between implementations.
- KB's context made recurring allocations for some JetBrains Mono inputs.
  Approximately 5,100 repeated shapes accumulated 23 MB with no allocator frees
  during the run. An 8 MB fixed-memory context eventually exhausted its memory
  and crashed at a null destination in `kbts_PlaceShapeScratchpad`. Do not assume
  the untouched KB context satisfies the bounded-scratch requirement.
- Astra's memory bounds can be conservative. At a 2,048-glyph capacity, the Noto
  Sans setup queried about 240 KiB for direct glyph storage, 921 KiB for direct
  execution scratch, and 102 MiB for context scratch. These are buffer bounds,
  not resident-memory measurements.

Temporary benchmark sources, snapshots, results, and perf recordings were saved
under `/tmp/shaper-bench/`. They are not durable repository artifacts. Preserve or
recreate the harness before relying on it in a future session. Important files:

- `bench-template.c`, `run.py`, `extra-fonts.py`
- `results.json`, `extra-results.json`, `source-checksums.txt`
- `astra-snapshot.h`, `kb-snapshot.h`
- `astra-context.perf`, `astra-direct.perf`, `kb-direct.perf`
- `kb-fixed.c`, `fixed-asan-o3-long.log`

## Execution and validation rules

- Use small, reviewable edits; avoid combining ownership changes and algorithm
  changes in the same patch.
- Establish a reproducible current baseline before implementing step 1.
- Keep font loading/configuration timing separate from per-paragraph timing.
- Compare glyph IDs, positioning, source/cluster mappings, font identity, and
  output ordering before accepting performance changes.
- Include ordinary prose, headings, ligatures, code, accented/combining text,
  and mixed-script input. Include Arabic and Indic smoke tests with suitable
  fonts; matching the old output is not sufficient as a full correctness oracle.
- Exercise repeated calls, changing paragraph lengths, feature changes, font
  stack changes, capacity boundaries, and insufficient-buffer behavior.
- Track steady-state allocations and memory growth as well as latency.
- Do not bypass validation or silently drop text when an optimization's
  preconditions do not hold; use the general path or report insufficient space.
- Consider early-exit decision trees when they simplify measured hot dispatch.
  Do not replace switches indiscriminately: compiler and input pattern matter.

## 1. Cache context scratch-size calculations

- [x] Implement
- [x] Verify invalidation and output equivalence
- [x] Benchmark and record results

**Complexity:** small. **Estimated change:** 20–60 lines. **Estimated time:** a few hours.

Cache the result of the expensive context scratch-bound calculation instead of
walking every script on every `ShapeBegin`.

Cache key:

- Font-stack revision.
- Language.
- Input-codepoint capacity.
- Intermediate-glyph capacity.

Font data is immutable after compilation. Invalidate when the font stack or any
other bound-affecting input changes. Confirm the complete set of dependencies in
the implementation rather than assuming this list is exhaustive.

**Expected benefit:** highest-confidence first win, targeting the profiled
all-script sizing loop. Actual per-pass configuration construction remains.

**Acceptance checks:** repeated identical calls reuse the bound; changed keys
recompute it; output and capacity/error behavior remain unchanged.

**Results:** the last successful bound is cached by language and capacities;
font push/pop invalidates it. On the ordinary Noto Sans paragraph, this alone
reduced warm context time from 1,106 to 986 µs. See the measured table below.

## 2. Persist script/language shape configurations

- [x] Implement
- [x] Verify lifetimes and output equivalence
- [x] Benchmark and record results

**Complexity:** medium. **Estimated change:** 80–200 lines. **Estimated time:** half a day to a day.

Retain configurations keyed by font, script, and language across paragraphs.
Build configurations lazily for combinations actually encountered, then reuse
them. Reuse existing cache structures where practical.

**Ownership requirement:** these configurations currently live in per-pass
scratch. Give them persistent backing storage. Merely retaining their cache
pointers while reusing that scratch would create dangling or overwritten data.
Keep persistent storage explicitly bounded/caller-owned and handle exhaustion.

**Expected benefit:** remove repeated configuration construction for familiar
scripts, bringing context overhead closer to the direct path.

**Acceptance checks:** repeated Latin paragraphs build one reusable configuration;
new scripts/languages get distinct configurations; resetting paragraph scratch
does not invalidate retained data; changing fonts cannot return stale configs.

**Results:** opt in with `fontshaper_SetContextCache(context, memory, bytes)`.
Existing callers retain per-pass configs; the new buffer explicitly owns cached
blocks and packed configs. Construction workspaces remain transient. Font-stack
changes clear both config caches at the next Begin. The cache is append-only and
bounded: exhaustion reports sticky `OUT_OF_MEMORY`; no allocation or eviction.
Rebind between passes to clear it before it fills. NULL/0 disables persistence.
After step 4, any successful SetContextCache invalidates the scratch bound:
re-query and provide enough scratch before the next Begin, especially on disable.
Noto Sans ordinary prose fell from 986 µs with step 1 to about 24 µs.

## 3. Persist feature-specific glyph configurations

- [x] Implement
- [x] Verify feature-key correctness and lifetimes
- [x] Benchmark and record results

**Complexity:** medium. **Estimated additional change:** 50–150 lines.
**Estimated time:** half a day. **Depends on:** step 2.

Retain glyph configurations keyed by the persistent shape configuration and the
effective feature overrides, including their values.

Compare feature settings by content, not by temporary-array address. Copy any
feature data that must survive a paragraph. Keep default prose, ligatures-off,
and code-feature configurations distinct without rebuilding them every pass.

**Expected benefit:** eliminate repeated lookup-control construction for recurring
styles. Likely smaller than steps 1–2 for plain, uniformly styled paragraphs.

**Acceptance checks:** equivalent feature sets reuse configurations; different
values do not collide; temporary feature arrays may be reused safely; toggling
features across repeated paragraphs gives the expected output without growth.

**Results:** effective feature keys are copied into persistent storage and
compared by count, tags and values, never by transient addresses. Equivalent
orders and last-wins duplicates reuse a config; different values remain distinct.
Default features reuse the base shape config without an extra glyph config.
Step 3's additional timing benefit is below the noise in these uniformly styled
paragraphs; its config reuse and bounded memory behavior are regression-tested.

### Measured results after steps 1–3

Fresh before/after runs: Clang 23 `-O3 -march=native`, CPU 5, warmed paragraphs,
five batches; UTF-8 context input plus the original flat-copy/English-wrapper
harness at 640 px / 18 px. Font loading and cold config construction are excluded.
Each entry is the median batch time in microseconds, not a universal speedup.

| Font / input | Codepoints | Before | Step 1 | Steps 1–2 | Steps 1–3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Noto Sans Nerd: ordinary | 249 | 1,106.0 | 986.3 | 24.2 | 24.3 |
| Noto Sans Nerd: long | 915 | 1,169.5 | 1,056.6 | 86.3 | 86.6 |
| Inter Nerd: ordinary | 249 | 732.8 | 554.9 | 31.1 | 31.3 |
| Inter Nerd: long | 915 | 809.4 | 638.3 | 112.9 | 113.7 |
| Noto Serif Nerd: ligatures | 241 | 537.8 | 403.1 | 22.4 | 22.4 |
| JetBrains Mono Nerd: ordinary | 249 | 2,035.1 | 1,881.6 | 21.3 | 21.4 |

All 192 stage/font/input/feature runs agreed on glyph/source/position checksums,
glyph counts and missing-glyph counts. These cover four fonts, six inputs, default
features and explicit `liga=0, kern=1`, and four implementation stages. Cached
runs used an 8 MiB caller-owned cache; this is a benchmark budget, not a required
size or a bound covering arbitrary script/language/style combinations. Scratch
queries remain conservative and essentially unchanged; tightening them is step 4.
Cold sizing and first use of each configuration still incur construction costs.

`tests/fontshaper_context_test.c` is a standalone regression check; its header
contains build/run commands. Clang, GCC, and Clang ASan/UBSan runs pass. It compares
cached/noncached IDs, positions, font identity, source IDs and ordering, including
Arabic, Devanagari, mixed scripts/styles, manual boundaries, combining marks,
empty input and line breaks. It alternates and poisons paragraph scratch, checks
feature order/values/duplicates, crosses glyph-cache blocks, verifies stable
cache usage on repeats, exercises language/capacity/font changes and cache
rebind/disable/unaligned storage, and checks cache/glyph exhaustion. This is
regression agreement, not independent shaping conformance certification.

Reproduction artifacts: `/tmp/shaper-bench/cache-optimizations.py`,
`cache-results.json`, `cache-benchmark.log`, and `cache-*.h`/`cache-*.c` preserve
the measured variants. The pre-change header is `astra-before-cache.h`, SHA-256
`53215920827a9827a66f1f559468193c259ceb928f29cf2887bfe15c2b74a9f8`.
These temporary harness artifacts are not durable repository files.

## 4. Tighten memory bounds around the prepared context

- [x] Implement (scope below)
- [x] Verify bounds and exhaustion behavior
- [x] Measure scratch requirements and latency

**Complexity:** medium. **Estimated change:** 50–150 lines.
**Estimated time:** half a day to a day. **Depends on:** persistent config ownership.

Separate persistent configuration storage from transient paragraph scratch.
Avoid reserving transient space for every hypothetical script/configuration when
only a prepared subset is supported by the current execution workspace.

Base execution bounds on supported/prepared configurations. Define what happens
when a newly encountered configuration needs more space: preparation/re-query or
an explicit insufficient-space result, not buffer overrun or lost text.

**Expected benefit:** primarily smaller scratch requirements. Smaller buffers do
not by themselves guarantee faster shaping.

**Acceptance checks:** sufficient queried buffers work at their stated limits;
new configurations cannot exceed old bounds silently; insufficient space fails
safely; paragraph scratch does not grow across identical repeated calls.

**Implemented scope:** when persistent storage is enabled, paragraph scratch no
longer reserves shape/glyph config payloads or cache blocks. The no-cache bound
is unchanged. Keep the maximum construction/execution workspace over every
font/script, rather than restricting the API to a frozen prepared subset. New
scripts/languages still fit without a new preparation protocol; the persistent
cache must independently have enough space. Further workspace tightening is
possible, but is not needed for this reduction.

Every successful SetContextCache invalidates the memoized bound. Call it before
querying/allocating scratch; re-query on rebind or disable. Disabling persistence
can substantially increase the required scratch size. The existing Begin API
has no scratch-byte-length argument: sufficient allocation remains the caller's
contract, not a runtime-detectable undersized-buffer error.

Measured queried scratch at input/glyph capacity 2,048, with an independent
8 MiB persistent cache and the same Clang/CPU/harness as above:

| Font | Before scratch (MiB) | After scratch (MiB) | Ordinary paragraph before/after (µs) |
| --- | ---: | ---: | ---: |
| Noto Sans Nerd | 101.87 | 1.83 | 24.71 / 24.58 |
| Inter Nerd | 73.46 | 1.32 | 31.87 / 31.35 |
| Noto Serif Nerd | 77.29 | 1.34 | 24.49 / 23.95 |
| JetBrains Mono Nerd | 250.55 | 2.35 | 21.52 / 21.29 |

These are allocation bounds, not a claim of reduced resident usage. Warm latency
is essentially unchanged: median after/before ratio across 48 cases was 0.990;
do not interpret that small difference as a demonstrated speedup. All 96 timed
before/after runs agreed on output checksums/counts, across four fonts, six
inputs and two feature settings. Scratch allocations used the new query after
cache setup, not the old oversized buffers.

The regression test now queries/reallocates exactly sized scratch on language,
capacity, font or cache-mode changes. It checks shrink/grow on enable/disable,
invalidated memoized bounds on rebind, and full input capacity. Existing cold
config, multilingual, scratch-poisoning and cache/glyph-exhaustion checks pass
with the smaller allocations under Clang ASan/UBSan and GCC.

Temporary reproduction artifacts: `/tmp/shaper-bench/step4.py`,
`step4-results.json`, `step4-benchmark.log`, `astra-step4.h`, and the pre-change
snapshot `astra-before-step4.h` (SHA-256
`d937fa3caad4dd2634a00462df9877a8543e54b8c09bb88679ca627aca480dac`).

## 5. Add a single-run ASCII fast path

- [ ] Implement
- [ ] Verify guards, fallback, and shaping equivalence
- [ ] Benchmark against the improved general context path

**Complexity:** medium. **Estimated change:** 50–150 lines.
**Estimated time:** half a day to a day. **Performance prerequisite:** remeasure after steps 1–3.

When input is ASCII with a known LTR paragraph direction, one applicable font,
and uniform features, construct a single Latin run without general script/bidi
segmentation. Confirm font coverage and handle controls/breaks according to the
normal API contract; ASCII alone does not make all characters ordinary prose.

Still use the normal OpenType engine: preserve kerning, ligatures, contextual
substitutions, and source mappings. Fall back when any precondition fails.

**Expected benefit:** remove segmentation overhead from common Markdown input.
Do not estimate its benefit from timings dominated by the old setup bottleneck.

**Acceptance checks:** fast/general paths agree for eligible input; non-ASCII,
font fallback, controls, mixed styles, and direction-sensitive input use the
appropriate general handling; feature overrides remain effective.

**Historical unchecked spike results (not production implementation):**
The initial `src/lib/fontshaper/fontshaper_ascii_spike.h` was a copy of the step-4 header with
`if(0)` disabling break initialization/processing and a `goto` forcing one
Latin/LTR run. It reuses normal UTF-8 decoding, input storage, cached configs,
nominal cmap mapping and OpenType shaping. It skips font-coverage validation,
segmentation and general run preparation; it has no eligibility checks, replay,
or correct public break metadata. Production `fontshaper.h` is unchanged by the
spike. This measures an optimistic ceiling, not the cost of a finished fast path.

Run the current spike from the repository root (now includes the guarded
follow-up below; the unchecked table records the earlier snapshot):

```sh
python tests/fontshaper_ascii_spike_bench.py --output /tmp/fontshaper-ascii-spike-results.json
```

The runner builds the same C harness against each header, alternates timing
order, pins CPU affinity, and compares every emitted glyph/source/position row.
The harness reuses font/context setup from the context regression test via its
optional `FONTSHAPER_TEST_HEADER` include override. Clang 23.1.4,
`-O3 -march=native`, CPU 5, medians of five warmed batches; timings include UTF-8
input, flat copying/source-ID lookup and the same English wrapper at 640 px /
18 px. Compilation, loading, first config creation, input validation and output
dumping are outside the timings. Both paths use capacity 2,048 and an 8 MiB
persistent cache. This is a fresh paired benchmark, not a comparison against
historical timings from the temporary harness that no longer exists.

| Font / ASCII input | Codepoints | Normal context (µs) | Unchecked spike (µs) | Time saved |
| --- | ---: | ---: | ---: | ---: |
| Noto Sans Nerd: ordinary | 249 | 24.63 | 16.06 | 34.8% |
| Inter Nerd: ordinary | 249 | 32.22 | 23.11 | 28.3% |
| Noto Serif Nerd: ordinary | 249 | 24.27 | 15.86 | 34.7% |
| JetBrains Mono Nerd: ordinary | 249 | 21.59 | 13.14 | 39.2% |
| Noto Sans Nerd: repeated ordinary text | 999 | 97.57 | 63.64 | 34.8% |
| Noto Serif Nerd: ligature-heavy | 241 | 22.95 | 14.87 | 35.2% |

All 48 pairs matched (96 timed runs): four fonts, six printable-ASCII inputs,
default features and explicit `liga=0, kern=1`. IDs, codepoints, source indices,
source user IDs, X/Y advances, X/Y offsets and wrapped positions agree; the
harness checks font identity and LTR direction. Cache usage remains stable in
every timed batch. The Noto Serif ligature input still produces 211 glyphs with
default features and 241 with ligatures disabled. Median time saving across all
pairs was 34.7%. Break metadata is intentionally NOT equivalent or validated.
Four spike ASan/UBSan smoke cases passed (Noto Sans ordinary and Noto Serif
ligature input, each with/without overrides); normal context regressions pass.

**Decision:** enough potential saving to justify implementing guards/replay and
preserving break metadata. Production step 5 remains pending; its actual benefit
will be smaller if restoring those semantics costs significant time.

### Phase 1: guarded candidate and one-way replay — historical intermediate spike

The spike now starts a tentative Latin/LTR run only for explicit LTR and one
font, with no pending manual segmentation. Each codepoint checks printable-ASCII
range, nominal glyph coverage and unchanged effective features; its nominal
mapping is reused during preparation. Equivalent reordered feature sets do not
cause a miss. A final letter-presence check sends neutral/digit-only and empty
input through the normal path rather than selecting Latin unconditionally.
Invalid UTF-8 also disqualifies the candidate. Manual boundaries and font-stack
mutations flush the candidate before changing state.

On the first miss, replay the existing buffered codepoints through the normal
break engine. Source IDs, feature snapshots and input storage are retained;
UTF-8 decoding and glyph shaping are not repeated. A diagnostic counter verifies
at most one replay per paragraph. Genuine resource errors remain sticky errors,
not fast-path misses. The next Begin resets eligibility.

**Scope:** fallback is conservatively paragraph-wide, with no fast re-entry in a
mixed paragraph yet. General processing determines its script/direction/font
run boundaries. Language in this API is a Begin parameter, not a per-run switch;
no English-only language-tag restriction is imposed. Successful fast passes
still omit public break metadata and are not production-ready. Phase 2 must
restore that metadata before integration; production `fontshaper.h` is untouched.

Fresh paired timings, same compiler/CPU/harness and five warm batches:

| Noto Sans input | Normal context (µs) | Guarded spike (µs) |
| --- | ---: | ---: |
| Ordinary, 249 ASCII characters | 24.56 | 17.01 |
| Early Unicode miss (`Café.` then ordinary paragraph) | 26.55 | 26.71 |
| Late Unicode miss (ordinary paragraph then `Café.`) | 26.57 | 27.58 |

The eligible case retains a 30.7% time saving. Miss overhead is about 0.16 µs
early and 1.00 µs after the 249-character prefix in this run; neither is a bound
for arbitrary fonts or input. All 64 timed pairs matched glyph/source/position
output across four fonts, eight inputs and two feature settings. Cache usage
remained stable. Results: `/tmp/fontshaper-ascii-guarded-results.json`.

The runner also builds `tests/fontshaper_ascii_replay_test.c` against both headers
and compares its output. Fallback checks include full run and per-codepoint
break metadata, early/late Unicode, combining marks, Greek, controls, invalid
UTF-8 through both UTF-8 APIs, digits-only/empty input, feature changes, explicit
manual runs/breaks, font push/pop, mixed-script fallback, source IDs and UTF-32.
Fast cases check font/script/direction/glyph output, including leading digits and
punctuation and equivalent reordered features. Additional assertions cover
missing nominal glyphs, one-way replay, repeated Begin and input exhaustion.
ASan/UBSan comparisons pass; the existing context regressions also pass when
compiled against the guarded header. Fast break-metadata equivalence remains
explicitly excluded until phase 2.

### Phase 2: restore metadata and remove unnecessary replay — current spike

Restoring the normal break engine at End first reduced the Noto Sans saving to
about 2–4%. The current version instead streams the normal breaker during input,
skipping repeated coverage tests for the already-validated single-font ASCII
candidate. This avoids an extra buffered traversal. It still takes the direct
one-Latin-run preparation shortcut when the entire candidate remains eligible.
Run flags come from the real first-codepoint break flags, not synthetic zeros.

Because break state stays current, a miss now only disables the optimization;
it does NOT need to reset or replay the prefix. General processing continues
from the existing state. No extra input buffer or decoding is needed. Feature
snapshots, source IDs, word/grapheme/line flags, script and direction metadata
are preserved. The diagnostic is now `AsciiMissCount`, not a replay count.
The optimization still does not re-enter within a mixed paragraph; eligibility
resets at the next Begin. Production `fontshaper.h` remains unchanged.

Fresh paired warm timings with final break metadata restored:

| Font / input | Normal context (µs) | Current spike (µs) | Time saved |
| --- | ---: | ---: | ---: |
| Noto Sans: ordinary, 249 cps | 24.29 | 23.11 | 4.9% |
| Inter: ordinary, 249 cps | 31.13 | 30.12 | 3.2% |
| Noto Serif: ordinary, 249 cps | 23.86 | 23.06 | 3.3% |
| JetBrains Mono: ordinary, 249 cps | 21.32 | 20.25 | 5.0% |
| Noto Sans: long, 999 cps | 95.84 | 90.35 | 5.7% |
| Noto Sans: early Unicode miss | 26.15 | 26.36 | -0.8% |
| Noto Sans: late Unicode miss | 26.21 | 26.03 | 0.7% |

Median saving over the 48 eligible ASCII pairs was 4.8%; small per-case changes
are noisy. Ineligible inputs are now near baseline because no replay is needed.
All 64 pairs match glyphs, source IDs, positions, run scripts/directions/flags/
boundaries and per-codepoint break metadata. The runner performs metadata dumps
outside the timed region. The additional guard/fallback regression now compares
metadata for successful fast calls too, including nominal/font mappings; it
passes with ASan/UBSan. Results: `/tmp/fontshaper-ascii-streamed-results.json`.

**Finding:** most of the unchecked spike's ~35% saving depended on omitting break
processing. Cheap eligibility detection is not the remaining bottleneck. A large
production win would require a genuinely faster but equivalent ASCII break
implementation, not merely more fallback logic. Keep this as an experiment;
production integration and per-run re-entry are not implemented.

## 6. Streamline input conversion and glyph population

- [ ] Reprofile and decide whether needed
- [ ] Implement only a demonstrated improvement

**Complexity:** medium. **Estimated change:** 50–200 lines. **Estimated time:** about a day.

Potential work:

- Decode UTF-8 directly into the input representation.
- Batch ASCII conversion/cmap lookup.
- Remove demonstrated redundant copies or normalization work.

Do not skip normalization indiscriminately: combining characters and font
coverage depend on correct handling.

**Expected benefit:** probably modest initially; external utf8proc was only
about 2.4% of the measured direct-path profile.

## 7. Add a flat-output paragraph operation

- [ ] Define the output/lifetime contract
- [ ] Implement the adapter
- [ ] Verify mappings and output equivalence

**Complexity:** small–medium. **Estimated change:** 30–100 lines.
**Estimated time:** a few hours to half a day.

Expose one operation that collects shaped runs into the caller's glyph array.
Preserve glyph IDs, advances, offsets, font identity, source/cluster mappings, and
ordering. Define output capacity and insufficient-space behavior explicitly.

Use existing iterators internally at first. Rewriting canonical glyph storage is
not required to get a pleasant editor-facing interface.

**Expected benefit:** primarily interface simplification; measure any bulk-output
speedup. This can be done earlier for usability despite its lower performance
priority.

## 8. Integrate width-dependent wrapping

- [ ] Define the initial layout scope
- [ ] Implement wrapping and final positioning
- [ ] Validate line boundaries, clusters, and source mappings

**English-only estimate:** 100–200 lines, about a day.
**General Unicode estimate:** substantially larger, several days or more.

Use existing segmentation/break information rather than writing a second breaker.
General layout must account for legal break opportunities, grapheme/shaping
clusters, bidi ordering within final lines, and boundary reshaping where needed.

**Expected benefit:** completes the desired width-aware API rather than making
shaping itself faster. The benchmark's English word-wrapper is not a complete
Unicode layout implementation.

## 9. Optimize remaining GSUB/GPOS execution

- [ ] Reprofile representative fonts after setup fixes
- [ ] Choose one measured hotspot
- [ ] Implement and validate incrementally

**Complexity:** large/variable. **Estimated change:** potentially hundreds of lines.
**Estimated time:** days to weeks.

Possible targets include lookup rejection/admission, common GPOS adjustments,
bucket/index rebuilding, and glyph-data locality/dependency chains.

**Expected benefit:** potentially valuable but font-dependent, with the greatest
regression risk. Astra already outperforms KB's direct path on most measured
prose cases. Do not replace working shaping machinery speculatively.

## Working order

1. Establish the current benchmark baseline.
2. Implement scratch-bound caching; benchmark again.
3. Persist shape configs; benchmark again.
4. Persist feature-specific configs; benchmark again.
5. Tighten memory bounds and add the guarded ASCII path, each separately tested.
6. Build the flat paragraph/wrapping interface when ready to integrate it.
7. Pursue conversion or core-engine optimizations only when the remaining cost
   justifies them.

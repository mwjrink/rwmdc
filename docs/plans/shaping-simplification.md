# Shaping simplification: implementation and acceptance plan

Status: proposed implementation contract; no implementation work is marked complete.

Scope: the owned shaper in `src/lib/kb`, its public construction/shaping paths, and the existing shaping validation adapters. This document turns the 34 inline review findings, the reachability requirement, and four additional source-review findings (`LUT01`, `LUT02`, `RUN07`, `CACHE01`) into required changes, implementation TODOs, observable acceptance criteria, and a removal ledger.

The existing HTML plans remain historical design artifacts. This plan builds on the implemented canonical stable-slot cutover; it does not restart that work or authorize a second shaping engine.

## 1. Objective and completion rule

Simplify by eliminating repeated discovery, intermediate serialization, competing ownership, and work outside the selected execution scope. Preserve shaping semantics and the existing supported API behavior.

**The target is fewer representations and fewer passes over the same facts, not fewer indented lines.**

A task is complete only when its required change, behavioral acceptance, applicable work/memory evidence, caller migration, and associated removals are complete. Compiling successfully is not acceptance. A performance result does not excuse a correctness failure.

Review findings marked `SUSPECT` are source-review findings, not runtime-confirmed failures. Establish a deterministic witness before changing semantics. A suspect may be closed without a behavioral change only with evidence disproving the premise or proving the existing invariant; record that disposition against its ID. This is not permission to waive the mandatory structural end state or quietly omit a finding.

### Required deliverables

- [ ] One implemented architecture satisfying section 2, without legacy bridges or alternative production engines.
- [ ] A disposition and evidence reference for every review ID in section 4.
- [ ] All applicable removals in section 5 completed alongside their owning changes.
- [ ] Correctness, allocation/lifetime, and targeted work-count evidence as specified in section 6. Timing optimization and performance gates are deferred.
- [ ] Accurate existing API/implementation documentation reflecting the final ownership and sizing contracts.

### Review conclusion and minimum-work contract

The architectural direction is sound: canonical stable slots, immutable font compilation, configuration-owned selection, and reusable execution capacity. **Closing the original comments alone does not remove all unnecessary work.** The additional obligations below cover repeated font-fact decoding, repeated config analysis, interval-insensitive index work, and cache working-set costs. Section 6.7 records the limited experiments actually run during this review; implementation boxes remain unchecked.

Minimize executed work and dependent memory accesses on representative inputs, not source-level nesting or branch count in isolation:

- Compute a fact at the longest lifetime where it remains true: font properties at font compilation; selected feature/stage policy at configuration construction; live glyph state at mutation. Do not move immutable table discovery into a run to make construction appear cheaper.
- A retained table must name its runtime or construction consumer, byte cost, and work it replaces. Precompilation is not permission for an unbounded glyph × lookup × context Cartesian product.
- Prefer a predictable guard that avoids a scan, clear, gather, or allocation over branchless execution of that work. Preserve the specifically approved all-stage SIMD matcher; this is not a mandate to make unrelated loops evaluate failed work.
- Sentinels/tombstones avoid deletion moves only with explicit validity, reuse, and traversal contracts. They do not make linked traversal cache-local or authorize stale queue handles. Glyph ID zero is a live missing-glyph value, not a deletion marker.
- No full-stream translation, glyph compaction, or extra mirror may be introduced to recover locality. Measure the existing canonical record/link layout and consume it directly. Necessary allocation growth, compact scheduling entries, bounded match windows, and one requested final output projection remain legitimate.
- Every proposed optimization must identify the removed instructions/reads/writes, any added resident bytes or dependent loads, and the affected phase. Reduced copied bytes alone is not acceptance.

**User priority, recorded during this review:** “Right now I want the cleanest, minimal code. We can worry about performance later. Minimal operations, minimal memory access & no more translating random data structures from one to the other. all compilation ahead of time.”

This decision governs the cutover: correctness, simple ownership, ahead-of-time compilation, no representation bridges, and removal of demonstrably redundant work are required now. Timing comparisons, PMU tuning, alternative-layout bake-offs, and percentage gates are later work, not prerequisites. Work/byte counts below prove structural claims; they are not a benchmark program to optimize before completing the architecture.

## 2. What the end state MUST look like

### 2.1 Font loading and immutable compilation

1. A font boundary establishes checked table extents and valid lookup/subtable references for the shaping structures consumed by this work.
2. Extension type and direction are resolved once, not independently rediscovered by compilation, fusion, planning, and each run.
3. One temporary compiler build model retains normalized rules, predicate identities, source/parent lookup relationships, action ownership, and rule priority.
4. Classification, dispatch construction, and fusion operate on that model or non-owning views of its tables.
5. Final contextual runtime tables are interned and packed into their resident allocation once. There is no complete intermediate contiguous `Raw` runtime cache.
6. All compiler-only data is released before compilation returns. Final tables retain neither pointers into dead scratch nor proof-only storage.
7. Successful compilation remains idempotent. Font compilation completes before the font is shared; this plan does not add a mutable lazy font cache.
8. Repeatedly consumed font-invariant GDEF classes and resolved lookup/filter descriptors are compiled once and read directly (LUT01/COLD2). Existing ASCII cmap and selected non-ASCII cmap ownership remain; do not add a parallel glyph mapping cache.

“One build model” does not require one physical struct for cold logical rules and compact SIMD columns. A compact final encoding is justified. A complete intermediate serialized cache that must then be decoded or copied again is not.

“One final allocation” refers to the contextual-program cache. The raw font/blob and the existing optional cmap cache have separate legitimate ownership; do not combine them merely to claim one allocation for the entire font.

### 2.2 Three logical compiler phases, not an impossible single pass

| Phase | Input | Required result | Repeated work that must disappear |
| --- | --- | --- | --- |
| Collect | Validated shaping-table views | Normalized rules, grouped source predicates, parent/action metadata | Repeated whole-glyph scans to decode related coverage/class sources; repeated extension walks |
| Analyze | One retained build model | Exact symbols, ordered dispatch candidates, fusion results, final layout inputs | Recovering parent/outcome relationships from serialized tables; evaluating identical dispatch membership again merely for sizing |
| Emit | Analyzed model and interned table payloads | Final immutable compact programs and classifier | Building a full `Raw` cache first, then repacking/copying it into a second cache |

Necessary dependencies remain: global symbol classes require collected predicates; fusion requires rule ordering and possible input domains; exact packing requires sizes and deduplication results. Several loops can remain within a phase. Every retained full-domain pass must have a named output and a reason it cannot consume information already produced.

### 2.3 Configuration construction

- Script/language selection uses the applicable OpenType language system and permitted `DFLT` fallback. It never selects an unrelated script merely because that script is present.
- Eligible feature references feed one ordered lookup union per semantic feature stage. Required/default/override policy is retained, not rediscovered through repeated minimum searches.
- Stage plans and admission rows use shared per-lookup analysis where that information is font-invariant. Configuration-dependent policy stays configuration-owned.
- Admission construction traverses its glyph-major data glyph-major or in justified cache-sized tiles.
- Configuration-owned runtime tables use the actual stage layout. Proof-only workspace has a construction lifetime.
- Fixed-memory sizing reports the capacity actually required for construction. Heap-owned final allocations do not retain temporary proof tails just because the fixed-memory path needs peak workspace.
- All font LUTs/programs are compiled before font publication. Selected shape configurations, effective glyph configurations, stage/admission tables and execution bounds are constructed during explicit configuration construction or high-level run preparation, before `ShapeRun`/`ShapeDirect` execution. Compile only configurations actually requested; do not enumerate every possible feature combination ahead of time.
- Prepared input/runs retain stable references to their resolved configs without copying glyph records into a new prepared-glyph representation. `ShapeRun` maps into canonical slots and executes; it does not call a config compiler or perform find-or-create per input glyph. Input/feature changes invalidate the appropriate preparation state before the next execution.
- “Ahead of time” does not mean precomputing live match outcomes. Live filtering, sequence matching, mutations, attachment propagation and capacity growth remain execution work. Native actions consume validated immutable tables/descriptors directly; no new per-run native-table translation is permitted.

### 2.4 Script-scoped work: exact boundary

A Latin run already uses selected lookups rather than executing every Arabic lookup in the font. The remaining requirement is to make execution plans and contextual workspace bounds depend on the **reachable lookup set**, not every contextual program in the font.

Reachability includes selected language-system features that this configuration can execute, required features, default-disabled features that a supported override can enable, contextual child lookup targets, and script preparation probes such as contextual `locl` localization. Preserve table identity and native lookup order. Shared `DFLT` or cross-referenced lookups remain reachable even if their source was associated with another script.

Required scoping witness: construct a font with a small Latin path and an unrelated Arabic-only contextual program with a very large window. The Latin configuration must not execute that program or inherit its contextual window capacity. Add a legal shared/nested reference and demonstrate that the now-reachable program is included and correctly sized.

**Deliberately retained tradeoff:** the font-global predicate classifier and immutable font program cache remain font-wide. This plan does not promise zero Arabic-related font-load work when only Latin will later be shaped. Script-local classifiers or lazy program construction would change symbol ownership, sharing, and the compile-before-sharing contract; those are not part of this cutover. Do not introduce remapping layers or a mutable lazy cache under the label of script filtering.

### 2.5 Runtime storage and state ownership

| Data | Owner and lifetime | Required sharing/use |
| --- | --- | --- |
| Validated native table views | Font; backed by live blob storage | Compilation, planning, native execution use the same interpretation |
| Compiler rules/proof workspace | Compilation scratch only | Queryable throughout analysis; not retained after emission |
| Compiled symbols/programs | Immutable font cache | Shared by dependent configurations; one symbol domain |
| Ordered plan/admission data | Immutable shape configuration | Reused by runs; no proof-only ownership |
| Canonical effective feature sets | Shaping context lifetime | Input spans and derived-config cache keys use stable immutable values |
| Derived glyph configurations | Existing context/config cache lifetime | Key includes the shape configuration and canonical feature-set identity |
| Execution scratch, indexes, queue workspace | Mutable shaping context or caller-owned low-level scratchpad | Capacity reused; never stored as mutable state in a shared font/plan |
| Glyph records | Existing stable-slot glyph storage | Normalization, GSUB, GPOS, actions, attachments, and output use the same records |
| Context windows and scheduling entries | Derived execution data | Only fields consumers need; no second full-stream glyph representation |

A feature-set identity is not a pointer into an arena that can be recycled while its cache entries remain live. A queue membership handle must remain meaningful under its documented growth/sort rules. A temporary script sort key must not silently replace canonical semantic metadata.

### 2.6 Preserved invariants

- One canonical `kbts_glyph` representation and stable surviving slot identities. Reordering relinks; insertion/deletion does not memmove the surviving glyph suffix.
- No glyph-to-hot-row, pending-span, emitted-row, or cold-record synchronization bridge returns.
- Native lookup, subtable, rule, and contextual action-record priority remains correct. A zero-action winning context can block later rules.
- Later actions and nested lookups observe prior live writes, insertions, and deletions. No frozen original-target array substitutes for live action targeting.
- GDEF filters, Unicode skip policy, required features, explicit disables/enables, and nonbinary feature values retain their distinct semantics.
- The approved fixed-depth SIMD matcher keeps its all-stage evaluation contract. Do not add failed-lane early exits as part of this work.
- Exact symbol-position indexes remain exact. Conservative admission may over-admit but must never lose a legal candidate.
- Source IDs, effective features, cluster associations, attachment relationships, advances, offsets, and output iteration/direction conventions remain intact except for an explicitly proved bug fix.
- GPOS ordering keys are established after structural reordering. Attachment interval logic relies on the declared nonstructural GPOS interval.
- Fixed buffers do not silently fall back to heap allocation. Exhaustion returns the existing appropriate error and leaves destruction/reuse safe under the API contract.
- Distinguish live count, physical slot high-water count, reserved capacity, and active-range length. None is a substitute for the others when sizing indexes or choosing traversal work.
- Reserve queue deletion keys and compiled invalid-index values outside valid domains. Boundary symbols, predicate-zero tautologies, inactive SIMD lanes, and compact-test overread guards remain valid after final interning/packing.
- Original allocator pointers are retained for freeing aligned interior storage. Construction failure does not dereference null or leak partially owned allocations.
- Font/config/scratch/glyph destruction order and borrowed result lifetime remain explicit and valid.

### 2.7 Non-goals

No editor/renderer integration, replacement shaper, new Unicode data version, general font-security audit, general text cache, disk LUT cache, thread pool, paragraph-bidi rewrite, or expanded OpenType conformance claim. Do not replace script algorithms merely to make their control flow look uniform. Validation changes cover the boundaries and shaping structures implicated by this plan, not an unrelated parser rewrite.

## 3. Execution order and shared contracts

| Phase | Work | Prerequisites / integration boundary |
| --- | --- | --- |
| P0 | Freeze evidence and establish witnesses | Current working tree, including user annotations, is the control |
| P1 | Validated font views, bounds, allocation ownership | Establish the shared descriptor/allocation contracts before downstream consumers migrate |
| P2 | Canonical feature state and cache identity | Stable feature ownership precedes execution-scratch reuse |
| P3 | Matching/action semantics and script lifecycle | Prove semantic behavior before changing compiler representation; P1 supplies resolved views |
| P4 | Single compiler model, ordered config build, scoped bounds | Preserve the P3 semantics; collect shared metadata before removing its old reconstruction paths |
| P5 | Reusable scratch and minimum necessary runtime index work | P2 lifetime separation and P4 layout/scoping are required |
| P6 | Queue representation and remaining ordering/fallback work | Stable GPOS/action contracts; P5 execution workspace ownership |
| P7 | Integrated acceptance | All producers/consumers have migrated; no alternative production path remains |

This order is not permission to defer a required removal to an unspecified later cleanup. Remove superseded code when its complete replacement path is accepted. If implementation is delegated, assign disjoint files where possible and name one owner for shared header/type changes; do not let separate agents invent competing views or ownership rules.

### P0 TODOs and evidence baseline

- [ ] Preserve the current owned correctness control and unchanged upstream control before recipes overwrite their executable paths. Retain existing pre-span/pre-translation binaries/source identities when available for later timing work; their absence does not block the structural cutover.
- [ ] Record source identity, compiler/flags, CPU affinity, input/feature modes, font file hashes, baseline output/checksums, and measurement commands.
- [ ] Record the disposition of all 34 review comments. Preserve user-authored annotations; do not treat them as disposable review markers.
- [ ] Establish minimal witnesses for suspect behavior using the existing fixture machinery where possible. Exercise public construction or shaping paths when that is where the defect lives.
- [ ] Record allocation/work costs for font compilation, config construction, low-level shaping and high-level preparation/execution separately. Do not require elapsed-time measurements at P0.
- [ ] Record work baselines: source-predicate visits, repeated dispatch membership tests, intermediate-cache bytes, proof/resident/peak bytes, row clears, completed-stage repairs, backtrack metadata stores, queue conversion bytes, cache searches, and allocator requests.
- [ ] Include font-class/lookup decodes, config symbol/bucket/proof visits, active-range versus whole-storage index decisions, index bytes by row family, link loads/writes, tombstone visits, and actual glyph-record/cache-line footprint in the work baseline.

Use the existing profiler where its counters answer the question. Add disposable probes for missing work counts, not a permanent telemetry subsystem. Instrumented work-count runs are not production timing runs.

## 4. Required implementation TODOs and per-finding acceptance

The review ID is the stable identifier. Acceptance labels `A-<ID>` can be used in result reports. Names and line numbers may move; the semantic obligation does not.

### P1 — Font trust boundary and allocation ownership

#### MAP01 — Bound font directory reads before dereference

**Location:** `kb_text_shape.h`, font count/loading and directory parsing.

- [ ] Validate the fixed directory header before reading `TableCount`; check TTC directory offsets and nested count/offset arithmetic against the actual available bytes.
- [ ] Carry established extents through loading/placement. Do not use an untrusted declared length as proof of the allocation's extent.
- [ ] Check offset/length/count arithmetic as integers before pointer formation, including early GSUB/GPOS lookup counting and `maxp` reads that precede compilation. Check scratch/output size accumulation, alignment additions, matrix products, and every narrowing to the public `int` sizes or stored `u32` sizes; no wrapped advertised capacity.

**Acceptance A-MAP01:** recognized but truncated four-byte font data, truncated collection headers, and out-of-range collection directory offsets return the documented invalid-font result without sanitizer errors or out-of-bounds reads. A valid single font and valid collection face still load. Probe each affected public entry point, not only a private parser helper.

Include truncated table headers/list arrays, valid outer directories with invalid nested offsets, and representability boundaries in A-MAP01. Use small malformed fixtures or synthetic sizing probes for overflow cases; allocating multi-gigabyte input is not necessary to prove checked arithmetic. A descriptor validator called only after `LoadFont` has already walked unchecked tables does not satisfy this boundary.

**Must remove:** pre-validation directory dereferences and unchecked nested pointer formation on these paths.

#### COLD2 — Resolve validated subtable descriptors once

**Location:** `kb_context_compile.inc`, `kb_context_fuse.inc`, `kb_compiled_context.h`, font loading/compilation in `kb_text_shape.h`.

- [ ] Establish shared checked lookup/subtable views with resolved type, table/lookup identity, valid target extent, and stable access to blob-backed data. Reuse existing flat subtable indexing rather than create a competing identity system.
- [ ] Cover loaded native blobs and the public load/place/compile path, not only TrueType/OpenType conversion.
- [ ] Reject invalid extension targets, extension-to-extension types, and inconsistent lookup subtype declarations. Retain the validated invariant for downstream consumers.
- [ ] Include the native action consumers: `kbts__DoSingleAdjustment` (GPOS extension handling), `kbts__DoSubstitution` (GSUB frames), `kbts__UnpackLookup`'s mark-filter-set resolution, and diagnostic decoders. Font-invariant flags/filter-set references belong in the same immutable descriptor, not a second runtime wrapper or a full descriptor copy per anchor.

**Acceptance A-COLD2:** valid direct and extension-wrapped equivalents shape identically; self/nested extension targets, zero/self offsets, truncated targets, out-of-range offsets, and inconsistent subtypes fail deterministically through the relevant construction boundary. No unbounded extension walk is reachable during compilation or shaping. Compilation remains idempotent and ownership is balanced on failure. Internal fixtures use the validated contract rather than a test-only unchecked bypass.

**Must remove:** independent raw extension-chasing logic in migrated compiler, fusion, plan, and runtime consumers.

**User ANSWER disposition:** the nested loop in `kb_context_compile.inc` enumerates GSUB/GPOS → each table's lookups → each lookup's subtables, then unwraps extensions. Its input is `Font->Blob`; its output is contextual rules/programs addressed by the existing flat subtable identity. It runs during font compilation, not for every run. The first three enumeration levels are necessary; repeated discovery and unchecked extension chasing are the problems. Resolve the wrapper once without flattening away native priority.

#### RUN06 — Consume resolved direction rather than decode each run

**Location:** `kb_gsub_stream.inc`, lookup direction selection.

- [ ] Read resolved type/direction from the shared descriptor established by COLD2.
- [ ] Remove the runtime wrapper walk without inventing mixed-direction handling for invalid fonts.

**Acceptance A-RUN06:** direct and extension-wrapped reverse-context lookups produce identical ordered results, including the existing reverse read-after-write witness. A scoped warm-run probe records no extension unwrap work in direction selection.

**Must remove:** per-run extension decoding used only to obtain lookup direction.

#### MAP02 — Separate class bounds from glyph bounds

**Location:** `kb_text_shape.h`, ClassDef admission matrix construction.

- [ ] Bound class-mask reads by the class index and its mask capacity.
- [ ] Bound matrix writes by glyph count. Define conservative admission for valid classes outside a small summary mask, or size the representation correctly; do not drop those rules.

**Acceptance A-MAP02:** ClassDef formats 1 and 2 behave equivalently for equivalent mappings; a low glyph ID with class 1024 or greater neither reads past a 16-word mask nor disappears from applicable behavior. Glyph IDs at/outside the font bound cannot write past admission rows. Preserve class-zero semantics. Run the boundary witnesses under ASan/UBSan.

**Must remove:** guards that validate `GlyphId` while indexing storage by `GlyphClass`.

#### MAP05 — Align bucket and sort storage with correct raw ownership

**Location:** `kb_text_shape.h`, bucket allocation and `SortGlyphBucket` storage; final queue replacement must use the same convention.

- [ ] Use checked alignment-slack sizing, aligned usable storage, and retained raw allocation bases.
- [ ] Migrate both bucket growth and sort/merge storage, including failure exits. Do not leave old and new allocation conventions beside each other.

**Acceptance A-MAP05:** an allocator returning an odd-address usable pointer can exercise queue growth and disordered sorting without undefined behavior. Fail each reachable allocation in turn; no invalid free, leak, stale usable pointer, or successful partial result occurs. Fixed-memory exhaustion remains explicit.

**Must remove:** direct typed casts of these unadjusted raw allocation results and freeing an aligned interior pointer.

#### MAP07 — Make context creation failure-safe

**Location:** `kb_text_shape.h`, `CreateShapeContext`/placement ownership.

- [ ] Guard the placement result before assigning allocator fields.
- [ ] Apply the common aligned/raw-base ownership convention and checked size calculation to self-owned context construction.

**Acceptance A-MAP07:** fail-first allocation returns failure without dereferencing null; odd-address allocation supports actual context use and balanced destruction. Caller-owned placement remains usable at its advertised size and does not acquire hidden heap ownership.

**Must remove:** unconditional dereference of a failed placement result and inconsistent context allocation ownership.

### P2 — Feature state, effective values, and cache identity

#### MAP09 — Publish the actual last-wins feature set

**Location:** `kb_text_shape.h`, effective feature snapshot construction.

- [ ] Publish the deduplicated effective values rather than the original stack prefix.
- [ ] Canonicalize tag/value ordering after last-wins resolution; retain explicit zero and nonbinary values. Do not collapse explicit disable into absence.

**Acceptance A-MAP09:** nested `[kern=0, kern=1]` and the reverse order shape according to the last value. Equivalent effective sets reached through different stack orders have identical behavior. Nonbinary selections and independent feature tags retain their values. Assert visible feature effects, not only array contents.

**Must remove:** copying the original stack prefix with a deduplicated count.

#### MAP10 — Correct feature pop and invalidation

**Location:** `kb_text_shape.h`, feature stack removal.

- [ ] Shift the actual remaining suffix, set the successful result, and invalidate effective state on a real removal.
- [ ] Preserve bounded public stack policy independently of font-feature enumeration.

**Acceptance A-MAP10:** top, middle, and bottom removal from three distinct overrides expose the correct remaining behavior; a missing tag reports no removal and changes nothing. Removing a nested same-tag override restores the earlier value. Append more text after each mutation and verify the next span receives the correct state. No-hit operations do not create unnecessary snapshots.

**Must remove:** the shift loop that ignores `MoveIndex`, the permanently false success result, and stale-state reuse after a successful pop. A small feature-metadata `memmove` is allowed; glyph suffix moves are not.

#### MAP11 — Give feature-set identity the cache's lifetime

**Location:** `kb_text_shape.h`, feature snapshots, prepared input references, persistent glyph-config cache keys.

- [ ] Intern immutable canonical effective feature sets in context-lifetime storage, shared by input references and cache keys.
- [ ] Key derived glyph configurations by their shape configuration and stable feature-set identity. Preserve font/script/language separation already carried by the shape configuration.
- [ ] Remove persistent references to recyclable snapshot storage; free interned state with the context.

**Acceptance A-MAP11:** reuse a context across successive `ShapeBegin` cycles with different equal-sized feature sets and deliberately reused scratch addresses. Each cycle shapes with its own values; earlier cached values cannot be returned because an address was recycled. Repeating the same bounded set of feature states stabilizes persistent allocation demand. Different shape configurations cannot share an incompatible derived glyph configuration.

**Must remove:** raw scratch address/count identity in persistent caches and duplicate ephemeral effective-value storage made obsolete by interning.

#### MAP12 — Stop cache searching at a hit and reuse span results

**Location:** `kb_text_shape.h`, shape/glyph-config caches and `ShapeRun` preparation.

- [ ] Return immediately on a hit instead of continuing through later blocks.
- [ ] Carry the last resolved key/result across contiguous input with unchanged effective configuration; invalidate on every actual key change.
- [ ] Resolve/create all actually used glyph configurations during run preparation and retain their references on the existing input/span ownership path. Remove `FindOrCreateGlyphConfig` from `ShapeRun`; merely caching its last result inside execution still leaves compilation on a miss. Preserve feature/source boundaries without allocating another full input or glyph mirror.

**Acceptance A-MAP12:** repeated spans and alternating configurations produce the same glyph/source/position output as fresh resolution. A many-span probe shows no search of later blocks after a hit and no per-codepoint cache search for an unchanged effective feature span. A previously unseen feature set is compiled during preparation; a counter around execution records zero font/shape/glyph-config compilation calls even on that first shape. Construction failures report through the preparation/error contract and leave reuse/destruction safe. Do not add a general hash layer in this cutover.

**Must remove:** inner-loop-only hit breaks, redundant per-codepoint configuration queries, and configuration find-or-create/compilation from the execution path.

### P3 — Matching semantics and script lifecycle

#### RUN05 — Give dispatch, matching, and actions the same anchor

**Location:** `kb_context_match.inc`, caller traversal and child action frames in `kb_glyph_actions.inc`/`kb_gsub_stream.inc`.

- [ ] Establish a filtered-anchor contract. Prefer outer traversal owning skip decisions; matching and actions use the supplied anchor without silent relocation.
- [ ] Migrate every matcher caller and direct fixture to that contract. Do not preserve an inconsistent private-helper expectation by adding compensating offsets.

**Acceptance A-RUN05:** a leading Unicode-skipped glyph, ignored mark, and overlapping first coverage cannot cause the matched glyph and action index zero to refer to different live glyphs. Test end-to-end substitution/positioning and source identity, not merely a matcher success flag. If admission already proves relocation impossible, document and demonstrate that invariant at the actual caller boundary.

**Must remove:** hidden anchor movement that leaves action frames pointing at an earlier glyph.

#### GLYPH2 — Share first-success subtable semantics

**Location:** `kb_glyph_actions.inc`, top-level and nested GPOS lookup application.

- [ ] Make both paths consume one ordered first-success lookup application contract.
- [ ] Continue executing distinct contextual action records in their prescribed order; only subtable iteration stops at success.

**Acceptance A-GLYPH2:** a child lookup with two overlapping adjustment subtables applies the first successful adjustment exactly once, matching direct application of the same lookup. A first-subtable miss reaches the second. A parent with two action records still executes both records. Compare advances/offsets, not helper call forwarding.

**Must remove:** child subtable loops that ignore adjustment success and can cumulatively apply mutually alternative subtables.

#### GLYPH3 — Resolve record targets with the same live filtering

**Location:** `kb_glyph_actions.inc`, `SequenceIndex` targeting after context matching.

- [ ] Use a shared live target resolver with the matching sequence skip policy and an explicit missing-target result.
- [ ] Resolve each record after preceding actions mutate the sequence. Never substitute an original-reference snapshot for live semantics.

**Acceptance A-GLYPH3:** ignored joiners/marks between input positions do not redirect an action to the wrong glyph. Earlier expansion, ligation, or deletion is visible to later records. An unfound target does not fall back to the original anchor. Verify exact changed glyph/source identities and retained suffix order.

**Must remove:** zero-skip rescans inconsistent with matching and accidental original-anchor fallback on a missing target.

#### GLYPH4 — Separate Hangul construction state from output length

**Location:** `kb_execute_op.inc`, Hangul normalization.

- [ ] Represent successful syllable construction separately from tone/prefix output count.
- [ ] Choose composed/decomposed syllable output before arranging the tone mark; preserve the intended consumed-input extent.

**Acceptance A-GLYPH4:** exercise L+V and L+V+T with/without tone, with composed forms supported or unavailable, plus precomposed inputs relevant to the path. A prefixed tone cannot suppress the syllable fallback or cause consumed letters to disappear. Assert exact emitted sequence and source association, not simply nonempty output.

**Must remove:** `LvtGlyphCount == 0` as a proxy for syllable construction success when the count includes a tone prefix.

#### GLYPH5 — Make Hangul transforms source-preserving

**Location:** `kb_execute_op.inc`, Hangul replacement construction and identity path.

- [ ] Inherit effective features and source ownership from the appropriate consumed record, following the existing normalizer's contraction/expansion convention.
- [ ] Keep a tone's own source association. Leave an identity transform in its original record/slot rather than remapping and reinserting it.

**Acceptance A-GLYPH5:** nonzero user/source IDs and visible per-source feature overrides survive composition, decomposition, tone movement, and unchanged input. Emitted glyphs follow the declared ownership convention; unchanged input retains its identity without a delete/insert cycle. Combine these cases with GLYPH4 rather than implement parallel Hangul builders.

**Must remove:** zero-source/zero-config replacement construction and delete/reinsert work on the unchanged fallback.

#### GLYPH6 — Keep semantic syllabic position canonical

**Location:** `kb_script_shape.inc`, Indic sort-key construction, attachment grouping, and restoration.

- [ ] Move temporary composite ordering into an explicit key lifetime. Prefer reuse of the existing `SortKey` field before its later GPOS assignment if all consumers and key-width requirements permit it.
- [ ] Make sorting and attachment grouping read the explicit key while canonical syllabic position remains meaningful. Do not add another full glyph representation.

**Acceptance A-GLYPH6:** left-matra reversal, equal-key stability, consonant/matra attachment groups, and `EndCluster` behavior match the semantic control. A work probe records no full-cluster shift-back/restoration walk. Subsequent GPOS receives freshly assigned logical-order keys, not stale script keys. Report any glyph-record size change; increasing every record is not the default solution.

**Must remove:** temporary packed-key ownership of `SyllabicPosition` and the full restoration pass.

#### GLYPH7 — Enforce Myanmar non-cluster progress

**Location:** `kb_script_shape.inc`, Myanmar non-cluster traversal.

- [ ] Establish reachability of an OTHER/out-of-range syllabic-class glyph after the preceding stages.
- [ ] Advance/consume/exit explicitly, matching the intended sibling-script non-cluster behavior rather than applying an arbitrary iteration limit.

**Acceptance A-GLYPH7:** a reachable witness terminates, preserves non-cluster glyphs, and proceeds into the following valid cluster. Empty/end-of-range input terminates correctly. Use an external timeout only to detect the pre-fix hang, not as the production repair.

**Must remove:** a reachable loop body that can neither advance nor change its predicate.

#### MAP14 — Make cluster restart and unwind one transition

**Location:** `kb_text_shape.h`, cluster execution checkpoint and range restoration.

- [ ] Save instruction position, feature cursor, and sequential lookup cursor in one explicit checkpoint.
- [ ] Share the range-unwind exit for success/error transitions without replacing specialized shapers with a generic framework.

**Acceptance A-MAP14:** consecutive clusters restart with identical intended feature/lookup cursors; the second cluster is not shaped using the first cluster's final cursor. Inject an error during cluster execution and verify range restoration, safe destruction, and a subsequent successful operation under the API's recovery contract. Cluster mutations preserve the outside suffix.

**Must remove:** independently maintained checkpoint locals and duplicate unwind loops for the same transition.

### P4 — Compiler model, configuration work, and scope

#### COLD1 — Decode related predicate sources together

**Location:** `kb_context_compile.inc`, `ContextPredicate` and source/predicate interning.

- [ ] Enumerate each coverage/class source once to construct its requested related memberships, instead of scanning every font glyph independently for each value/index.
- [ ] Preserve class-zero complements, coverage intersections, boundary-symbol behavior, and bitset interning.

**Acceptance A-COLD1:** format-1 coverage-index rules, equivalent class/coverage rules, implicit class zero, and glyphs outside explicit class ranges retain exact matching behavior. A repeated-source fixture records one source enumeration per distinct source rather than one whole-glyph search per requested value. Report global symbol-refinement work separately; do not claim that necessary pass vanished.

**Must remove:** full-domain source searches repeated only because the coverage index or class value changed.

#### COLD4 — Analyze one model and emit final storage once

**Location:** `kb_context_compile.inc`, `kb_context_fuse.inc`, shared compiler definitions in `kb_compiled_context.h`.

- [ ] Retain parent lookup, outcome/rule identity, global predicate identity, source domain information, and ordered rule relationships when first known.
- [ ] Build reusable candidate lists/dispatch facts once; derive counts/layout from those facts and emit from them without repeating identical membership evaluation.
- [ ] Let fusion query retained rules and dispatch views directly. Delete the complete intermediate contiguous cache and metadata recovery from it.
- [ ] Perform one final contextual-cache packing/ownership step after analysis; release all scratch on success and every failure path.
- [ ] Count and eliminate repeated identical fusion-source/domain/child-map analysis where retained facts suffice. `FusionConstant` revisits glyph domains and child coverage; `FusionWinner` traverses symbol/program/candidate/test combinations. Removing `Raw` alone does not remove this work. Name each remaining proof pass and its growth dimensions; do not silently drop previously supported fusions or change priority to make compilation faster.

**Acceptance A-COLD4:** existing priority, zero-record blocker, wide-probe, mask-width, sparse-program, contextual GPOS, extension, and fusion-guard witnesses pass on the final resident cache. Allocation/work evidence shows zero complete intermediate-cache allocation and zero bytes copied from such a cache. Fusion does not recover relationships that collection already stored. Distinguish legitimate scratch payload construction and final emission from prohibited intermediate serialization. No final pointer references freed build storage; injected failures leave balanced ownership.

Final-cache witnesses must include empty dispatch/programs, boundary symbol zero (without confusing live glyph ID zero with a tombstone), tautological padding, candidate counts immediately around 4/8-lane boundaries, first/last bucket masks, compact/wide probes, and compact outcome-width limits. Exercise the final compact test's 32-bit gather overread guard under AVX2, not merely the pre-pack allocation. Preserve one explicit invalid-outcome encoding per width.

**Must remove:** `Raw` cache construction/ownership, raw-to-final classifier/table copy plumbing, compact-to-logical decoding used only to recover compiler facts, and duplicate sizing/emission membership scans.

#### COLD3 — Scale cold table interning without runtime indirection

**Location:** `kb_context_fuse.inc`, `PackTable` and final table emission.

- [ ] Replace the fixed 256-chain assumption with a capacity policy appropriate to the distinct table population, reusing the existing cold interning pattern where practical.
- [ ] Reuse or compute hashes during final-byte emission when this avoids re-reading the same bytes; equality still includes byte comparison and the required size/alignment compatibility.

**Acceptance A-COLD3:** equal table payloads can share resident storage; unequal payloads, including a deliberate hash-collision witness, cannot alias incorrectly. Final matching remains direct-table access with unchanged alignment guarantees. Record distinct-table count, capacity, chain/probe lengths, and bytes hashed on small and large fonts. This task is not accepted on the assertion that a larger hash table must be faster.

**Must remove:** the fixed-chain bottleneck and redundant candidate rehashing where a valid final-byte hash is already available. No new runtime interning lookup remains.

#### MAP03 — Build each stage's ordered lookup union once

**Location:** `kb_text_shape.h`, `PlaceShapeConfig` feature/root collection.

- [ ] Accumulate each eligible feature lookup reference once per semantic stage, keyed by native lookup index; combine required/default/filter/skip metadata according to current policy.
- [ ] Emit ascending lookup order from the accumulator. Reuse collected results rather than rerun semantic construction solely to count storage.
- [ ] Keep cheap fixed-memory sizing bounds distinct from actual construction; do not force expensive speculative lookup execution into a sizing call.
- [ ] Prove accumulated stage boundaries and queue/lookup indexes fit their stored widths before publication. Native lookup IDs fitting `u16` does not prove that the sum of selected roots across GSUB/GPOS and repeated semantic stages fits `FeatureStageFirstLookupIndices` or queue membership fields. Widen necessary aggregate fields or return the documented construction error; never truncate. Sizing and placement must agree at the boundary.

**Acceptance A-MAP03:** deliberately unsorted feature lookup lists, duplicate roots across features, required features, optional enables/disables, and nonbinary values produce the correct ordered output. The same root in different semantic stages is not improperly collapsed. A work probe shows no repeated full-feature search for every next minimum root; count-only entry points do not execute shaping probes.

Include a synthetic aggregate crossing the 16-bit stage-boundary limit while each native lookup ID remains valid. This is distinct from the more-than-32-features case.

**Must remove:** next-lowest-root selection by repeated rescanning and duplicate full root analysis for count/fill/placement.

#### MAP04 — Do not truncate eligible font features at the override limit

**Location:** `kb_text_shape.h`, baked-feature enumeration.

- [ ] Remove the use of `KBTS_MAX_SIMULTANEOUS_FEATURES` as a limit on eligible language-system font features.
- [ ] Stream references into MAP03's accumulator or size temporary storage from validated font counts. Keep the separate public override-stack limit explicit.

**Acceptance A-MAP04:** a valid language system with more than 32 eligible features can execute a visibly affecting feature beyond entry 32, including explicit enable and required-feature cases where applicable. No silent partial feature list is published. Large malformed counts fail through validation rather than memory corruption.

**Must remove:** the silent `BakedFeatureCount` truncation and the fixed baked-feature array if its only justification is the public override limit.

#### COLD5 — Separate proof lifetime, construction peak, and resident bytes

**Location:** `kb_gsub_plan.inc`, `Parts[5..7]` planning workspace; config size/place/create paths in `kb_text_shape.h`.

- [ ] Make proof arrays construction-only and reuse dead scratch by liveness. No resident plan field may require their contents after publication.
- [ ] Size resident admission rows by actual stage stride rather than retaining the lookup-count upper-bound layout.
- [ ] For self-owned construction, retain only final storage. For caller-owned placement, keep peak required capacity honest; overlay only when lifetimes provably do not overlap.

**Acceptance A-COLD5:** requested resident allocation bytes decrease by the removed proof/over-reservation payload on a fixture that exposes it. Report constructor peak separately. A caller-owned buffer of the advertised capacity succeeds with untouched canaries; insufficient capacity fails safely without heap fallback. Sizing does not execute contextual localization. Existing config-time contextual `locl` behavior still works. Do not report a smaller logical struct as physical memory saved while its backing allocation remains unchanged.

**Must remove:** proof-only resident ownership, stale proof pointers, and unconditional upper-bound admission reservation in final owned configurations. A genuinely necessary fixed-memory construction peak is not falsely listed as eliminated.

#### COLD6 — Build admission in its storage order

**Location:** `kb_gsub_plan.inc`, default/possible admission aggregation; related matrix production in `kb_text_shape.h`.

- [ ] Aggregate glyph-major or in bounded tiles once the selected stage mapping exists.
- [ ] Where consumers permit, construct lookup eligibility and selected stage admission in the same glyph-row traversal using shared lookup summaries. Remove only intermediate matrices that no remaining consumer needs.

**Acceptance A-COLD6:** default, disabled, explicit-enabled, required, and nonbinary paths preserve admission and final output, including later writes enabling candidates. Work-count evidence shows the removed lookup-major scatter or repeated row traversal. Keep config construction work separate from warm matching; do not weaken live checks to simplify cold aggregation.

**Must remove:** full glyph-row scatter repeated once per selected lookup where the same result can be aggregated in row order.

#### P4-SCOPE — Derive runtime bounds from reachable programs

**Location:** configuration planning and scratch sizing in `kb_text_shape.h`, `kb_gsub_plan.inc`, and shared compiled program metadata.

- [ ] Derive the relevant program closure from permitted roots, nested targets, and preparation probes, with table identity and cycle-safe visitation.
- [ ] Compute contextual window maxima and applicable plan metadata from that closure, including SIMD guards and config-time probe requirements.
- [ ] Keep the one global symbol domain. Do not add a per-script symbol mirror or claim that all font-global symbol-index costs have disappeared.

**Acceptance A-SCOPE:** the Latin/unrelated-large-Arabic witness from section 2.4 has unchanged Latin output and no Arabic-only lookup execution or window inflation. Shared/default/nested/probe references and later explicit enables remain functional and correctly sized. A configuration with no reachable contextual program does not allocate a font-wide contextual window merely because another script has one.

**Must remove:** global maximum contextual-window inheritance when selected reachability proves a smaller requirement. Mandatory semantic lookup-stage barriers remain.

The selected window bound must be published by the configuration/plan and consumed by **both** scratch size and placement paths, replacing their current reads of `Font->CompiledContexts->WindowCapacity`. Keep font-wide maxima only if an actual font-level consumer needs them. Shared font compilation and a small config-local execution window are compatible.

#### LUT01 — Compile repeatedly consumed font glyph facts

**Location:** `kb_text_shape.h`, `kbts__GlyphClasses`, `kbts__InitializeGlyph`, `kbts__GsubMutate`; font compilation and destruction.

**Source finding:** `GlyphClasses` reopens GDEF and performs ClassDef lookup(s); initialization calls it for mapped glyphs and `GsubMutate` calls it for substitutions. The glyph-symbol classifier is already compiled, but these adjacent immutable facts are not. No speedup is claimed before measurement.

- [ ] Compile glyph class and mark-attachment class from validated GDEF once per font; consume the result directly during initialization and mutations. Reuse the canonical glyph-class fields; do not materialize font-specific full `kbts_glyph` templates and copy them into every record.
- [ ] Prefer a direct per-glyph class table with the existing narrow class fields; reuse an existing exact paging convention only where it avoids substantial empty storage without a new decode/translation layer. Record bytes and dependent accesses; defer compression experiments. Missing GDEF and absent class definitions require explicit semantics: the initializer currently derives fallback base/mark class from Unicode properties, whereas substitution uses GDEF lookup results. Do not erase that distinction with a glyph-ID-only fallback LUT or an unproved identity shortcut.
- [ ] Avoid recomputing ID-derived fields when a mutation provably leaves them unchanged, but still apply observable generated/ligature flags and required admission repair. A same-ID substitution is not necessarily a semantic no-op; prove field validity before skipping writes.

**Acceptance A-LUT01:** GDEF formats 1/2, implicit class zero, mark attachment classes/filtering, absent GDEF, out-of-range IDs, and same-ID/changed-ID actions preserve exact output and live classification behavior. After font compilation, warm initialization/substitution performs no GDEF ClassDef search for cached facts. Report resident bytes, compilation work and ID-derived loads; fixed/native-blob compilation and allocation failure retain their contracts. Do not claim coverage/mark-filter membership searches disappeared merely because glyph classes were cached.

**Must remove:** repeated GDEF class decoding on the migrated warm paths and superseded lookup/cache ownership; retain Unicode-derived fallback logic that is not font-invariant.

#### LUT02 — Share useful immutable planning summaries

**Location:** `kb_gsub_plan.inc`, `GsubPlanDomains` and final `FirstSymbols` construction; compiler analysis/emission.

**Source finding:** config construction scans program candidates/tests and the full symbol domain to derive read/write domains, then scans symbol/bucket dispatch again for `FirstSymbols`. The compiler already knows these relationships. COLD4's compiler-only reuse does not by itself stop rediscovery for every configuration.

- [ ] Produce reusable font-invariant per-lookup analysis and nonempty first-symbol information while compiler facts are live; configurations select/aggregate it without decoding compact runtime tests back into logical predicates.
- [ ] Retain only summaries with actual config/runtime consumers. A compact reusable summary is not proof-only workspace; retaining every temporary domain/candidate array is prohibited. Configuration-dependent feature masks, stage placement and closure remain configuration-owned.
- [ ] Measure summary construction/resident bytes against repeated configuration cost. Use the existing symbol domain and flat lookup identity; no script remap layer, duplicate per-font classifier, or lazy mutable cache.

**Acceptance A-LUT02:** multiple script/language/feature configurations over the same font preserve ordered plans, blockers, write/read dependencies and backtrack guards. Count candidate/test/bucket/symbol visits separately for the first font build and subsequent configs. Repeated font-invariant analysis disappears; necessary config union/proof work is explicitly identified. Resident and peak accounting includes every retained summary.

**Must remove:** compact-test-to-logical reconstruction and repeat full-domain discovery in each configuration for facts now provided by immutable font summaries.

### P5 — Execution workspace and minimum index work

#### MAP13 — Reuse high-level execution scratch

**Location:** `kb_text_shape.h`, `ShapeRun`, context reset/destruction, scratch initialization/binding.

- [ ] Reuse context-owned scratch capacity rather than allocate a new independent scratchpad for every run. Prefer one reusable active workspace with explicit configuration rebinding/growth; keep low-level caller-owned scratch supported.
- [ ] Separate execution lifetime from prepared input, canonical feature state, and public output lifetime. Reset all configuration-dependent cursors, rows, buckets, and range state at the appropriate boundary.

**Acceptance A-MAP13:** actual `ShapeBegin`/input preparation/`ShapeRun` use with many short runs has memory bounded by the prepared input, distinct persistent configs/feature sets, and high-water execution capacity—not the sum of each run's scratch allocation. After warming a bounded set of configurations and maximum run size, repeated same-bound workloads issue no new execution-scratch allocations. Alternating configurations, empty runs, growth, errors, and subsequent reuse remain correct. Pending spans are not invalidated by scratch reclamation; borrowed results obey their existing lifetime.

**Must remove:** one newly initialized, independently retained execution scratchpad per high-level run.

#### RUN01 — Initialize index storage once when it becomes readable

**Location:** `kb_gsub_stream.inc`, stream growth and row admission.

- [ ] Initialize validity/presence metadata eagerly, copy live rows, zero their new tails, and initialize dormant rows at first admission.
- [ ] Preserve zero SIMD/word guards and no-read-before-initialization across growth, slot reuse, and configuration changes.
- [ ] Use RUN07's actual row-family consumers/layout when deciding what exists at all; lazy initialization of a needlessly allocated dense matrix is only a partial improvement.

**Acceptance A-RUN01:** fresh allocation, growth with admitted rows, first admission of dormant rows, and slot counts around word/SIMD boundaries preserve exact output and safe loads. Poison-backed scratch or an appropriate uninitialized-read diagnostic demonstrates initialization coverage; ASan alone is not evidence against uninitialized reads. A byte-write probe shows unused capacity is not cleared twice.

**Must remove:** blanket zeroing immediately overwritten by live-row copies and repeated zeroing of still-unused rows.

#### RUN02 — Drop redundant exact-position tests in logical traversal

**Location:** `kb_gsub_stream.inc`, unordered live traversal.

- [ ] Remove the exact symbol-position membership probe when traversal already supplies the live slot and its current symbol.
- [ ] Retain first-symbol eligibility, live filters, and the exact index for ordered bitmap consumers.

**Acceptance A-RUN02:** deletion/reuse, reordering, and symbol-changing substitution produce identical selected candidates and output in unordered traversal. A work probe records no exact-position recheck in that path. Ordered index consumers continue passing word-boundary and mutation witnesses.

**Must remove:** the redundant probe, not the shared symbol index or mutation hooks.

#### RUN03 — Repair only observable stage rows after deletion

**Location:** `kb_gsub_stream.inc`, deletion/index repair and stage interval ownership.

- [ ] Limit native-row clearing to current/future observable stages, narrowing to the active operation interval only where reinitialization proves that safe.
- [ ] Maintain exact symbol removal and immediate current-stage visibility; document the completed-row reset boundary.

**Acceptance A-RUN03:** ligature component deletion, nested deletion, recycled slots, current-stage continuation, and later dependent lookups remain correct. Subsequent operation/reuse cannot resurrect a deleted candidate from stale completed rows. A deletion probe records zero writes to completed native stage rows while retaining all currently observable repairs.

**Must remove:** unconditional iteration over already completed admitted stages for every deletion.

#### RUN04 — Gather only consumed backtrack data

**Location:** `kb_context_match.inc`, backtrack window gather and consumers.

- [ ] Remove backtrack glyph-reference/offset stores and distance maintenance that no consumer reads.
- [ ] Retain forward action/fusion metadata and symbol gathering. Do not add a replacement array-of-structs window.

**Acceptance A-RUN04:** wide backtrack, ignored glyphs, range boundaries, and fused actions retain exact behavior. Consumer inspection plus a disposable write-count probe shows symbols-only backtrack gathering and no removed-field reads. Forward targeting still passes live-action cases.

**Must remove:** unused backward `Glyphs`/`Offsets` writes and backward distance arithmetic, not metadata required on the forward side.

#### RUN07 — Scope index work to the active execution interval

**Location:** `kb_gsub_stream.inc`, `ReserveGsubStream`, `RunGsubStages`, `GsubBuildIndexes`, candidate iteration and growth; stable-slot active ranges.

**Source finding:** `ReserveGsubStream` derives symbol-row allocation from the whole plan (`NativeCount != GsubLookupCount`), not `IndexSymbols`; native rows reserve every native lookup. `RunGsubStages` chooses native indexing using whole-storage `LiveCount > 64`, even when the active cluster contains one glyph. Initialization then clears metadata/full physical row strides for that interval. This is separate from RUN01's double clearing and P4-SCOPE's contextual windows.

- [ ] Bind/grow index row families for actual consumers. A native-only interval must not first-allocate unused symbol-position rows just because another interval contains compiled contexts. Reuse already warmed capacity without clearing or copying dormant families; rebinding must check layout/config identity as well as glyph capacity.
- [ ] Base small-range work decisions on the active range, not outside live glyphs. Reuse a trustworthy count if already available; otherwise a bounded probe up to the decision threshold is preferable to another unconditional full-range count. Do not add per-mutation counters with greater cost than the decision they serve.
- [ ] Separate current initialized word extent from retained capacity where this reduces repeated clears/copies. Absolute stable-slot coordinates remain the index key; a tiny cluster at a high slot cannot be sized as if it occupied slot zero. Prove bounds and zero tails across range changes, tombstone reuse, growth, and config rebinding.
- [ ] Account for the retained dense cost explicitly: symbol rows scale as `SymbolCount * ceil(slot_capacity/64) * 8`, native rows as `NativeCount * ceil(slot_capacity/64) * 8`, plus metadata. Keep the simplest direct allocation layout that omits unused work; do not hide font-global symbol costs behind the smaller contextual window claim.

**Acceptance A-RUN07:** exercise the same one-glyph native interval alone and inside a larger storage/range, including high physical slots and a large-then-small reuse sequence. Outside glyphs must not force an unnecessary full index build. Exercise native-only → contextual → native-only intervals and alternate configurations with equal slot capacity but different symbol/stage layouts; no stale row, wrong stride, hidden allocation after bounded warmup, or read of uninitialized capacity. Record first-allocation bytes, initialized bytes, row copies and scans, not just steady-state allocation count.

**Must remove:** unused-family first allocations, whole-storage threshold decisions for small active ranges, and capacity-sized work not required by the current readable extent. Exact live index semantics and consumer-required retained capacity remain.

#### CACHE01 — Verify locality without rebuilding a glyph mirror

**Location:** `kb_glyph_storage.inc`, canonical `kbts_glyph`/slot links, matching and ordered/unordered index consumers.

**Source finding:** on the reviewed x86-64 build each reserved slot costs 104 bytes of glyph record, 8 bytes of links and 4 bytes of free-slot storage. Logical traversal follows a separate dependent link load; one nonmonotonic edit clears `Ordered` until storage reset. Sentinels remove suffix moves but do not remove this load dependency or the dense bitmap footprint.

- [ ] Inventory fields actually loaded/stored by initialization, gather, substitution, sorting, GPOS and output; record `sizeof`/offsets and touched cache lines. Preserve directly consumed cached fields; remove dead fields/stores rather than adding a hot/cold synchronization layer.
- [ ] Exercise ordered input, reordered/recycled slots, long combining sequences, and large-then-small storage reuse. Count ordered/unordered path use and link/index traffic where needed to substantiate a removal. Do not add order-restoration scans or revive the rejected contiguous traversal experiment in this structural cutover.
- [ ] Keep surviving references stable and tombstone/free-list state unambiguous. Grow storage only when required; no glyph-suffix memmove on deletion, insertion, reorder, or range exit. A sentinel must be checked/mapped before dereference unless a real addressable guard record exists.

**Acceptance A-CACHE01:** document the final canonical working set and dependent accesses, including tiny runs and fragmentation. The existing layout may remain; no speculative record-layout rewrite, alternate traversal engine, or PMU campaign is required. Any field/layout change migrates all consumers and preserves sources, attachment identities, allocation alignment and public iteration. Zero-copy is not presented as proof of cache locality or a speed win.

**Must remove:** demonstrated dead canonical/run bookkeeping and any trial mirrors/translation or speculative order-restoration machinery. In particular, `OutputFirst`, `OutputEnd` and `OutputGlyphCount` currently have writes but no output consumer; remove them with their obsolete stores unless the implementation inventory establishes a real existing consumer.

### P6 — Queue ownership, ordering, and fallback reuse

#### MAP06 — Use one queue representation through sort and execution

**Location:** `kb_text_shape.h`, GPOS bucket insertion/removal/sorting/execution; scheduling fields and consumers in glyph storage/actions.

- [ ] Replace block-to-flat-to-block conversion with one queue representation shared by insertion, ordering, and consumption. Allocate for active queues, not a full glyph-capacity array for every font lookup.
- [ ] Use contiguous active-bucket vectors plus reusable same-entry-type merge workspace and index-based membership as the default design. Growth must not invalidate membership. Sorting changes indices: publish membership once after the final order, before actions can observe it. Do not call a vector index stable across sorting without this update.
- [ ] Keep one active queue ownership model; do not build competing vector/intrusive implementations or a benchmark-selected runtime mode. Change the default only for a concrete correctness/ownership constraint, not a speculative timing advantage.
- [ ] Preserve ordered fast paths, deletion/removal semantics, nonbinary values, continuation, and live rebucketing. Return drained storage to reusable execution ownership rather than retaining a whole-run vector for every visited lookup.
- [ ] Use tombstones/sentinels for queue removal without shifting the remaining queue on each deletion. Reserve the deletion key outside valid sort-key values; clear membership before recycling an entry and publish valid surviving handles after any sort/compaction. Queue-local sorting may move compact scheduling entries, never the canonical glyph suffix.

**Acceptance A-MAP06:** ordered, disordered, duplicate-key, removed/tombstoned, growth-during-scheduling, and live-rebucket cases produce exact GPOS results with no lost/duplicate execution. No reference into a movable vector survives a mutation that invalidates it. Queue conversion bytes into/out of linked blocks are zero. Sorting may use merge scratch or one required membership publication; report those bytes rather than relabel them zero-copy. Measure warm allocations, retained/peak bytes, and ordered/disordered cost on many tiny and large queues. Only the selected representation remains.

Include interior tombstones followed by growth, live rebucketing and storage reuse; verify no stale unbucket writes, duplicate execution, or valid key equal to the deletion sentinel. Report tombstone visits and compaction/handle-publication bytes even when the ordered path avoids sorting.

**Must remove:** the linked-block/flat-array round trip, copy-back into blocks, conversion-induced pointer repairs, and competing replacement prototypes. Sorting scratch contains the same compact entry type; it is not a second data model.

#### GLYPH1 — Make attachment marking single-owner

**Location:** `kb_glyph_actions.inc`, `AttachGlyph` and mark-to-ligature attachment.

- [ ] Make `AttachGlyph` own the parent/child interval's `NO_BREAK` propagation.
- [ ] Delete the duplicate mark-to-ligature walk after proving the same endpoint semantics under immutable GPOS order keys.

**Acceptance A-GLYPH1:** direct mark-to-ligature attachment, reattachment, non-topological attachment order, and intervening glyphs preserve positions and break flags. A work probe shows one traversal for that interval, not two. Later attachment-bound optimization remains correct.

**Must remove:** the second interval marking walk after `AttachGlyph`.

#### MAP15 — Find an insertion position, then splice once

**Location:** `kb_text_shape.h`, linked glyph insertion sort; callers in script/normalization ordering.

- [ ] Search predecessors read-only, then perform one splice at the final position.
- [ ] Preserve stable ties, range boundaries, and surviving slot/reference identity; use GLYPH6's explicit key where applicable.

**Acceptance A-MAP15:** already sorted, reverse-sorted, duplicate-key, and long combining sequences retain exact glyph/source order and stable slots. The moved glyph is spliced at most once per insertion, not once per inversion. Comparison complexity need not change; report reduced link writes without claiming an asymptotic improvement.

**Must remove:** repeated relinking of the same glyph during destination search.

#### MAP08 — Reuse exact fallback evidence without changing priority

**Location:** `kb_text_shape.h`, grapheme font coverage selection and glyph preparation.

- [ ] Reuse exact cmap/normalization/coverage facts within the current preparation lifetime instead of rediscovering them between fallback selection and initialization.
- [ ] Carry already-computed exact mapping/coverage evidence to its immediate preparation consumer using the existing input ownership path. Do not add a repeated-grapheme memo/hash cache in this cutover; speculative caching is later tuning, not necessary to eliminate same-lifetime rediscovery.
- [ ] Preserve priority, missing-glyph behavior, normalization-sensitive coverage, and input ownership. A hash collision must not equate different graphemes.

**Acceptance A-MAP08:** repeated and changing graphemes across fonts preserve the selected highest-priority supporting font and exact output. Changing font priority invalidates reused selection; joiner/mark/normalization-sensitive sequences do not pass on nominal coverage alone. Count actual cmap/coverage/decomposition work before/after on single-font and fallback-heavy input. Exact same-lifetime reuse is required, without an additional general memo or prepared-glyph representation.

**Must remove:** exact duplicate coverage/mapping work made redundant by retained evidence. Do not replace it with previous-font-first selection or an approximate coverage shortcut.

### P7 — Integrated acceptance

- [ ] Every `A-<review ID>`, `A-SCOPE`, `A-LUT01`, `A-LUT02`, `A-RUN07` and `A-CACHE01` has the behavioral/structural evidence its contract requires. Suspect behavior follows section 1; source-confirmed redundant work is not closed merely by calling it unmeasured.
- [ ] All affected exported API callers, internal fixtures, size/place/create paths, benchmarks, and destruction paths use the final contracts. Use symbol-aware references when a language server is available; otherwise perform a scoped complete caller inventory.
- [ ] Final resident programs, not a temporary compiler view, pass the matcher/action correctness suite on SSE2 and AVX2.
- [ ] Public high-level shaping passes feature-stack, multi-run scratch, fallback, and cross-`ShapeBegin` cases absent from the direct benchmark.
- [ ] All required representation/pass removals have source evidence and relevant work-count evidence. No legacy fallback or renamed copy of a removed bridge remains.
- [ ] Final allocation failure/odd-address/fixed-memory witnesses and full project validation complete after integration.
- [ ] Allocation/lifetime and necessary work-count evidence follow section 6. All compilation occurs before execution; no timing threshold or recovery percentage blocks this structural acceptance.

## 5. What NEEDS to go

These are structural removals, not a blanket instruction to delete whole files. Remove now-unused fields/helpers/types and all callers of obsolete paths as their replacement is accepted. Do not keep aliases, deprecation shims, disabled legacy branches, or two production implementations.

| Removal ID | Required removal | Owner / evidence |
| --- | --- | --- |
| R01 | Complete intermediate contiguous `Raw` contextual cache and its allocation/free/copy plumbing | COLD4; no intermediate-cache allocation or transport bytes |
| R02 | Recovery of parent/outcome/global-predicate facts by decoding the intermediate runtime format | COLD4; retained build metadata feeds proof directly |
| R03 | Duplicate dispatch membership evaluation solely for count versus fill | COLD4; one retained candidate result serves both |
| R04 | Whole-glyph predicate-source search repeated for each index/class from the same source | COLD1; grouped source enumeration counts |
| R05 | Independent repeated extension walks and per-run direction decoding | COLD2, RUN06; validated descriptor consumers |
| R06 | Fixed 256-chain table-interning assumption and redundant hashing of already-hashed final payloads | COLD3; measured interner behavior |
| R07 | Proof-only resident plan storage and unconditional final lookup-upper-bound admission reservation | COLD5; actual allocation accounting |
| R08 | Lookup-major admission scatter where row-major aggregation replaces it | COLD6; config construction work witness |
| R09 | Repeated next-minimum root scans and silent 32-font-feature truncation | MAP03, MAP04; ordered-union and feature-beyond-limit witnesses |
| R10 | Font-global contextual-window sizing for configurations with a smaller reachable requirement | P4-SCOPE; unrelated-Arabic window witness |
| R11 | Blanket index clearing followed by copy/first-use clearing of the same unused regions | RUN01; initialization/write counts |
| R12 | Exact symbol-position rechecks in live logical traversal | RUN02; path-specific probe count |
| R13 | Deletion repair of already completed native stages | RUN03; stage-qualified repair counts |
| R14 | Unused backtrack glyph/offset writes and distance arithmetic | RUN04; consumer inventory and write counts |
| R15 | Silent anchor relocation and mismatched action-target skip policy | RUN05, GLYPH3; end-to-end target identity |
| R16 | Nested GPOS subtable loops that ignore successful application | GLYPH2; overlapping-child witness |
| R17 | Duplicate mark-to-ligature attachment interval marking | GLYPH1; single-owner interval traversal |
| R18 | Tone-inclusive output count used as Hangul syllable-success state | GLYPH4; tone/fallback witness |
| R19 | Zero-owned Hangul replacement metadata and identity-path delete/reinsert | GLYPH5; source/features/slot witness |
| R20 | Temporary packed sort-key meaning of canonical `SyllabicPosition` and its restoration walk | GLYPH6; ordering and pass-count evidence |
| R21 | Reachable non-progressing Myanmar loop | GLYPH7; termination and next-cluster witness |
| R22 | Unchecked directory reads and wrong-domain class guards | MAP01, MAP02; malformed/boundary sanitized cases |
| R23 | Inconsistent raw allocation alignment/free conventions and null-result dereferences | MAP05, MAP07; allocator witnesses |
| R24 | GPOS queue block-to-flat-to-block conversion and conversion-induced pointer repair | MAP06; zero block conversion bytes |
| R25 | Original-stack-prefix snapshots, broken pop shift, and stale success/invalidation state | MAP09, MAP10; visible feature-state transitions |
| R26 | Persistent cache identity based on recycled scratch snapshot addresses | MAP11; repeated `ShapeBegin` witness |
| R27 | Post-hit cache-block scans, per-codepoint config resolution and execution-time config compilation | MAP12; preparation/execution counters and cache work counts |
| R28 | Independently accumulated execution scratchpad allocation per high-level run | MAP13; bounded high-water memory and warm requests |
| R29 | Scattered cluster checkpoint ownership and duplicated unwind paths | MAP14; transition/error witness |
| R30 | Multiple splices per glyph while finding its insertion destination | MAP15; link-write count |
| R31 | Exact fallback/mapping rediscovery replaced by proven shared evidence | MAP08; coverage-query count and priority witness |
| R32 | Repeated warm GDEF class decoding for immutable cached glyph facts | LUT01; class-search counts and fallback/class witnesses |
| R33 | Per-config reconstruction of retained font-invariant planning facts | LUT02; config candidate/test/bucket/symbol visit counts |
| R34 | Unused index-family first allocation and whole-storage small-range decisions | RUN07; active-range/row-family allocation and initialization evidence |
| R35 | Dead run-output counters/stores and rejected locality prototypes | CACHE01; consumer inventory and final canonical-path measurements |

### Things that MUST NOT be deleted in the name of simplification

- Semantic GSUB/reordering/GPOS stage barriers and native lookup/subtable/rule priority.
- Required features, override-enabled reachable lookups, nested action targets, and configuration-time shaping probes.
- Class-zero complements, coverage intersections, boundary-symbol handling, and SIMD guards.
- Exact symbol indexes used by ordered execution and immediate mutation hooks needed by live consumers.
- Forward window metadata used by actions/fusion, original allocation bases, and real construction-peak accounting.
- Prepared input still referenced by pending runs, persistent canonical feature state, and output still within its documented borrowed lifetime.
- The immutable upstream benchmark vendor, historical approved plans, or user-authored review answers.
- Necessary count/layout dependencies, legitimate sort workspace, or final requested output projection simply to claim a literal one-pass pipeline.

Automatically inserted `REVIEW[...]` comments should be resolved into concise invariants or removed when their finding is closed. User-authored `ANSWER` annotations are not included in that automatic removal; preserve or explicitly incorporate their content without silently discarding it.

## 6. Validation protocol

### 6.1 Behavioral authority and regression policy

Use deterministic, minimal fixtures for uncertain semantics and real errors. A confirmed bug should have a regression that fails before the fix and passes afterward where practical. Extend existing behavioral coverage rather than add tests that assert field copies, helper forwarding, exact code structure, or default wording.

Source-removal checks and work counters are acceptance evidence, not permanent source-text unit tests. Keep compiler tests exercising final resident programs. Keep high-level API cases separate from direct matcher tests where their lifetimes differ.

Preserve unchanged outputs against the pre-change owned control and existing upstream comparison. A deliberate correctness fix may differ from upstream; establish its expected output from the supported specification/contract and a concrete fixture. Report the mismatch rather than hide it with a whitelist, relaxed comparator, or blanket golden update. Zero unexplained differences is the correctness gate. If a verifier stops on an intentional difference, the remaining corpus still must be exercised and reported; a partial run is not a pass.

### 6.2 Existing commands

These commands exist in `justfile`; they are future implementation validation commands, not claims of execution when this plan was written.

Focused contextual regressions under ASan/UBSan:

```sh
just _test kb_context_test
```

Exact differential coverage on both supported benchmark ISAs:

```sh
just shaping-verify sse2
just shaping-verify avx2
```

Sanitized differential runs and deterministic stress reports:

```sh
SHAPING_SANITIZE=1 just shaping-verify sse2
SHAPING_SANITIZE=1 just shaping-verify avx2
SHAPING_SANITIZE=1 just shaping-stress sse2 --report /tmp/shaping-simplification-sse2.jsonl
SHAPING_SANITIZE=1 just shaping-stress avx2 --report /tmp/shaping-simplification-avx2.jsonl
```

Final integrated project validation, once after all edits are integrated:

```sh
just check
```

The shaping benchmark/stress recipes require the HarfBuzz development package. `just check` does not. Stress currently uses its fixed font matrix under `/usr/share/fonts/noto` by default; record actual resolved fonts and missing prerequisites rather than claiming coverage of unavailable fonts.

Optional commands retained for **later timing/tuning**, not current acceptance:

```sh
just shaping-bench avx2 --engine owned --size 4096 --corpus code --iterations 1000 --batches 5
just shaping-bench avx2 --engine owned --size 4096 --corpus prose --iterations 1000 --batches 5
just shaping-core avx2 --engine owned --size 4096 --corpus code --iterations 1000 --batches 5
just shaping-profile avx2 --engine owned --size 4096 --corpus code --profile detail
```

Set the same available CPU explicitly with `--cpu` for paired measurements. Preserve baseline binaries before invoking recipes again: the recipes overwrite `/tmp/rwmd-shaping-<mode>-<isa>`.

### 6.3 Coverage gaps that MUST be exercised separately

The current adapter in `bench/shaping_kb.inc` creates/reuses low-level scratch and calls `kbts_ShapeDirect`. It does not establish acceptance for context creation, feature push/pop snapshots, cross-`ShapeBegin` identity, fallback, or per-run high-level allocation.

- [ ] Run an actual high-level scenario that creates a context, pushes fonts/features/text, shapes all prepared runs, reuses the context for changed input/state, consumes public results, and destroys it.
- [ ] Exercise many short same-config runs and alternating configurations with a counting/failure/odd-address allocator. Separate new immutable config/feature-set allocations from reusable execution scratch.
- [ ] Exercise actual fallback with repeated graphemes and priority changes. The direct single-font corpus does not cover this.
- [ ] Exercise Hangul and Myanmar via fixtures/public-API smoke. The current benchmark CLI accepts only `Latn`, `Arab`, `Hebr`, `Deva`, `Beng`, and `Thai`; do not invent unsupported `--script` command examples or claim those two scripts were covered by the existing matrix.
- [ ] Exercise construction failures, exact advertised placement capacity/canaries, fixed-memory exhaustion, poisoned/uninitialized scratch, and successful reuse after errors at the relevant API boundary.
- [ ] Exercise one active glyph inside a large storage, high-slot bounded ranges, native-only intervals in mixed configs, and huge-then-tiny reuse. These expose work hidden by freshly cleared direct benchmark input.

Use throwaway scenario drivers for straightforward feature/performance demonstrations. Retain a permanent regression only for a genuine uncertain edge or confirmed bug. No renderer/UI proof is required because editor integration is outside this plan.

### 6.4 Workload coverage and later timing protocol

Exercise applicable behavioral and work-count witnesses across:

- Short, medium, and 4 KiB code and prose, including changing input rather than one indefinitely cached string.
- Correctly tagged Arabic, Hebrew, Devanagari, Bengali, and Thai runs with explicit language/direction and recorded supporting fonts.
- Combining-heavy and reordered clusters; native/contextual, reverse, length-changing, and mark-positioning paths.
- Default, disabled, explicit-enabled, optional, and nonbinary feature modes.
- Many tiny high-level runs, alternating bounded configurations, repeated feature states across `ShapeBegin`, and fallback-heavy graphemes.
- Small fonts and a large multi-script font; repeated-source/interner stress and unrelated-large-context scoping fixtures.
- Existing 64 KiB code/prose cases and 64-byte script cases, plus first-use and repeated-use configuration lifetimes. Use targeted counts/smokes now; the amortized timing study is deferred.

For each accepted change, report the phases it affects: font load/compile, config construction, prepared input, warm shaping core, output consumption, and high-level total. Memory reporting distinguishes requested live payload, resident allocations, constructor peak, retained capacity, raw font bytes, and any RSS observation. Do not call requested payload RSS or hide mandatory consumer packing outside total time.

**Deferred timing protocol, retained for future use:** compare current owned, pre-span and new controls in at least three paired repetitions with alternating order, identical inputs, CPU affinity, compiler/ISA and features. Start with 1,000 iterations and five batches; increase sampling if noisy. Missing pre-span controls limit recovery claims, not this cutover. Keep instrumentation out of production timing; label RDTSCP as TSC ticks, not core cycles.

PMU/cache/branch tuning is also deferred. If later collected, report named events/scope without multiplexing where feasible, pin the same core/CCD, and distinguish observed misses from footprint estimates. No benchmark win is claimed merely from removing copies or reducing allocation bytes.

### 6.5 Acceptance policy: architecture now, timing later

**Decision:** the user explicitly prioritizes “the cleanest, minimal code”, “Minimal operations, minimal memory access”, no data-structure translation, and “all compilation ahead of time”; performance tuning is later work. This supersedes the proposed workload-tradeoff policy and strict 2% timing gate **for this cutover**.

Hard gates now:

1. Correct supported behavior, ownership, failure handling and stable identities.
2. Font compilation at font construction; selected config/feature compilation before execution, with no lazy compiler inside shaping.
3. One canonical glyph representation, one selected queue representation, and no intermediate complete runtime-cache serialization/repacking.
4. Removal of redundant discovery, copies, scans, clears, stale bookkeeping and unused-family allocations, demonstrated with consumer inventories and focused work/byte counts.
5. Simple direct data access and bounded reusable capacity; no speculative compression, memoization framework, or competing optimization prototypes.

Elapsed-time comparisons, PMU studies and percentage gates are not required for completion. This does not authorize needless operations merely because timing is deferred. Conversely, a theoretically smaller instruction count does not justify an extra representation or an elaborate runtime framework.

Historical performance results and the old failed gate in `readme.md` remain historical facts, not newly passed measurements. Existing docs updated after implementation must distinguish structural acceptance under this decision from unperformed performance acceptance. If timing is collected incidentally, report it honestly without blocking the clean cutover or claiming universal recovery.

### 6.6 Evidence record for later review

Use this schema in the implementation result report; do not mark a box complete based only on a code claim:

| Field | Required content |
| --- | --- |
| Task / acceptance ID | Review ID, `P4-SCOPE`, or additional finding; associated removal IDs |
| Disposition | Structurally/behaviorally accepted, retained simple layout with consumer evidence, or disproved suspect |
| Control | Source/executable identity, font hash, exact input and properties |
| Behavioral witness | Expected observable output/error/transition; pre-fix result where applicable; post-change result |
| Command / artifact | Actual command and output/report location, including full mismatch details |
| Structural evidence | Removed owner/representation/pass and remaining intentional dependency |
| Work / memory | Before/after counts with scope and allocation lifetime clearly stated |
| Timing | Deferred; if collected, include scope/variability and avoid unsupported recovery claims |
| Boundary status | Caller migration, fixed-memory/failure behavior, and no surviving obsolete path |

After smoke evidence proves the implementation, update existing `IMPLEMENTATION.md`, relevant `readme.md` shaping sections, and API comments to describe the final model and measured results. Preserve historical results as historical rather than rewriting them as current claims. Remove throwaway probes/drivers after retaining their commands/results; do not leave instrumentation enabled in production builds.

### 6.7 Evidence obtained in this plan review

This is investigation evidence, **not post-refactor performance acceptance**. Only this markdown plan was changed in the repository; no shaping fix is claimed.

Source inspection covered all 34 inline IDs and the compiler/config, canonical storage, matcher, action/script, high-level input/cache, and queue paths. The additional findings are source-grounded; only the following allocation/active-range behavior was exercised dynamically.

A throwaway C driver loaded the bundled JetBrains Mono font, created its Latin/English configuration, called the actual stream reserve/GSUB interval functions, ran `kbts_ShapeDirect`, and destroyed its owned objects. Built with Clang 22.1.8 using `-std=gnu17 -O1 -g -fsanitize=address,undefined` and `-lm`; no sanitizer findings:

```text
glyph_record=104 link=8 free_slot=4 symbols=51 native=36 gsub=191 stages=38
reserve index_symbols=0 requested=44567 symbol_rows=allocated stride=64
reserve index_symbols=1 requested=44567 symbol_rows=allocated stride=64
real_shape error=0 input=11 output=11 live=11 high_water=11
native_interval total_live=1 active=1 indexed=0 requests=0 error=0
native_interval total_live=65 active=1 indexed=1 requests=1 error=0
```

The reserve probe requested 4,096 physical slots on fresh scratchpads with `IndexNative=1`, varying `IndexSymbols`; both allocated 44,567 bytes. The 51 symbol rows alone account for 26,112 bytes, despite one probe disabling their consumption. This proves unused allocation, not that freeing/reallocating between intervals would be faster.

The native-interval probe selected the first native plan node, shaped an active range containing the first `a`, and varied outside storage from zero to 64 further `a` glyphs. One outside-independent active glyph changed from no indexing/allocation to indexing and one allocation. Both operations returned no error. The direct smoke input was `a != b -> c`; its successful execution is a smoke, not differential correctness coverage.

Font: `assets/fonts/JetBrainsMonoNerdFontMono-Regular.ttf`, SHA-256 `f01031f40e48dc29e1112e6b0b0450a2c6cd097f3f35cfff05c55cb311f8034c`.

Probe command: `clang -std=gnu17 -O1 -g -fsanitize=address,undefined /tmp/rwmd-shaping-review-probe.c -o /tmp/rwmd-shaping-review-probe -lm && /tmp/rwmd-shaping-review-probe`. The disposable source/binary are not permanent tests; the scenario and observed output above are the evidence record. No paired timing, PMU result, or all-suspects behavioral pass is claimed by this review.

## 7. Final reviewer checklist

- [ ] Can a maintainer follow font data through collect/analyze/emit without encountering a second complete intermediate runtime cache?
- [ ] Does every retained compiler pass produce a necessary result rather than reconstruct an already-known relationship?
- [ ] Are script reachability, default-disabled overrides, nested targets, and preparation probes represented together without font-global window inflation?
- [ ] Is the retained font-global classifier tradeoff stated honestly, with no claim of eliminating all unrelated-script cold work?
- [ ] Is each immutable object separate from mutable execution state, and does every cached identity outlive its cache entry?
- [ ] Do matching, child targeting, attachment marking, and cluster restoration each have one explicit owner/contract?
- [ ] Are real glyph transformations source-preserving and identity transformations left alone?
- [ ] Does warm high-level shaping reuse capacity rather than accumulate one execution workspace per run?
- [ ] Does GPOS use one queue representation, with growth/sort membership semantics explicit and no block conversion round trip?
- [ ] Are all 34 review findings, reachability, and the four additional findings accounted for with observable evidence?
- [ ] Have every mandatory removal and every preserved invariant been checked against the final integrated code?
- [ ] Are font facts compiled once, useful planning summaries reused, active-range indexes consumer-scoped, and sentinel/guard domains proved?
- [ ] Is the canonical working set/direct access path explicit without a glyph mirror or speculative locality machinery?
- [ ] Is all compilation before execution, and are deferred timing work, untested boundaries and intentional upstream differences reported honestly?

## 8. Grounding references

- Existing implementation and API contracts: [IMPLEMENTATION.md](../../IMPLEMENTATION.md), especially the owned shaping, canonical execution, and compilation lifetime sections.
- Existing validation recipes: [justfile](../../justfile).
- Existing behavioral fixtures: [tests/kb_context_test.c](../../tests/kb_context_test.c).
- Benchmark entry points and limitations: [bench/shaping.c](../../bench/shaping.c), [bench/shaping_kb.inc](../../bench/shaping_kb.inc), [bench/shaping-build.sh](../../bench/shaping-build.sh).
- Prior architectural constraints: [minimum-work-shaping-pipeline-v2.html](minimum-work-shaping-pipeline-v2.html), [compiled-shaping-lut-v2.html](compiled-shaping-lut-v2.html). Historical performance status remains in `readme.md`; section 6.5 records the user's current architecture-first acceptance decision.
- OpenType lookup structure and extension rules: [GSUB specification](https://learn.microsoft.com/en-us/typography/opentype/spec/gsub), [GPOS specification](https://learn.microsoft.com/en-us/typography/opentype/spec/gpos).

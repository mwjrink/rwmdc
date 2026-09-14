# Shaping simplification: implementation and acceptance plan

Status: implemented and accepted after the complete 139-item audit in section 9.6. The original bulk closure was not fully justified; its evidence gaps and required code fixes are resolved. Timing and PMU work remain deferred.

Scope: the owned shaper in `src/lib/kb`, its public construction/shaping paths, and the existing shaping validation adapters. This document turns the 34 inline review findings, the reachability requirement, and four additional source-review findings (`LUT01`, `LUT02`, `RUN07`, `CACHE01`) into required changes, implementation TODOs, observable acceptance criteria, and a removal ledger.

The existing HTML plans remain historical design artifacts. This plan builds on the implemented canonical stable-slot cutover; it does not restart that work or authorize a second shaping engine.

## 1. Objective and completion rule

Simplify by eliminating repeated discovery, intermediate serialization, competing ownership, and work outside the selected execution scope. Preserve shaping semantics and the existing supported API behavior.

**The target is fewer representations and fewer passes over the same facts, not fewer indented lines.**

A task is complete only when its required change, behavioral acceptance, applicable work/memory evidence, caller migration, and associated removals are complete. Compiling successfully is not acceptance. A performance result does not excuse a correctness failure.

Review findings marked `SUSPECT` are source-review findings, not runtime-confirmed failures. Establish a deterministic witness before changing semantics. A suspect may be closed without a behavioral change only with evidence disproving the premise or proving the existing invariant; record that disposition against its ID. This is not permission to waive the mandatory structural end state or quietly omit a finding.

### Required deliverables

- [x] One implemented architecture satisfying section 2, without legacy bridges or alternative production engines.
- [x] A disposition and evidence reference for every review ID in section 4.
- [x] All applicable removals in section 5 completed alongside their owning changes.
- [x] Correctness, allocation/lifetime, and targeted work-count evidence as specified in section 6. Timing optimization and performance gates are deferred.
- [x] Accurate existing API/implementation documentation reflecting the final ownership and sizing contracts.

### Review conclusion and minimum-work contract

The architectural direction is sound: canonical stable slots, immutable font compilation, configuration-owned selection, and reusable execution capacity. **Closing the original comments alone does not remove all unnecessary work.** The additional obligations below cover repeated font-fact decoding, repeated config analysis, interval-insensitive index work, and cache working-set costs. Section 6.7 preserves the pre-implementation review experiments; section 9 records completed implementation evidence.

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

- [x] Preserve the current owned correctness control and unchanged upstream control before recipes overwrite their executable paths. Retain existing pre-span/pre-translation binaries/source identities when available for later timing work; their absence does not block the structural cutover.
- [x] Record source identity, compiler/flags, CPU affinity, input/feature modes, font file hashes, baseline output/checksums, and measurement commands.
- [x] Record the disposition of all 34 review comments. Preserve user-authored annotations; do not treat them as disposable review markers.
- [x] Establish minimal witnesses for suspect behavior using the existing fixture machinery where possible. Exercise public construction or shaping paths when that is where the defect lives.
- [x] Record allocation/work costs for font compilation, config construction, low-level shaping and high-level preparation/execution separately. Do not require elapsed-time measurements at P0.
- [x] Record work baselines: source-predicate visits, repeated dispatch membership tests, intermediate-cache bytes, proof/resident/peak bytes, row clears, completed-stage repairs, backtrack metadata stores, queue conversion bytes, cache searches, and allocator requests.
- [x] Include font-class/lookup decodes, config symbol/bucket/proof visits, active-range versus whole-storage index decisions, index bytes by row family, link loads/writes, tombstone visits, and actual glyph-record/cache-line footprint in the work baseline.

Use the existing profiler where its counters answer the question. Add disposable probes for missing work counts, not a permanent telemetry subsystem. Instrumented work-count runs are not production timing runs.

## 4. Required implementation TODOs and per-finding acceptance

The review ID is the stable identifier. Acceptance labels `A-<ID>` can be used in result reports. Names and line numbers may move; the semantic obligation does not.

### P1 — Font trust boundary and allocation ownership

#### MAP01 — Bound font directory reads before dereference

**Location:** `kb_text_shape.h`, font count/loading and directory parsing.

- [x] Validate the fixed directory header before reading `TableCount`; check TTC directory offsets and nested count/offset arithmetic against the actual available bytes.
- [x] Carry established extents through loading/placement. Do not use an untrusted declared length as proof of the allocation's extent.
- [x] Check offset/length/count arithmetic as integers before pointer formation, including early GSUB/GPOS lookup counting and `maxp` reads that precede compilation. Check scratch/output size accumulation, alignment additions, matrix products, and every narrowing to the public `int` sizes or stored `u32` sizes; no wrapped advertised capacity.

**Acceptance A-MAP01:** recognized but truncated four-byte font data, truncated collection headers, and out-of-range collection directory offsets return the documented invalid-font result without sanitizer errors or out-of-bounds reads. A valid single font and valid collection face still load. Probe each affected public entry point, not only a private parser helper.

Include truncated table headers/list arrays, valid outer directories with invalid nested offsets, and representability boundaries in A-MAP01. Use small malformed fixtures or synthetic sizing probes for overflow cases; allocating multi-gigabyte input is not necessary to prove checked arithmetic. A descriptor validator called only after `LoadFont` has already walked unchecked tables does not satisfy this boundary.

**Must remove:** pre-validation directory dereferences and unchecked nested pointer formation on these paths.

#### COLD2 — Resolve validated subtable descriptors once

**Location:** `kb_context_compile.inc`, `kb_context_fuse.inc`, `kb_compiled_context.h`, font loading/compilation in `kb_text_shape.h`.

- [x] Establish shared checked lookup/subtable views with resolved type, table/lookup identity, valid target extent, and stable access to blob-backed data. Reuse existing flat subtable indexing rather than create a competing identity system.
- [x] Cover loaded native blobs and the public load/place/compile path, not only TrueType/OpenType conversion.
- [x] Reject invalid extension targets, extension-to-extension types, and inconsistent lookup subtype declarations. Retain the validated invariant for downstream consumers.
- [x] Include the native action consumers: `kbts__DoSingleAdjustment` (GPOS extension handling), `kbts__DoSubstitution` (GSUB frames), `kbts__UnpackLookup`'s mark-filter-set resolution, and diagnostic decoders. Font-invariant flags/filter-set references belong in the same immutable descriptor, not a second runtime wrapper or a full descriptor copy per anchor.

**Acceptance A-COLD2:** valid direct and extension-wrapped equivalents shape identically; self/nested extension targets, zero/self offsets, truncated targets, out-of-range offsets, and inconsistent subtypes fail deterministically through the relevant construction boundary. No unbounded extension walk is reachable during compilation or shaping. Compilation remains idempotent and ownership is balanced on failure. Internal fixtures use the validated contract rather than a test-only unchecked bypass.

**Must remove:** independent raw extension-chasing logic in migrated compiler, fusion, plan, and runtime consumers.

**User ANSWER disposition:** the nested loop in `kb_context_compile.inc` enumerates GSUB/GPOS → each table's lookups → each lookup's subtables, then unwraps extensions. Its input is `Font->Blob`; its output is contextual rules/programs addressed by the existing flat subtable identity. It runs during font compilation, not for every run. The first three enumeration levels are necessary; repeated discovery and unchecked extension chasing are the problems. Resolve the wrapper once without flattening away native priority.

#### RUN06 — Consume resolved direction rather than decode each run

**Location:** `kb_gsub_stream.inc`, lookup direction selection.

- [x] Read resolved type/direction from the shared descriptor established by COLD2.
- [x] Remove the runtime wrapper walk without inventing mixed-direction handling for invalid fonts.

**Acceptance A-RUN06:** direct and extension-wrapped reverse-context lookups produce identical ordered results, including the existing reverse read-after-write witness. A scoped warm-run probe records no extension unwrap work in direction selection.

**Must remove:** per-run extension decoding used only to obtain lookup direction.

#### MAP02 — Separate class bounds from glyph bounds

**Location:** `kb_text_shape.h`, ClassDef admission matrix construction.

- [x] Bound class-mask reads by the class index and its mask capacity.
- [x] Bound matrix writes by glyph count. Define conservative admission for valid classes outside a small summary mask, or size the representation correctly; do not drop those rules.

**Acceptance A-MAP02:** ClassDef formats 1 and 2 behave equivalently for equivalent mappings; a low glyph ID with class 1024 or greater neither reads past a 16-word mask nor disappears from applicable behavior. Glyph IDs at/outside the font bound cannot write past admission rows. Preserve class-zero semantics. Run the boundary witnesses under ASan/UBSan.

**Must remove:** guards that validate `GlyphId` while indexing storage by `GlyphClass`.

#### MAP05 — Align bucket and sort storage with correct raw ownership

**Location:** `kb_text_shape.h`, bucket allocation and `SortGlyphBucket` storage; final queue replacement must use the same convention.

- [x] Use checked alignment-slack sizing, aligned usable storage, and retained raw allocation bases.
- [x] Migrate both bucket growth and sort/merge storage, including failure exits. Do not leave old and new allocation conventions beside each other.

**Acceptance A-MAP05:** an allocator returning an odd-address usable pointer can exercise queue growth and disordered sorting without undefined behavior. Fail each reachable allocation in turn; no invalid free, leak, stale usable pointer, or successful partial result occurs. Fixed-memory exhaustion remains explicit.

**Must remove:** direct typed casts of these unadjusted raw allocation results and freeing an aligned interior pointer.

#### MAP07 — Make context creation failure-safe

**Location:** `kb_text_shape.h`, `CreateShapeContext`/placement ownership.

- [x] Guard the placement result before assigning allocator fields.
- [x] Apply the common aligned/raw-base ownership convention and checked size calculation to self-owned context construction.

**Acceptance A-MAP07:** fail-first allocation returns failure without dereferencing null; odd-address allocation supports actual context use and balanced destruction. Caller-owned placement remains usable at its advertised size and does not acquire hidden heap ownership.

**Must remove:** unconditional dereference of a failed placement result and inconsistent context allocation ownership.

### P2 — Feature state, effective values, and cache identity

#### MAP09 — Publish the actual last-wins feature set

**Location:** `kb_text_shape.h`, effective feature snapshot construction.

- [x] Publish the deduplicated effective values rather than the original stack prefix.
- [x] Canonicalize tag/value ordering after last-wins resolution; retain explicit zero and nonbinary values. Do not collapse explicit disable into absence.

**Acceptance A-MAP09:** nested `[kern=0, kern=1]` and the reverse order shape according to the last value. Equivalent effective sets reached through different stack orders have identical behavior. Nonbinary selections and independent feature tags retain their values. Assert visible feature effects, not only array contents.

**Must remove:** copying the original stack prefix with a deduplicated count.

#### MAP10 — Correct feature pop and invalidation

**Location:** `kb_text_shape.h`, feature stack removal.

- [x] Shift the actual remaining suffix, set the successful result, and invalidate effective state on a real removal.
- [x] Preserve bounded public stack policy independently of font-feature enumeration.

**Acceptance A-MAP10:** top, middle, and bottom removal from three distinct overrides expose the correct remaining behavior; a missing tag reports no removal and changes nothing. Removing a nested same-tag override restores the earlier value. Append more text after each mutation and verify the next span receives the correct state. No-hit operations do not create unnecessary snapshots.

**Must remove:** the shift loop that ignores `MoveIndex`, the permanently false success result, and stale-state reuse after a successful pop. A small feature-metadata `memmove` is allowed; glyph suffix moves are not.

#### MAP11 — Give feature-set identity the cache's lifetime

**Location:** `kb_text_shape.h`, feature snapshots, prepared input references, persistent glyph-config cache keys.

- [x] Intern immutable canonical effective feature sets in context-lifetime storage, shared by input references and cache keys.
- [x] Key derived glyph configurations by their shape configuration and stable feature-set identity. Preserve font/script/language separation already carried by the shape configuration.
- [x] Remove persistent references to recyclable snapshot storage; free interned state with the context.

**Acceptance A-MAP11:** reuse a context across successive `ShapeBegin` cycles with different equal-sized feature sets and deliberately reused scratch addresses. Each cycle shapes with its own values; earlier cached values cannot be returned because an address was recycled. Repeating the same bounded set of feature states stabilizes persistent allocation demand. Different shape configurations cannot share an incompatible derived glyph configuration.

**Must remove:** raw scratch address/count identity in persistent caches and duplicate ephemeral effective-value storage made obsolete by interning.

#### MAP12 — Stop cache searching at a hit and reuse span results

**Location:** `kb_text_shape.h`, shape/glyph-config caches and `ShapeRun` preparation.

- [x] Return immediately on a hit instead of continuing through later blocks.
- [x] Carry the last resolved key/result across contiguous input with unchanged effective configuration; invalidate on every actual key change.
- [x] Resolve/create all actually used glyph configurations during run preparation and retain their references on the existing input/span ownership path. Remove `FindOrCreateGlyphConfig` from `ShapeRun`; merely caching its last result inside execution still leaves compilation on a miss. Preserve feature/source boundaries without allocating another full input or glyph mirror.

**Acceptance A-MAP12:** repeated spans and alternating configurations produce the same glyph/source/position output as fresh resolution. A many-span probe shows no search of later blocks after a hit and no per-codepoint cache search for an unchanged effective feature span. A previously unseen feature set is compiled during preparation; a counter around execution records zero font/shape/glyph-config compilation calls even on that first shape. Construction failures report through the preparation/error contract and leave reuse/destruction safe. Do not add a general hash layer in this cutover.

**Must remove:** inner-loop-only hit breaks, redundant per-codepoint configuration queries, and configuration find-or-create/compilation from the execution path.

### P3 — Matching semantics and script lifecycle

#### RUN05 — Give dispatch, matching, and actions the same anchor

**Location:** `kb_context_match.inc`, caller traversal and child action frames in `kb_glyph_actions.inc`/`kb_gsub_stream.inc`.

- [x] Establish a filtered-anchor contract. Prefer outer traversal owning skip decisions; matching and actions use the supplied anchor without silent relocation.
- [x] Migrate every matcher caller and direct fixture to that contract. Do not preserve an inconsistent private-helper expectation by adding compensating offsets.

**Acceptance A-RUN05:** a leading Unicode-skipped glyph, ignored mark, and overlapping first coverage cannot cause the matched glyph and action index zero to refer to different live glyphs. Test end-to-end substitution/positioning and source identity, not merely a matcher success flag. If admission already proves relocation impossible, document and demonstrate that invariant at the actual caller boundary.

**Must remove:** hidden anchor movement that leaves action frames pointing at an earlier glyph.

#### GLYPH2 — Share first-success subtable semantics

**Location:** `kb_glyph_actions.inc`, top-level and nested GPOS lookup application.

- [x] Make both paths consume one ordered first-success lookup application contract.
- [x] Continue executing distinct contextual action records in their prescribed order; only subtable iteration stops at success.

**Acceptance A-GLYPH2:** a child lookup with two overlapping adjustment subtables applies the first successful adjustment exactly once, matching direct application of the same lookup. A first-subtable miss reaches the second. A parent with two action records still executes both records. Compare advances/offsets, not helper call forwarding.

**Must remove:** child subtable loops that ignore adjustment success and can cumulatively apply mutually alternative subtables.

#### GLYPH3 — Resolve record targets with the same live filtering

**Location:** `kb_glyph_actions.inc`, `SequenceIndex` targeting after context matching.

- [x] Use a shared live target resolver with the matching sequence skip policy and an explicit missing-target result.
- [x] Resolve each record after preceding actions mutate the sequence. Never substitute an original-reference snapshot for live semantics.

**Acceptance A-GLYPH3:** ignored joiners/marks between input positions do not redirect an action to the wrong glyph. Earlier expansion, ligation, or deletion is visible to later records. An unfound target does not fall back to the original anchor. Verify exact changed glyph/source identities and retained suffix order.

**Must remove:** zero-skip rescans inconsistent with matching and accidental original-anchor fallback on a missing target.

#### GLYPH4 — Separate Hangul construction state from output length

**Location:** `kb_execute_op.inc`, Hangul normalization.

- [x] Represent successful syllable construction separately from tone/prefix output count.
- [x] Choose composed/decomposed syllable output before arranging the tone mark; preserve the intended consumed-input extent.

**Acceptance A-GLYPH4:** exercise L+V and L+V+T with/without tone, with composed forms supported or unavailable, plus precomposed inputs relevant to the path. A prefixed tone cannot suppress the syllable fallback or cause consumed letters to disappear. Assert exact emitted sequence and source association, not simply nonempty output.

**Must remove:** `LvtGlyphCount == 0` as a proxy for syllable construction success when the count includes a tone prefix.

#### GLYPH5 — Make Hangul transforms source-preserving

**Location:** `kb_execute_op.inc`, Hangul replacement construction and identity path.

- [x] Inherit effective features and source ownership from the appropriate consumed record, following the existing normalizer's contraction/expansion convention.
- [x] Keep a tone's own source association. Leave an identity transform in its original record/slot rather than remapping and reinserting it.

**Acceptance A-GLYPH5:** nonzero user/source IDs and visible per-source feature overrides survive composition, decomposition, tone movement, and unchanged input. Emitted glyphs follow the declared ownership convention; unchanged input retains its identity without a delete/insert cycle. Combine these cases with GLYPH4 rather than implement parallel Hangul builders.

**Must remove:** zero-source/zero-config replacement construction and delete/reinsert work on the unchanged fallback.

#### GLYPH6 — Keep semantic syllabic position canonical

**Location:** `kb_script_shape.inc`, Indic sort-key construction, attachment grouping, and restoration.

- [x] Move temporary composite ordering into an explicit key lifetime. Prefer reuse of the existing `SortKey` field before its later GPOS assignment if all consumers and key-width requirements permit it.
- [x] Make sorting and attachment grouping read the explicit key while canonical syllabic position remains meaningful. Do not add another full glyph representation.

**Acceptance A-GLYPH6:** left-matra reversal, equal-key stability, consonant/matra attachment groups, and `EndCluster` behavior match the semantic control. A work probe records no full-cluster shift-back/restoration walk. Subsequent GPOS receives freshly assigned logical-order keys, not stale script keys. Report any glyph-record size change; increasing every record is not the default solution.

**Must remove:** temporary packed-key ownership of `SyllabicPosition` and the full restoration pass.

#### GLYPH7 — Enforce Myanmar non-cluster progress

**Location:** `kb_script_shape.inc`, Myanmar non-cluster traversal.

- [x] Establish reachability of an OTHER/out-of-range syllabic-class glyph after the preceding stages.
- [x] Advance/consume/exit explicitly, matching the intended sibling-script non-cluster behavior rather than applying an arbitrary iteration limit.

**Acceptance A-GLYPH7:** a reachable witness terminates, preserves non-cluster glyphs, and proceeds into the following valid cluster. Empty/end-of-range input terminates correctly. Use an external timeout only to detect the pre-fix hang, not as the production repair.

**Must remove:** a reachable loop body that can neither advance nor change its predicate.

#### MAP14 — Make cluster restart and unwind one transition

**Location:** `kb_text_shape.h`, cluster execution checkpoint and range restoration.

- [x] Save instruction position, feature cursor, and sequential lookup cursor in one explicit checkpoint.
- [x] Share the range-unwind exit for success/error transitions without replacing specialized shapers with a generic framework.

**Acceptance A-MAP14:** consecutive clusters restart with identical intended feature/lookup cursors; the second cluster is not shaped using the first cluster's final cursor. Inject an error during cluster execution and verify range restoration, safe destruction, and a subsequent successful operation under the API's recovery contract. Cluster mutations preserve the outside suffix.

**Must remove:** independently maintained checkpoint locals and duplicate unwind loops for the same transition.

### P4 — Compiler model, configuration work, and scope

#### COLD1 — Decode related predicate sources together

**Location:** `kb_context_compile.inc`, `ContextPredicate` and source/predicate interning.

- [x] Enumerate each coverage/class source once to construct its requested related memberships, instead of scanning every font glyph independently for each value/index.
- [x] Preserve class-zero complements, coverage intersections, boundary-symbol behavior, and bitset interning.

**Acceptance A-COLD1:** format-1 coverage-index rules, equivalent class/coverage rules, implicit class zero, and glyphs outside explicit class ranges retain exact matching behavior. A repeated-source fixture records one source enumeration per distinct source rather than one whole-glyph search per requested value. Report global symbol-refinement work separately; do not claim that necessary pass vanished.

**Must remove:** full-domain source searches repeated only because the coverage index or class value changed.

#### COLD4 — Analyze one model and emit final storage once

**Location:** `kb_context_compile.inc`, `kb_context_fuse.inc`, shared compiler definitions in `kb_compiled_context.h`.

- [x] Retain parent lookup, outcome/rule identity, global predicate identity, source domain information, and ordered rule relationships when first known.
- [x] Build reusable candidate lists/dispatch facts once; derive counts/layout from those facts and emit from them without repeating identical membership evaluation.
- [x] Let fusion query retained rules and dispatch views directly. Delete the complete intermediate contiguous cache and metadata recovery from it.
- [x] Perform one final contextual-cache packing/ownership step after analysis; release all scratch on success and every failure path.
- [x] Count and eliminate repeated identical fusion-source/domain/child-map analysis where retained facts suffice. `FusionConstant` revisits glyph domains and child coverage; `FusionWinner` traverses symbol/program/candidate/test combinations. Removing `Raw` alone does not remove this work. Name each remaining proof pass and its growth dimensions; do not silently drop previously supported fusions or change priority to make compilation faster.

**Acceptance A-COLD4:** existing priority, zero-record blocker, wide-probe, mask-width, sparse-program, contextual GPOS, extension, and fusion-guard witnesses pass on the final resident cache. Allocation/work evidence shows zero complete intermediate-cache allocation and zero bytes copied from such a cache. Fusion does not recover relationships that collection already stored. Distinguish legitimate scratch payload construction and final emission from prohibited intermediate serialization. No final pointer references freed build storage; injected failures leave balanced ownership.

Final-cache witnesses must include empty dispatch/programs, boundary symbol zero (without confusing live glyph ID zero with a tombstone), tautological padding, candidate counts immediately around 4/8-lane boundaries, first/last bucket masks, compact/wide probes, and compact outcome-width limits. Exercise the final compact test's 32-bit gather overread guard under AVX2, not merely the pre-pack allocation. Preserve one explicit invalid-outcome encoding per width.

**Must remove:** `Raw` cache construction/ownership, raw-to-final classifier/table copy plumbing, compact-to-logical decoding used only to recover compiler facts, and duplicate sizing/emission membership scans.

#### COLD3 — Scale cold table interning without runtime indirection

**Location:** `kb_context_fuse.inc`, `PackTable` and final table emission.

- [x] Replace the fixed 256-chain assumption with a capacity policy appropriate to the distinct table population, reusing the existing cold interning pattern where practical.
- [x] Reuse or compute hashes during final-byte emission when this avoids re-reading the same bytes; equality still includes byte comparison and the required size/alignment compatibility.

**Acceptance A-COLD3:** equal table payloads can share resident storage; unequal payloads, including a deliberate hash-collision witness, cannot alias incorrectly. Final matching remains direct-table access with unchanged alignment guarantees. Record distinct-table count, capacity, chain/probe lengths, and bytes hashed on small and large fonts. This task is not accepted on the assertion that a larger hash table must be faster.

**Must remove:** the fixed-chain bottleneck and redundant candidate rehashing where a valid final-byte hash is already available. No new runtime interning lookup remains.

#### MAP03 — Build each stage's ordered lookup union once

**Location:** `kb_text_shape.h`, `PlaceShapeConfig` feature/root collection.

- [x] Accumulate each eligible feature lookup reference once per semantic stage, keyed by native lookup index; combine required/default/filter/skip metadata according to current policy.
- [x] Emit ascending lookup order from the accumulator. Reuse collected results rather than rerun semantic construction solely to count storage.
- [x] Keep cheap fixed-memory sizing bounds distinct from actual construction; do not force expensive speculative lookup execution into a sizing call.
- [x] Prove accumulated stage boundaries and queue/lookup indexes fit their stored widths before publication. Native lookup IDs fitting `u16` does not prove that the sum of selected roots across GSUB/GPOS and repeated semantic stages fits `FeatureStageFirstLookupIndices` or queue membership fields. Widen necessary aggregate fields or return the documented construction error; never truncate. Sizing and placement must agree at the boundary.

**Acceptance A-MAP03:** deliberately unsorted feature lookup lists, duplicate roots across features, required features, optional enables/disables, and nonbinary values produce the correct ordered output. The same root in different semantic stages is not improperly collapsed. A work probe shows no repeated full-feature search for every next minimum root; count-only entry points do not execute shaping probes.

Include a synthetic aggregate crossing the 16-bit stage-boundary limit while each native lookup ID remains valid. This is distinct from the more-than-32-features case.

**Must remove:** next-lowest-root selection by repeated rescanning and duplicate full root analysis for count/fill/placement.

#### MAP04 — Do not truncate eligible font features at the override limit

**Location:** `kb_text_shape.h`, baked-feature enumeration.

- [x] Remove the use of `KBTS_MAX_SIMULTANEOUS_FEATURES` as a limit on eligible language-system font features.
- [x] Stream references into MAP03's accumulator or size temporary storage from validated font counts. Keep the separate public override-stack limit explicit.

**Acceptance A-MAP04:** a valid language system with more than 32 eligible features can execute a visibly affecting feature beyond entry 32, including explicit enable and required-feature cases where applicable. No silent partial feature list is published. Large malformed counts fail through validation rather than memory corruption.

**Must remove:** the silent `BakedFeatureCount` truncation and the fixed baked-feature array if its only justification is the public override limit.

#### COLD5 — Separate proof lifetime, construction peak, and resident bytes

**Location:** `kb_gsub_plan.inc`, `Parts[5..7]` planning workspace; config size/place/create paths in `kb_text_shape.h`.

- [x] Make proof arrays construction-only and reuse dead scratch by liveness. No resident plan field may require their contents after publication.
- [x] Size resident admission rows by actual stage stride rather than retaining the lookup-count upper-bound layout.
- [x] For self-owned construction, retain only final storage. For caller-owned placement, keep peak required capacity honest; overlay only when lifetimes provably do not overlap.

**Acceptance A-COLD5:** requested resident allocation bytes decrease by the removed proof/over-reservation payload on a fixture that exposes it. Report constructor peak separately. A caller-owned buffer of the advertised capacity succeeds with untouched canaries; insufficient capacity fails safely without heap fallback. Sizing does not execute contextual localization. Existing config-time contextual `locl` behavior still works. Do not report a smaller logical struct as physical memory saved while its backing allocation remains unchanged.

**Must remove:** proof-only resident ownership, stale proof pointers, and unconditional upper-bound admission reservation in final owned configurations. A genuinely necessary fixed-memory construction peak is not falsely listed as eliminated.

#### COLD6 — Build admission in its storage order

**Location:** `kb_gsub_plan.inc`, default/possible admission aggregation; related matrix production in `kb_text_shape.h`.

- [x] Aggregate glyph-major or in bounded tiles once the selected stage mapping exists.
- [x] Where consumers permit, construct lookup eligibility and selected stage admission in the same glyph-row traversal using shared lookup summaries. Remove only intermediate matrices that no remaining consumer needs.

**Acceptance A-COLD6:** default, disabled, explicit-enabled, required, and nonbinary paths preserve admission and final output, including later writes enabling candidates. Work-count evidence shows the removed lookup-major scatter or repeated row traversal. Keep config construction work separate from warm matching; do not weaken live checks to simplify cold aggregation.

**Must remove:** full glyph-row scatter repeated once per selected lookup where the same result can be aggregated in row order.

#### P4-SCOPE — Derive runtime bounds from reachable programs

**Location:** configuration planning and scratch sizing in `kb_text_shape.h`, `kb_gsub_plan.inc`, and shared compiled program metadata.

- [x] Derive the relevant program closure from permitted roots, nested targets, and preparation probes, with table identity and cycle-safe visitation.
- [x] Compute contextual window maxima and applicable plan metadata from that closure, including SIMD guards and config-time probe requirements.
- [x] Keep the one global symbol domain. Do not add a per-script symbol mirror or claim that all font-global symbol-index costs have disappeared.

**Acceptance A-SCOPE:** the Latin/unrelated-large-Arabic witness from section 2.4 has unchanged Latin output and no Arabic-only lookup execution or window inflation. Shared/default/nested/probe references and later explicit enables remain functional and correctly sized. A configuration with no reachable contextual program does not allocate a font-wide contextual window merely because another script has one.

**Must remove:** global maximum contextual-window inheritance when selected reachability proves a smaller requirement. Mandatory semantic lookup-stage barriers remain.

The selected window bound must be published by the configuration/plan and consumed by **both** scratch size and placement paths, replacing their current reads of `Font->CompiledContexts->WindowCapacity`. Keep font-wide maxima only if an actual font-level consumer needs them. Shared font compilation and a small config-local execution window are compatible.

#### LUT01 — Compile repeatedly consumed font glyph facts

**Location:** `kb_text_shape.h`, `kbts__GlyphClasses`, `kbts__InitializeGlyph`, `kbts__GsubMutate`; font compilation and destruction.

**Source finding:** `GlyphClasses` reopens GDEF and performs ClassDef lookup(s); initialization calls it for mapped glyphs and `GsubMutate` calls it for substitutions. The glyph-symbol classifier is already compiled, but these adjacent immutable facts are not. No speedup is claimed before measurement.

- [x] Compile glyph class and mark-attachment class from validated GDEF once per font; consume the result directly during initialization and mutations. Reuse the canonical glyph-class fields; do not materialize font-specific full `kbts_glyph` templates and copy them into every record.
- [x] Prefer a direct per-glyph class table with the existing narrow class fields; reuse an existing exact paging convention only where it avoids substantial empty storage without a new decode/translation layer. Record bytes and dependent accesses; defer compression experiments. Missing GDEF and absent class definitions require explicit semantics: the initializer currently derives fallback base/mark class from Unicode properties, whereas substitution uses GDEF lookup results. Do not erase that distinction with a glyph-ID-only fallback LUT or an unproved identity shortcut.
- [x] Avoid recomputing ID-derived fields when a mutation provably leaves them unchanged, but still apply observable generated/ligature flags and required admission repair. A same-ID substitution is not necessarily a semantic no-op; prove field validity before skipping writes.

**Acceptance A-LUT01:** GDEF formats 1/2, implicit class zero, mark attachment classes/filtering, absent GDEF, out-of-range IDs, and same-ID/changed-ID actions preserve exact output and live classification behavior. After font compilation, warm initialization/substitution performs no GDEF ClassDef search for cached facts. Report resident bytes, compilation work and ID-derived loads; fixed/native-blob compilation and allocation failure retain their contracts. Do not claim coverage/mark-filter membership searches disappeared merely because glyph classes were cached.

**Must remove:** repeated GDEF class decoding on the migrated warm paths and superseded lookup/cache ownership; retain Unicode-derived fallback logic that is not font-invariant.

#### LUT02 — Share useful immutable planning summaries

**Location:** `kb_gsub_plan.inc`, `GsubPlanDomains` and final `FirstSymbols` construction; compiler analysis/emission.

**Source finding:** config construction scans program candidates/tests and the full symbol domain to derive read/write domains, then scans symbol/bucket dispatch again for `FirstSymbols`. The compiler already knows these relationships. COLD4's compiler-only reuse does not by itself stop rediscovery for every configuration.

- [x] Produce reusable font-invariant per-lookup analysis and nonempty first-symbol information while compiler facts are live; configurations select/aggregate it without decoding compact runtime tests back into logical predicates.
- [x] Retain only summaries with actual config/runtime consumers. A compact reusable summary is not proof-only workspace; retaining every temporary domain/candidate array is prohibited. Configuration-dependent feature masks, stage placement and closure remain configuration-owned.
- [x] Measure summary construction/resident bytes against repeated configuration cost. Use the existing symbol domain and flat lookup identity; no script remap layer, duplicate per-font classifier, or lazy mutable cache.

**Acceptance A-LUT02:** multiple script/language/feature configurations over the same font preserve ordered plans, blockers, write/read dependencies and backtrack guards. Count candidate/test/bucket/symbol visits separately for the first font build and subsequent configs. Repeated font-invariant analysis disappears; necessary config union/proof work is explicitly identified. Resident and peak accounting includes every retained summary.

**Must remove:** compact-test-to-logical reconstruction and repeat full-domain discovery in each configuration for facts now provided by immutable font summaries.

### P5 — Execution workspace and minimum index work

#### MAP13 — Reuse high-level execution scratch

**Location:** `kb_text_shape.h`, `ShapeRun`, context reset/destruction, scratch initialization/binding.

- [x] Reuse context-owned scratch capacity rather than allocate a new independent scratchpad for every run. Prefer one reusable active workspace with explicit configuration rebinding/growth; keep low-level caller-owned scratch supported.
- [x] Separate execution lifetime from prepared input, canonical feature state, and public output lifetime. Reset all configuration-dependent cursors, rows, buckets, and range state at the appropriate boundary.

**Acceptance A-MAP13:** actual `ShapeBegin`/input preparation/`ShapeRun` use with many short runs has memory bounded by the prepared input, distinct persistent configs/feature sets, and high-water execution capacity—not the sum of each run's scratch allocation. After warming a bounded set of configurations and maximum run size, repeated same-bound workloads issue no new execution-scratch allocations. Alternating configurations, empty runs, growth, errors, and subsequent reuse remain correct. Pending spans are not invalidated by scratch reclamation; borrowed results obey their existing lifetime.

**Must remove:** one newly initialized, independently retained execution scratchpad per high-level run.

#### RUN01 — Initialize index storage once when it becomes readable

**Location:** `kb_gsub_stream.inc`, stream growth and row admission.

- [x] Initialize validity/presence metadata eagerly, copy live rows, zero their new tails, and initialize dormant rows at first admission.
- [x] Preserve zero SIMD/word guards and no-read-before-initialization across growth, slot reuse, and configuration changes.
- [x] Use RUN07's actual row-family consumers/layout when deciding what exists at all; lazy initialization of a needlessly allocated dense matrix is only a partial improvement.

**Acceptance A-RUN01:** fresh allocation, growth with admitted rows, first admission of dormant rows, and slot counts around word/SIMD boundaries preserve exact output and safe loads. Poison-backed scratch or an appropriate uninitialized-read diagnostic demonstrates initialization coverage; ASan alone is not evidence against uninitialized reads. A byte-write probe shows unused capacity is not cleared twice.

**Must remove:** blanket zeroing immediately overwritten by live-row copies and repeated zeroing of still-unused rows.

#### RUN02 — Drop redundant exact-position tests in logical traversal

**Location:** `kb_gsub_stream.inc`, unordered live traversal.

- [x] Remove the exact symbol-position membership probe when traversal already supplies the live slot and its current symbol.
- [x] Retain first-symbol eligibility, live filters, and the exact index for ordered bitmap consumers.

**Acceptance A-RUN02:** deletion/reuse, reordering, and symbol-changing substitution produce identical selected candidates and output in unordered traversal. A work probe records no exact-position recheck in that path. Ordered index consumers continue passing word-boundary and mutation witnesses.

**Must remove:** the redundant probe, not the shared symbol index or mutation hooks.

#### RUN03 — Repair only observable stage rows after deletion

**Location:** `kb_gsub_stream.inc`, deletion/index repair and stage interval ownership.

- [x] Limit native-row clearing to current/future observable stages, narrowing to the active operation interval only where reinitialization proves that safe.
- [x] Maintain exact symbol removal and immediate current-stage visibility; document the completed-row reset boundary.

**Acceptance A-RUN03:** ligature component deletion, nested deletion, recycled slots, current-stage continuation, and later dependent lookups remain correct. Subsequent operation/reuse cannot resurrect a deleted candidate from stale completed rows. A deletion probe records zero writes to completed native stage rows while retaining all currently observable repairs.

**Must remove:** unconditional iteration over already completed admitted stages for every deletion.

#### RUN04 — Gather only consumed backtrack data

**Location:** `kb_context_match.inc`, backtrack window gather and consumers.

- [x] Remove backtrack glyph-reference/offset stores and distance maintenance that no consumer reads.
- [x] Retain forward action/fusion metadata and symbol gathering. Do not add a replacement array-of-structs window.

**Acceptance A-RUN04:** wide backtrack, ignored glyphs, range boundaries, and fused actions retain exact behavior. Consumer inspection plus a disposable write-count probe shows symbols-only backtrack gathering and no removed-field reads. Forward targeting still passes live-action cases.

**Must remove:** unused backward `Glyphs`/`Offsets` writes and backward distance arithmetic, not metadata required on the forward side.

#### RUN07 — Scope index work to the active execution interval

**Location:** `kb_gsub_stream.inc`, `ReserveGsubStream`, `RunGsubStages`, `GsubBuildIndexes`, candidate iteration and growth; stable-slot active ranges.

**Source finding:** `ReserveGsubStream` derives symbol-row allocation from the whole plan (`NativeCount != GsubLookupCount`), not `IndexSymbols`; native rows reserve every native lookup. `RunGsubStages` chooses native indexing using whole-storage `LiveCount > 64`, even when the active cluster contains one glyph. Initialization then clears metadata/full physical row strides for that interval. This is separate from RUN01's double clearing and P4-SCOPE's contextual windows.

- [x] Bind/grow index row families for actual consumers. A native-only interval must not first-allocate unused symbol-position rows just because another interval contains compiled contexts. Reuse already warmed capacity without clearing or copying dormant families; rebinding must check layout/config identity as well as glyph capacity.
- [x] Base small-range work decisions on the active range, not outside live glyphs. Reuse a trustworthy count if already available; otherwise a bounded probe up to the decision threshold is preferable to another unconditional full-range count. Do not add per-mutation counters with greater cost than the decision they serve.
- [x] Separate current initialized word extent from retained capacity where this reduces repeated clears/copies. Absolute stable-slot coordinates remain the index key; a tiny cluster at a high slot cannot be sized as if it occupied slot zero. Prove bounds and zero tails across range changes, tombstone reuse, growth, and config rebinding.
- [x] Account for the retained dense cost explicitly: symbol rows scale as `SymbolCount * ceil(slot_capacity/64) * 8`, native rows as `NativeCount * ceil(slot_capacity/64) * 8`, plus metadata. Keep the simplest direct allocation layout that omits unused work; do not hide font-global symbol costs behind the smaller contextual window claim.

**Acceptance A-RUN07:** exercise the same one-glyph native interval alone and inside a larger storage/range, including high physical slots and a large-then-small reuse sequence. Outside glyphs must not force an unnecessary full index build. Exercise native-only → contextual → native-only intervals and alternate configurations with equal slot capacity but different symbol/stage layouts; no stale row, wrong stride, hidden allocation after bounded warmup, or read of uninitialized capacity. Record first-allocation bytes, initialized bytes, row copies and scans, not just steady-state allocation count.

**Must remove:** unused-family first allocations, whole-storage threshold decisions for small active ranges, and capacity-sized work not required by the current readable extent. Exact live index semantics and consumer-required retained capacity remain.

#### CACHE01 — Verify locality without rebuilding a glyph mirror

**Location:** `kb_glyph_storage.inc`, canonical `kbts_glyph`/slot links, matching and ordered/unordered index consumers.

**Source finding:** on the reviewed x86-64 build each reserved slot costs 104 bytes of glyph record, 8 bytes of links and 4 bytes of free-slot storage. Logical traversal follows a separate dependent link load; one nonmonotonic edit clears `Ordered` until storage reset. Sentinels remove suffix moves but do not remove this load dependency or the dense bitmap footprint.

- [x] Inventory fields actually loaded/stored by initialization, gather, substitution, sorting, GPOS and output; record `sizeof`/offsets and touched cache lines. Preserve directly consumed cached fields; remove dead fields/stores rather than adding a hot/cold synchronization layer.
- [x] Exercise ordered input, reordered/recycled slots, long combining sequences, and large-then-small storage reuse. Count ordered/unordered path use and link/index traffic where needed to substantiate a removal. Do not add order-restoration scans or revive the rejected contiguous traversal experiment in this structural cutover.
- [x] Keep surviving references stable and tombstone/free-list state unambiguous. Grow storage only when required; no glyph-suffix memmove on deletion, insertion, reorder, or range exit. A sentinel must be checked/mapped before dereference unless a real addressable guard record exists.

**Acceptance A-CACHE01:** document the final canonical working set and dependent accesses, including tiny runs and fragmentation. The existing layout may remain; no speculative record-layout rewrite, alternate traversal engine, or PMU campaign is required. Any field/layout change migrates all consumers and preserves sources, attachment identities, allocation alignment and public iteration. Zero-copy is not presented as proof of cache locality or a speed win.

**Must remove:** demonstrated dead canonical/run bookkeeping and any trial mirrors/translation or speculative order-restoration machinery. In particular, `OutputFirst`, `OutputEnd` and `OutputGlyphCount` currently have writes but no output consumer; remove them with their obsolete stores unless the implementation inventory establishes a real existing consumer.

### P6 — Queue ownership, ordering, and fallback reuse

#### MAP06 — Use one queue representation through sort and execution

**Location:** `kb_text_shape.h`, GPOS bucket insertion/removal/sorting/execution; scheduling fields and consumers in glyph storage/actions.

- [x] Replace block-to-flat-to-block conversion with one queue representation shared by insertion, ordering, and consumption. Allocate for active queues, not a full glyph-capacity array for every font lookup.
- [x] Use contiguous active-bucket vectors plus reusable same-entry-type merge workspace and index-based membership as the default design. Growth must not invalidate membership. Sorting changes indices: publish membership once after the final order, before actions can observe it. Do not call a vector index stable across sorting without this update.
- [x] Keep one active queue ownership model; do not build competing vector/intrusive implementations or a benchmark-selected runtime mode. Change the default only for a concrete correctness/ownership constraint, not a speculative timing advantage.
- [x] Preserve ordered fast paths, deletion/removal semantics, nonbinary values, continuation, and live rebucketing. Return drained storage to reusable execution ownership rather than retaining a whole-run vector for every visited lookup.
- [x] Use tombstones/sentinels for queue removal without shifting the remaining queue on each deletion. Reserve the deletion key outside valid sort-key values; clear membership before recycling an entry and publish valid surviving handles after any sort/compaction. Queue-local sorting may move compact scheduling entries, never the canonical glyph suffix.

**Acceptance A-MAP06:** ordered, disordered, duplicate-key, removed/tombstoned, growth-during-scheduling, and live-rebucket cases produce exact GPOS results with no lost/duplicate execution. No reference into a movable vector survives a mutation that invalidates it. Queue conversion bytes into/out of linked blocks are zero. Sorting may use merge scratch or one required membership publication; report those bytes rather than relabel them zero-copy. Measure warm allocations, retained/peak bytes, and ordered/disordered cost on many tiny and large queues. Only the selected representation remains.

Include interior tombstones followed by growth, live rebucketing and storage reuse; verify no stale unbucket writes, duplicate execution, or valid key equal to the deletion sentinel. Report tombstone visits and compaction/handle-publication bytes even when the ordered path avoids sorting.

**Must remove:** the linked-block/flat-array round trip, copy-back into blocks, conversion-induced pointer repairs, and competing replacement prototypes. Sorting scratch contains the same compact entry type; it is not a second data model.

#### GLYPH1 — Make attachment marking single-owner

**Location:** `kb_glyph_actions.inc`, `AttachGlyph` and mark-to-ligature attachment.

- [x] Make `AttachGlyph` own the parent/child interval's `NO_BREAK` propagation.
- [x] Delete the duplicate mark-to-ligature walk after proving the same endpoint semantics under immutable GPOS order keys.

**Acceptance A-GLYPH1:** direct mark-to-ligature attachment, reattachment, non-topological attachment order, and intervening glyphs preserve positions and break flags. A work probe shows one traversal for that interval, not two. Later attachment-bound optimization remains correct.

**Must remove:** the second interval marking walk after `AttachGlyph`.

#### MAP15 — Find an insertion position, then splice once

**Location:** `kb_text_shape.h`, linked glyph insertion sort; callers in script/normalization ordering.

- [x] Search predecessors read-only, then perform one splice at the final position.
- [x] Preserve stable ties, range boundaries, and surviving slot/reference identity; use GLYPH6's explicit key where applicable.

**Acceptance A-MAP15:** already sorted, reverse-sorted, duplicate-key, and long combining sequences retain exact glyph/source order and stable slots. The moved glyph is spliced at most once per insertion, not once per inversion. Comparison complexity need not change; report reduced link writes without claiming an asymptotic improvement.

**Must remove:** repeated relinking of the same glyph during destination search.

#### MAP08 — Reuse exact fallback evidence without changing priority

**Location:** `kb_text_shape.h`, grapheme font coverage selection and glyph preparation.

- [x] Reuse exact cmap/normalization/coverage facts within the current preparation lifetime instead of rediscovering them between fallback selection and initialization.
- [x] Carry already-computed exact mapping/coverage evidence to its immediate preparation consumer using the existing input ownership path. Do not add a repeated-grapheme memo/hash cache in this cutover; speculative caching is later tuning, not necessary to eliminate same-lifetime rediscovery.
- [x] Preserve priority, missing-glyph behavior, normalization-sensitive coverage, and input ownership. A hash collision must not equate different graphemes.

**Acceptance A-MAP08:** repeated and changing graphemes across fonts preserve the selected highest-priority supporting font and exact output. Changing font priority invalidates reused selection; joiner/mark/normalization-sensitive sequences do not pass on nominal coverage alone. Count actual cmap/coverage/decomposition work before/after on single-font and fallback-heavy input. Exact same-lifetime reuse is required, without an additional general memo or prepared-glyph representation.

**Must remove:** exact duplicate coverage/mapping work made redundant by retained evidence. Do not replace it with previous-font-first selection or an approximate coverage shortcut.

### P7 — Integrated acceptance

- [x] Every `A-<review ID>`, `A-SCOPE`, `A-LUT01`, `A-LUT02`, `A-RUN07` and `A-CACHE01` has the behavioral/structural evidence its contract requires. Suspect behavior follows section 1; source-confirmed redundant work is not closed merely by calling it unmeasured.
- [x] All affected exported API callers, internal fixtures, size/place/create paths, benchmarks, and destruction paths use the final contracts. Use symbol-aware references when a language server is available; otherwise perform a scoped complete caller inventory.
- [x] Final resident programs, not a temporary compiler view, pass the matcher/action correctness suite on SSE2 and AVX2.
- [x] Public high-level shaping passes feature-stack, multi-run scratch, fallback, and cross-`ShapeBegin` cases absent from the direct benchmark.
- [x] All required representation/pass removals have source evidence and relevant work-count evidence. No legacy fallback or renamed copy of a removed bridge remains.
- [x] Final allocation failure/odd-address/fixed-memory witnesses and full project validation complete after integration.
- [x] Allocation/lifetime and necessary work-count evidence follow section 6. All compilation occurs before execution; no timing threshold or recovery percentage blocks this structural acceptance.

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

- [x] Run an actual high-level scenario that creates a context, pushes fonts/features/text, shapes all prepared runs, reuses the context for changed input/state, consumes public results, and destroys it.
- [x] Exercise many short same-config runs and alternating configurations with a counting/failure/odd-address allocator. Separate new immutable config/feature-set allocations from reusable execution scratch.
- [x] Exercise actual fallback with repeated graphemes and priority changes. The direct single-font corpus does not cover this.
- [x] Exercise Hangul and Myanmar via fixtures/public-API smoke. The current benchmark CLI accepts only `Latn`, `Arab`, `Hebr`, `Deva`, `Beng`, and `Thai`; do not invent unsupported `--script` command examples or claim those two scripts were covered by the existing matrix.
- [x] Exercise construction failures, exact advertised placement capacity/canaries, fixed-memory exhaustion, poisoned/uninitialized scratch, and successful reuse after errors at the relevant API boundary.
- [x] Exercise one active glyph inside a large storage, high-slot bounded ranges, native-only intervals in mixed configs, and huge-then-tiny reuse. These expose work hidden by freshly cleared direct benchmark input.

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

- [x] Can a maintainer follow font data through collect/analyze/emit without encountering a second complete intermediate runtime cache?
- [x] Does every retained compiler pass produce a necessary result rather than reconstruct an already-known relationship?
- [x] Are script reachability, default-disabled overrides, nested targets, and preparation probes represented together without font-global window inflation?
- [x] Is the retained font-global classifier tradeoff stated honestly, with no claim of eliminating all unrelated-script cold work?
- [x] Is each immutable object separate from mutable execution state, and does every cached identity outlive its cache entry?
- [x] Do matching, child targeting, attachment marking, and cluster restoration each have one explicit owner/contract?
- [x] Are real glyph transformations source-preserving and identity transformations left alone?
- [x] Does warm high-level shaping reuse capacity rather than accumulate one execution workspace per run?
- [x] Does GPOS use one queue representation, with growth/sort membership semantics explicit and no block conversion round trip?
- [x] Are all 34 review findings, reachability, and the four additional findings accounted for with observable evidence?
- [x] Have every mandatory removal and every preserved invariant been checked against the final integrated code?
- [x] Are font facts compiled once, useful planning summaries reused, active-range indexes consumer-scoped, and sentinel/guard domains proved?
- [x] Is the canonical working set/direct access path explicit without a glyph mirror or speculative locality machinery?
- [x] Is all compilation before execution, and are deferred timing work, untested boundaries and intentional upstream differences reported honestly?

## 8. Grounding references

- Existing implementation and API contracts: [IMPLEMENTATION.md](../../IMPLEMENTATION.md), especially the owned shaping, canonical execution, and compilation lifetime sections.
- Existing validation recipes: [justfile](../../justfile).
- Existing behavioral fixtures: [tests/kb_context_test.c](../../tests/kb_context_test.c).
- Benchmark entry points and limitations: [bench/shaping.c](../../bench/shaping.c), [bench/shaping_kb.inc](../../bench/shaping_kb.inc), [bench/shaping-build.sh](../../bench/shaping-build.sh).
- Prior architectural constraints: [minimum-work-shaping-pipeline-v2.html](minimum-work-shaping-pipeline-v2.html), [compiled-shaping-lut-v2.html](compiled-shaping-lut-v2.html). Historical performance status remains in `readme.md`; section 6.5 records the user's current architecture-first acceptance decision.
- OpenType lookup structure and extension rules: [GSUB specification](https://learn.microsoft.com/en-us/typography/opentype/spec/gsub), [GPOS specification](https://learn.microsoft.com/en-us/typography/opentype/spec/gpos).


## 9. Implemented cutover and acceptance evidence

This section is the implementation result, not an amendment to the historical experiments in section 6.7. The 34 original findings, reachability and four additional findings are dispositioned below. R01–R35 are removed through their named owners; no compatibility engine, full glyph mirror, intermediate complete runtime cache, queue block conversion or execution-time config compiler remains. User-authored `ANSWER` text is preserved.

### 9.1 Controls, commands and scope

The evidence archive is `/var/tmp/rwmd-shaping-simplification-evidence.tar.gz`; its companion `/var/tmp/rwmd-shaping-simplification-evidence.json` records the archive SHA-256. Paths below are relative to the archive's `rwmd-shaping-simplification-zak2kmht/` directory. `control/` and `control-manifest.json` identify the immutable pre-change source. `final/` and `final-manifest.json` identify the **pre-audit** implementation snapshot used by historical profiles, not the subsequent audit fixes. `baseline.json` identifies the frozen SSE2/AVX2 executables.

`completion-audit/verified-source-manifest.json` identifies the post-audit source. `completion-audit/completion-matrix.json` maps every one of the 139 checklist items to its original assessment and closure evidence. Captured `verified-*.json` command records, full reports, source snapshots and disposable drivers retain the final proofs. Earlier `verification.json`, `final-replays.json` and intermediate logs are preserved with their original scope. Throwaway instrumentation is archived, removed from loose storage and never enabled in production.

Environment: Clang 22.1.8, HarfBuzz 14.4.0, Linux x86-64, available CPU affinity 0–31; differential/stress commands select CPU 0. Feature modes are default, off, explicit, optional and alternate/nonbinary. The bundled font has 2,470,116 input bytes and SHA-256 `f01031f40e48dc29e1112e6b0b0450a2c6cd097f3f35cfff05c55cb311f8034c`. `fonts.json` records hashes, sizes and resolved paths for it and all seven Noto fonts used by the eight-font stress matrix. No required stress font was missing. Pre-span controls were not available; no recovery percentage is claimed.

Observed validation:

- Post-audit `just check`: config, profiler, contextual/GSUB, document, parser, layout and editor suites passed; complete output is in `completion-audit/verified-just-check.json`. The full contextual suite also passed under ASan/UBSan on **both SSE2 and AVX2**, including the new public arena-failure regression.
- Default 4 KiB verifier, both ISAs: **555 exact cases**, consumed checksum `352eeca8ff9a264d`, matching the frozen controls. Production and sanitized builds were exercised.
- Sanitized `--verify --size 65536 --cpu 0`, both ISAs: **555 exact cases**, checksum `e70410c2710bc6a7`, also matching the frozen 64 KiB control. Initial three-minute deadlines expired; the complete reruns succeeded. These are correctness runs, not timing samples.
- Complete sanitized stress, both ISAs: **9,760 measured cases each**, seed 1801614451; identical per-case result arrays and checksum `79f2ea2342ed662a`. **9,423 measured cases remain exact; 337 differ intentionally**, plus five Arabic warmup differences. Every input and every upstream/HarfBuzz checksum matches the frozen run. The frozen owned control was exact in all 9,760 cases.
- The normal strict stress command stops on the first intentional difference; the post-audit run reaches 3,660 exact measured cases before the first Arabic warmup difference. The complete disposable observer includes both `shaping.c` and its continuation-only `shaping_stress.inc` override. It retains the strict tuple comparator and nonzero exit, saving all results/full failing tuples in `completion-audit/verified-observer/full-{isa}.jsonl`. Both complete observers exit **1**, not “PASS”. No production whitelist, relaxed comparison or golden replacement was added.
- Six separately constructed **64-byte UTF-8** cases with supporting Noto fonts and the actual script/language/direction adapters: Latn, Arab, Hebr, Deva, Beng and Thai all match the frozen upstream adapter. Hangul and Myanmar are covered by actual shaping fixtures, not fictitious CLI script options.
- Public high-level, fixed/odd/failing allocator, native serialization, interning, ordering, attachment, high-slot and error-unwind scenarios below all passed their stated checks under ASan/UBSan. Poison-backed row probes, not ASan alone, establish initialization coverage.

### 9.2 Disposition of every finding

“Accepted” means the named behavior/removal has evidence **after the completion audit in section 9.6**; it does not retrospectively justify the original blanket closure or claim a timing win. The work scopes and tradeoffs in sections 9.3–9.5 apply to these rows.

| Finding | Removal | Disposition and concrete evidence |
|---|---|---|
| COLD1 | R04 | Accepted. Grouped class/coverage sources preserve class zero, intersections and live glyph ID zero. `compiler_probe`: nine requested predicates, two sources, 12 enumerated glyphs versus 72 repeated-domain visits. Real-font source counts below. |
| COLD2 | R05 | Accepted. One checked font-owned lookup/subtable descriptor resolves type, direction, filtering and extension target. `extensions`, `font-boundaries`, allocation-failure fixtures and `native-roundtrip` exercise raw/native/load-place-compile paths. Invalid extensions were accepted by the frozen witness; current construction rejects them. |
| COLD3 | R06 | Accepted with measured tradeoff. One growing payload interner; final-byte emission supplies hashes. Small/large font counts and deliberate FNV collision proof below. More observed chain probes are reported, not hidden. |
| COLD4 | R01–R03 | Accepted. Retained logical rules, parent/action identities and ordered dispatch feed fusion and one final pack. Frozen 74,427-byte complete Raw cache disappears. Final resident priority/blocker, wide/sparse/mask-width, fusion-guard and contextual GPOS fixtures pass both ISAs. |
| COLD5 | R07 | Accepted after closing a public-capacity gap. Config proofs use construction storage; self-owned configs retain final payload. Public `kbts_PlaceShapeConfig(..., Memory, MemorySize)` now rejects short buffers before writes. `config-work`, the migrated public contextual-locl fixture and `audit-compiler-aggregate` cover exact capacity/canaries, safe insufficient capacity, allocation failure, no fixed-memory heap fallback and no sizing-time localization execution. |
| COLD6 | R08 | Accepted. Glyph-major construction shares lookup eligibility and stage admission; actual stage stride sizes resident rows. Feature modes and later-write dependency fixtures pass. The former lookup-major admission traversal is removed; required row construction remains. |
| MAP01 | R22 | Accepted. Integer extent/count/alignment checks precede directory, TTC, maxp and lookup pointer formation. `font-boundaries` reproduces the old ASan overread. `verified-font-boundaries-{isa}` additionally checks the public matrix, a valid TTC with two observably different faces, nested counts and int/u32 sizing overflows without a huge allocation. |
| MAP02 | R22 | Accepted. Class-domain mask bounds and glyph-domain matrix bounds are distinct. `class-admission` covers formats 1/2, implicit zero, high class values and out-of-range glyph IDs; the frozen high-class witness overreads, current passes. |
| MAP03 | R09 | Accepted. One ordered per-stage root union handles unsorted/duplicate references, required/default/explicit/nonbinary states. `aggregate-width` publishes 66,000 sequential roots from 22,000 native lookups across three stages using four-byte boundaries; the frozen constructor fails the corrected witness. |
| MAP04 | R09 | Accepted. Eligible font features are independent of the public override-stack limit. `features-33` visibly executes the beyond-32 feature; frozen output fails. Required/explicit feature paths remain covered. |
| MAP05 | R23 | Accepted. Queue vector and merge allocations retain raw bases and align usable storage. `queues`, `queue-failures` and poison/odd-address probes cover growth, disorder and balanced destruction. `verified-runtime-queue-failure` fails growth of a populated queue while moving a glyph from another queue; both queues and the old membership remain intact. |
| MAP06 | R24 | Accepted. One contiguous scheduling vector, index-plus-one membership, tombstones and same-entry merge workspace. Ordered/disordered/tied/removed/rebucketed cases pass; block conversion bytes are zero. Required compaction, merge and membership writes are counted below. |
| MAP07 | R23 | Accepted after an additional failure-path fix. Construction checks precede field writes and placement/owning creation share alignment rules. The audit found that failed arena growth linked its sentinel as a new block and lost earlier allocations. The fixed early return, permanent public regression and twelve fail-each input/preparation/execution cases preserve sticky errors and balance destruction. |
| MAP08 | R31 | Accepted, including a confirmed normalization-sensitive fallback defect. Exact mapped IDs stay on existing input records and are consumed during initialization; no grapheme memo or prepared-glyph mirror. `fallback-normalization` fails frozen preferred-font selection and passes current priority changes. Paired real two-font/repeated/changing input has identical tuples and fewer cmap queries; added decomposition work is disclosed below. |
| MAP09 | R25 | Accepted. Immutable sorted effective values use last-wins resolution, retain explicit zero/nonbinary values and drive observable shaping. `feature-stack` and `feature-identity` cover nested overrides and equivalent histories; frozen witnesses fail. |
| MAP10 | R25 | Accepted. Pop moves the actual suffix, reports success and invalidates only on removal. Top/middle/bottom, missing-tag and nested restoration transitions shape correctly in `feature-stack`. |
| MAP11 | R26 | Accepted. Context-lifetime interned feature identities outlive cached glyph configs and pending input. Cross-`ShapeBegin` changed/equal-sized states and repeated bounded states pass; the frozen identity witness returns the wrong glyph. |
| MAP12 | R27 | Accepted. Immediate cache-hit return; contiguous unchanged spans retain resolved keys. Preparation creates every used glyph config. The 768-run probe reduces shape/glyph-config searches to eight each. Forty-entry public cache histories hit the first block with one block visit and no allocation. Public alternating-script execution records zero font, shape-config and glyph-config constructor calls, including first execution. |
| MAP13 | R28 | Accepted. One context-owned execution scratchpad rebinds/grows independently of input, immutable state and output. First 96-run execution requests fall 197→9; bounded warmed states request zero. Actual two-font layouts with equal 256-slot capacity and different symbols/stages survive 102 poison-backed rebindings with exact tuples. Public execution failures, fixed-memory exhaustion, destruction and successful fresh-context reuse are exercised. |
| MAP14 | R29 | **Cursor suspect disproved; ownership cleanup accepted.** The frozen cursor restoration was already correct. One explicit three-cursor checkpoint/common unwind replaces scattered ownership. `audit-cluster-recovery-run.json` now actually executes success→allocation failure→success with fresh scratch, preserves 62 outside glyphs and restores First/End/RangeActive each time. The former probe did not exercise its claimed post-error success. |
| MAP15 | R30 | Accepted. Read-only predecessor search followed by one stable splice. Sorted/reverse/tied 130-slot inputs preserve references/sources; splice and primary link-write counts below. Comparison complexity is unchanged. |
| RUN01 | R11 | Accepted. Consumer-scoped row families separate initialized extent from retained capacity. Poison-backed first-use/growth/dormant-row/tail/reuse probes pass; no blanket unused-capacity clear followed by a duplicate first-use clear. |
| RUN02 | R12 | Accepted. Live unordered traversal uses its supplied slot/current symbol without an exact-position recheck. Ordered bitmap consumers retain their exact index. Mutation, deletion/reuse, reorder and word-boundary fixtures pass both ISAs. |
| RUN03 | R13 | Accepted. Deletion repairs only observable native stages while maintaining exact symbol removal. Workspace probe observes one current-row write and **zero completed-stage writes**; later-stage/recycled-slot fixtures pass. |
| RUN04 | R14 | Accepted. Native backtrack gathering writes symbols only; forward action/fusion metadata remains. Paired seven-match wide/ignored-mark probes remove 1,038 metadata stores and 522 backward-distance decrements with identical matching results. |
| RUN05 | R15 | Accepted. Outer traversal owns filtering; matching/actions use that exact anchor, without relocation. `filtered-actions`, ignored marks/joiners and overlapping coverage witnesses assert changed glyph/source identity; frozen target witness fails. |
| RUN06 | R05 | Accepted. Shared resolved descriptors supply direction; no runtime extension walk. The observed direct/wrapped reverse fixture shapes 130 read-after-write-dependent glyphs on both ISAs: one direction-descriptor read and zero extension decodes per warm lookup, with identical output. |
| GLYPH1 | R17 | Accepted. `AttachGlyph` solely owns interval NO_BREAK marking. Direct/reattachment/non-topological fixtures plus paired real ffi/combining-mark cases preserve exact tuples, attachments and flags; 12 duplicate interval stores are removed. |
| GLYPH2 | R16 | Accepted. Root and child GPOS use ordered first-success subtable application; distinct parent records still all execute. `gpos-first-success` catches the old doubled advance and passes first-miss/second-hit and multiple-record cases. |
| GLYPH3 | R15 | Accepted. Each record resolves a live target after preceding mutation using the actual parent sequence policy; missing target is zero, never the anchor. Expansion/deletion/ligation and `missing-action`/`arabic-live-target` regressions pass. Intentional Arabic differences are explained in section 9.5. |
| GLYPH4 | R18 | Accepted. Hangul syllable success is independent of tone count, including the first precomposed syllable. Composed/unavailable L+V/L+V+T and tone cases preserve the complete consumed sequence; frozen source/output witness fails. |
| GLYPH5 | R19 | Accepted. Hangul transformations inherit effective source/features, preserve tone ownership and leave identity slots unchanged. `hangul-features` catches the frozen wrong glyph; combined composition/decomposition/tone/identity fixtures pass. |
| GLYPH6 | R20 | Accepted. Explicit temporary SortKey, not packed SyllabicPosition, owns Indic ordering/grouping. No shift-back restoration walk. Matra/reph/equal-key cases pass; `dotted-circle-boundary` additionally proves reph-only/dotted-circle/joiner boundaries and untouched following-cluster metadata/output. |
| GLYPH7 | R21 | Accepted. Reachable Myanmar OTHER advances and leaves the following valid cluster reachable. Frozen witness times out; current `myanmar` terminates with exact preserved output. No production iteration cap. |
| SCOPE | R10 | Accepted. Root/default/explicit/nested/probe closure selects windows over the one global symbol domain. `scope` catches the frozen unrelated-script window inflation; nested localization expansion and later enables remain correctly sized. |
| LUT01 | R32 | Accepted. Font-owned class/attachment LUT, four bytes/glyph, feeds initialization/mutation directly. Controlled observed GDEF formats 1/2, absent definitions/no GDEF and attachment classes 255/511/65535 pass both ISAs. Warm initialization/mutation performs direct class loads and zero ClassDef searches, preserving the no-GDEF same-ID fallback distinction. The frozen same-ID witness already passes; it is a preservation edge, not a claimed bug. |
| LUT02 | R33 | Accepted. Font compilation retains consumed read/write/first-symbol, child, kind, window and insertion-bound summaries. Configs perform selection/dependency unions, not compact-test/bucket reconstruction. Removed per-config and added per-font visits are separated below. |
| RUN07 | R34 | Accepted. Active-range bounded native probe and independently bound native/symbol/metadata families. One glyph at slot 8,192 behaves like one glyph alone; mixed-family and huge-then-small poison-backed reuse passes without warm allocation or outside mutation. Dense contextual cost remains explicit. |
| CACHE01 | R35 | Accepted simple layout. Dead run-output fields/stores removed; canonical slots, links and free-list remain. Measured 96-byte glyph/8-byte link/4-byte free entry; no compaction, hot/cold mirror, translation or alternative traversal prototype. Field/cache-line footprint below is an address-layout observation, not a cache-miss measurement. |

### 9.3 Cold construction, summaries and remaining proof work

`coverage-{control,current}.out`, LLVM exports and `final-work-ledger.json` cover one bundled-font load plus the same eight-cycle, 96-runs/cycle high-level workload: 352 codepoints/run for four cycles, then 11 for four, totaling 139,392 glyphs and two distinct language configurations. Config instrumentation includes sizing/materialization calls; it is not a count of distinct cached objects.

| Scoped work | Frozen | Current |
|---|---:|---:|
| Source-predicate full-domain visits | 4,054,092 | 84,966 class decodes |
| Additional grouped-source writes | — | 84,966 class links; 1,273 coverage members; 242 format-2 mappings |
| Global predicate/symbol refinement visits | 582,624 | 582,624 |
| Identical count/fill dispatch membership tests | 83,232 | 41,616 |
| Lookup-major eligibility glyph visits | 4,758,096 | Replaced by combined glyph-row construction |
| Separate lookup-major admission glyph visits | 4,636,716 | 0 |
| Combined glyph rows / glyph-lookup visits | — | 24,276 / 4,818,786 |
| Complete intermediate Raw cache allocation | 74,427 B | 0 B |
| Fusion compact-test / subtable recovery calls | 3,353 / 1,671 | 0 / 0 |
| Font-phase lookup-decode counter | 3,381 | 930 checked construction decodes |
| Execution lookup-decode counter, all eight cycles | 511,104 | 0 |
| ClassDef helper calls, whole workload | 299,374 | 84,966, all cold grouped-class work |
| Font allocator requests / cumulative requested bytes | 66 / 2,507,420 | 216 / 3,440,303 |
| Font live / peak allocator bytes | 1,572,543 / 2,225,961 | 1,674,450 / 3,333,091 |

The current font has seven class sources and 189 coverage sources. Only 39 coverage sources need membership bitsets; index-only rules borrow format-1 indices or use one normalized format-2 mapping. This removes unused bitsets and the zero-before-full-overwrite predicate clear. The live native blob is 1,516,384 bytes; the final contextual cache is 83,267 versus 56,159 bytes. Its unchanged classifier/predicate/test/bucket/outcome/fusion payloads are 2,816/5,609/4,312/8,249/22,320/1,176 bytes. The growth includes 27,100 bytes of consumed lookup summaries; the separate shared font table allocation includes the 48,552-byte GDEF class LUT. Raw input bytes, requested allocator payload and resident payload are distinct; no RSS observation is claimed.

Removed `GsubPlanDomains` ran 310 times across the two materialized configs: 316 program visits, 2,608 candidates, 4,660 tests, 16,116 symbol/bucket visits and 1,420 bucket candidates. Current font summary construction runs once: 465 lookups, 472 subtables, 816 logical rules and 402 child records; its lowerable rules require 1,616 domain-word unions, 1,742 test visits and 565 distinct read-predicate unions. Backtrack safety still examines 4,643 source symbols and 290 predicate entries. These are not zero-cost summaries.

`completion-audit/audit-compiler-work-results.json` records the source expressions, LLVM regions and source hashes behind the config/summary counts. Eligibility-matrix writes are **1,851→1,885**, not a reduction. Necessary dependency pairs/word visits remain two/two; selected-root summary reads are 387. Retained dispatch removes one of two 41,616-test membership passes, but emission test visits rise **1,761→2,355**; bucket writes remain 310.

Remaining necessary passes are explicit: global symbol refinement scales with glyphs × predicates; ordered dispatch with symbols × rules; fusion proves each eligible action/domain and selected winner; backtrack safety compares source/replacement predicate membership; insertion bounds use up to the existing 32-frame action depth with saturated arithmetic; configs union selected roots, compute RAW/WAR/WAW ordering and fill actual glyph-major admission rows; final packing sizes, interns and emits resident bytes. FusionDomain/Constant/Winner/Predicate call counts remain 2,696/596/292/730 on this font. Their removed source decoding, repeated child maps and nonanchor winner tests are not a claim that the proof itself vanished.

The bundled run has **no positive winner-cache reuse**: its 730 fusion-predicate calls are unchanged. A separate retained-format-2 fixture with two real symbols and three queries proves the reusable case: six unfactored per-symbol predicate checks become three checks plus three cached reuses. This controlled proof is not relabeled as a bundled-font win. Three configurations across four feature modes separately demonstrate one font-summary build and 216 resident summary bytes.

Interner results (`interner/results-{control,current}.json`):

| Font | Distinct payloads | Capacity, frozen→current | Longest chain | Actual chain probes | Bytes hashed |
|---|---:|---:|---:|---:|---:|
| Noto Sans | 20 | 4×256→32 | 2→3 | 10→26 | 590→590 |
| Bundled JetBrains Mono | 390 | 4×256→512 | 10→7 | 455→666 | 30,018→30,018 |

Hashing is fused with necessary final-byte emission, not eliminated; hashes survive interner growth. One shared table replaces four fixed families, at the measured probe tradeoff. The synthetic 1,027-distinct-payload proof grows to 2,048 slots, longest chain four, 1,033 successful-search probes and 4,120 unique payload bytes. Two different eight-byte payloads deliberately share FNV hash `0x1b75c11c` yet retain distinct offsets; equal bytes share only when size/alignment also permit.

The default bundled Latin config has 198 selected root references (193 GSUB), 40 stages and a 16-symbol window. Its eligible-root bound is 198 rather than 925 font-wide entries. Heap resident payload is 883,247 bytes; fixed placement advertises 1,468,343 bytes; split construction requests 2,351,590 bytes. Admission rows occupy 194,216 bytes and the config matrix 679,728 bytes. Turkish has 199 roots, 41 stages and 883,303 resident bytes. A Devanagari request for this font selects USE and does not run irrelevant Indic probes. `config-work` separately proves native/nested locl expansion with exactly 7,199 advertised/touched bytes, zero sizing substitutions/sequences, output 21, odd placement and canaries. Saturating self-cycle insertion bounds reject unrepresentable sizing without execution or allocation.

### 9.4 Prepared input, workspace and canonical footprint

Paired high-level output checksums match each cycle; final consumed decimal checksum is `12639911568920250115`. Shape-config searches fall 768→8; glyph-config searches 139,392→8; cmap ID queries 278,792→139,396. Execution compiles nothing. First execution allocations fall 197→9, and bounded warmed cycles allocate nothing. Two-config retained allocator memory is **11,195,952 versus 8,314,593 bytes**, including fonts, prepared input and arena high-water capacity. Input records grow 48→72 bytes; glyph records shrink 104→96. This is not an overall memory reduction.

`fallback-work` alternates both priorities of real Noto Sans/Arabic fonts over repeated and changing Latin/combining/Arabic input. Both engines produce identical font choices, sources and geometry in 576 total runs. Cmap ID queries fall 4,240→2,632 and glyph-config searches 1,664→512. Post-audit Unicode decomposition queries are **6,920→9,956**, down from the pre-audit 16,036 but still an increase over the frozen control: missing-glyph coverage certification must discover alternatives that the old dead parent loop skipped.

The single-font workload now performs **329,476→329,474** decomposition queries, not the pre-audit 658,946. Nominally covered input no longer eagerly enumerates parents; missing-only resolution visits each parent once while preserving singleton-alias priority. There is no duplicate coverage parent list or general grapheme cache. The priority probe compares all 2,431 parent/combining cases for each of eight real fonts.

An additional paired workload includes combining marks, ZWJ/ZWNJ and U+10FFFF under both font priorities. All four cycle checksums and complete font/source/geometry outputs agree. Cmap queries are **5,136→3,272**, parent-info queries **3,720→1,924**, and decomposition queries **7,176→10,212**. These are separate work scopes; no claim that every normalization operation decreased.

Workspace evidence (`workspace_probe.final.out`):

- Native-only first use at 130 slots requests 54 bytes, retains 54, clears 32 and allocates no symbol family. Growth to 257 slots copies/moves 24 live bytes and zeros a 16-byte tail. Contextual first use separately requests 538 bytes. One hundred warmed mixed operations allocate nothing.
- One active native glyph, alone or at physical slot 8,192 of 8,193, probes one active glyph and builds/allocates no index. After huge native warmup and contextual-family warmup, 100 high-slot native/context/native cycles allocate zero, preserve all outside IDs/sources/references and clear 104,800 necessary readable bytes. Retained stride is 256 words while the current readable extent is 129 words. Native-only intervals do not initialize dormant contextual rows.
- Dense symbol storage still scales with global symbols × retained slot words; native storage with currently relevant native rows × retained slot words, plus validity metadata. Neither config reachability nor a smaller match window removes this cost. Binding layout identity matters even at equal glyph capacity.
- Actual bundled Latin/Noto Arabic configurations retain equal 256-slot capacity but have 51/24 symbols and 40/24 stages. Across 102 alternating bindings, all retained raw row families and alignment slack are poisoned before reuse; complete output matches independent fresh-scratch shaping, with zero allocations after warming both layouts.
- Queue ordered/tombstone/growth workload: six allocations, 6,022 requested bytes, 3,130 retained, 4,669 peak; 130 visited entries, one tombstone, 1,488 compaction bytes and 516 membership bytes. Disorder/ties/rebucket retains 6,455 bytes and performs 4,632 merge bytes. Fifty warmed sorts allocate zero but still perform 314,400 copy bytes, 231,600 merge bytes and 26,000 membership bytes. Block conversion is zero, not all queue traffic.
- The runtime queue probe uses 130 glyphs, an interior tombstone, growth and live rebucketing between two actual GPOS contextual lookups. It observes all 259 adjustments exactly once: source 17 has advance 13 and the others 20. A separate populated-growth failure preserves both old queue contents and old membership.
- Paired native-backtrack probes visit 522 physical glyphs, gather 519 symbols and execute seven matches each. The old extra 519 reference plus 519 offset stores (4,152 bytes) and 522 distance decrements are gone. Forward action metadata remains.
- Paired ffi/mark attachment cases preserve all ten full tuples. Both make 13 attachments and 12 common interval stores; the old additional 12 mark-to-ligature interval stores (48 bytes) disappear.
- Sorted/reverse/tied 130-slot scenarios preserve slot/source order. Splices fall 16,705→257; six primary link/head/tail stores per splice give 100,230→1,542 writes. Evaluated source-level link reads fall **84,109→18,317**; the necessary 16,899 predecessor-link reads and 390 forward-link reads are unchanged. These are source-expression counts, not machine-load or cache-miss measurements.
- A paired unordered 130-candidate traversal removes **130 exact-position bitmap rechecks**, retaining every candidate and first-symbol eligibility. Paired real Devanagari/Bengali high-level and direct shaping removes **80 syllabic-position restoration stores** with identical complete tuples and 28 BeginCluster/EndCluster calls on each side.

`layout-{control,current}.txt` records actual compiler offsets. Current canonical fields: ID/symbol at 8/12, source index at 16, offsets/advances/attachment at 20–36, config pointer at 40, decomposition at 48, GDEF classes at 56, flags at 60, parent info at 64, queue membership at 68, sort key at 72, bucket index at 76 and ligature/script properties at 80–92. Initialization/normalization use Unicode/decomposition/class facts; gather uses symbol/filter fields plus links; substitution changes live ID/class/admission; sort uses keys/links; GPOS uses metrics/attachments/queue fields; output consumes source/geometry. The 96-byte, eight-aligned record spans two or three 64-byte cache lines depending on base alignment; separate eight-byte links add a dependent access for traversal, and a four-byte free-list entry is distinct slot metadata. Config/classifier/matrix loads remain dependent accesses. These footprint facts are not PMU measurements or a locality win. Stable references, sentinel checks and no glyph-suffix moves survive fragmentation and huge-then-small reuse.

### 9.5 Intentional differences and additional redundant work

`observer/difference-classification.json` accounts for all 337 measured differences plus five warmups; complete tuples remain in both full reports:

- **Arabic: 245 measured cases plus five warmups.** GSUB lookup 35 is format-3 context with IgnoreMarks. Its init/medi subtables execute `(0, child), (1, child)` with child 2/3; parent matching crosses the ignored joiner. Old action targeting rescanned with different flags and missed the alef target. Correct live targeting changes alef.fina glyph 9 to 10 or 11, with its corresponding advance, while preserving source/other tuple fields. Minimal `ل‍ا` and `arabic-live-target` establish the expected live record target. The owned shaper's pre-existing combined input/lookahead manual-ZWJ policy differs from HarfBuzz; redesigning that policy is not part of this cleanup, and this is not labeled a HarfBuzz bug.
- **Indic: 42 Devanagari and 50 Bengali measured cases.** Dotted-circle insertion left the post-reph boundary before the inserted base; reverse base search also entered an empty post-reph interval, and reph joiner extension could cross the cluster end. Those invalid intervals allowed attachment/key processing to escape the cluster. Correct boundaries preserve the existing no-base fallback: bare RA+HAL is not shaped with rphf as though a following base existed. Devanagari reph 506 becomes RA 82/HAL 103; Bengali reph 132 becomes RA 51/HAL 70. KA+HAL+ZWNJ retains KA/HAL instead of an incorrectly enabled half KA. The eight-case `dotted-circle-boundary` regression checks following-cluster features and exact standalone-equivalent output. Normal matra/reph/half/equal-mark paired cases remain exact. No difference is left unexplained.

Additional source-confirmed work removed during integration, beyond merely deleting review comments:

- Unused coverage-membership bitsets for index-only predicates and clear-before-complete-overwrite scratch initialization.
- Outcome×symbol source-bit duplication, repeated fusion child mapping/nonanchor winner checks and reconstructed parent ambiguity.
- Overlarge per-stage eligible-root bounds, irrelevant script probe discovery and unused config probe-state fields.
- Unused public font shaping-table mirror, dead run output counters, unused action arguments and match/attachment bookkeeping. `MatchCoverageSequence` itself remains where native reverse-context matching consumes it.
- Duplicate immediate cmap lookups between fallback selection and glyph initialization; repeated parent cmap queries after a successful recomposition.
- Prepared-key invalidation at execution rather than preparation, cache wrappers that continued after a hit, and destruction/rebinding paths that consulted expired old storage.
- Boolean conversion of an upper-bit feature mask, the dead fallback parent loop, the Hangul range edge and escaped Indic cluster intervals were real correctness defects, not additional optimization projects.

Explicitly unnecessary for this cutover: editor/renderer integration, a new shaping engine, a repeated-grapheme memo, global mutable caches, glyph templates or hot/cold mirrors, alternate queue/traversal designs, narrow eight-bit mark-attachment classes, an unrelated font-pop ownership redesign, compression experiments, a permanent profiling subsystem, PMU studies or timing/recovery gates. Mark-attachment class 258 is valid in the exercised contract and must not be truncated. Existing variation/bidi/CJK/stch capability TODOs are not silently promoted into this cleanup. Historical performance results remain historical.

API and implementation documentation is updated in this plan, `IMPLEMENTATION.md`, `readme.md` and the public header. Native owning construction now owns its bytes; aligned public native loading still borrows as documented. PlaceBlob produces a complete serializable image before compilation. Native-copy/early-source-release/idempotent-compile proof compares every Unicode cmap input and three actual shaped results; the frozen owning-native witness reports use-after-free, current passes. Fixed-memory behavior, sticky context errors, feature-stack lifetime and borrowed result lifetime remain explicit. Symbol-aware lookup was attempted; no language server was available, so scoped complete caller inventories and actual builds covered migration instead.

### 9.6 Completion audit after the rapid TODO closure

The original mass completion update was delayed bookkeeping, **not sufficient acceptance evidence**. Several clauses had been marked complete on source review or narrower probes: the cluster probe had no post-error shaping, the high-level allocator never failed, cache hits did not cross multiple blocks, and some font/compiler boundaries and operation counts were unexercised. The original blanket closure was therefore not fully justified.

The audit covers **all 139 checklist items**: three independent reviewers assessed 132; the integration owner assessed the seven P0 requirements and closed the resulting gaps. `completion-audit/completion-reviewers.json` preserves the original assessments; `completion-audit/completion-matrix.json` records every item and its closure evidence. The audit required source changes, not merely revised wording:

- **Arena allocation failure:** return before linking the sentinel as a block. The before-fix public failure and leaked ownership are retained separately. A permanent public-context regression now warms real shaping, fails later input growth, checks sticky error behavior and verifies balanced destruction.
- **Shape-config placement capacity:** add the explicit fifth `MemorySize` argument and reject insufficient capacity before writing. The active public caller is migrated; there is no compatibility shim and the upstream vendor remains immutable.
- **Duplicate normalization discovery:** remove eager nominal-coverage parent enumeration and the duplicate parent-list representation; resolve missing-only alternatives in one walk with singleton priority. The final work counts and fallback tradeoff are above.

New proofs include public valid/malformed standalone/TTC/native construction; distinct TTC faces mapping A to 37 and 0; 65 KiB malformed inputs whose advertised outputs would overflow int/u32; direct/wrapped reverse direction counts; controlled GDEF warm counts; SIMD bucket edges 3/4/5, 7/8/9 and outcome widths 65,535/65,536; positive winner reuse; GSUB aggregate index 65,999 followed by actual GPOS queue index 66,000; zero/shared/DFLT/later-enabled scope; and repeated summary consumers.

Public runtime verification separates one input, nine preparation and two execution allocator requests and fails **every one**. All twelve failures preserve sticky errors and balanced destruction; fresh contexts subsequently shape successfully. Bounded alternating-script runs allocate nothing after warmup and execute no constructors. Fixed-context exhaustion/canaries, forty-entry cache histories, populated queue failure, same-parent ligation, filtered GPOS targeting, live rebucketing and real equal-capacity/different-layout reuse close the remaining lifetime/action gaps.

Final post-audit verification is captured rather than reconstructed from memory: `verified-just-check.json`, full sanitized `verified-context-{isa}-run.json`, eight compiler and six observed/boundary font runs, production/sanitized 555-case default verifiers, and both sanitized 64 KiB verifiers. Complete stress observers compare **20,792 non-header records per ISA** against the independently audited report: every input, result, output checksum and full failing tuple agrees. `verified-corpus-equality.json` records 9,423 exact measured cases, the same 337 explained measured differences and five Arabic warmup differences. Strict exit 1 is preserved.

Intermediate compile mistakes, incomplete/early-stop runs and pre-fix failures are retained as such, not used as passing evidence. Historical profile source snapshots are not overwritten by later header changes; the sort counter was rebuilt when its earlier line mapping proved stale. Timing/PMU gates remain deferred, the no-GDEF and fallback-heavy tradeoffs remain explicit, and no untested acceptance clause is silently declared optional.

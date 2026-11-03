# Shared-body-exit assembly investigation

**Correctness correction:** the switch version measured in section 4 assigned `l0_tag` only in `default`,
so several marker kinds became paragraphs. Those timings do not represent a correct switch implementation.
The fix moves the non-NONE assignment after the switch; the harness now also checks expected marker tags,
not just agreement between variants. Corrected switch matches the previous if-chain in release and sanitizer checks.

These are observations from actual linked Clang/GCC binaries, not estimates from C statement counts.
The benchmark runner and C harness live here; snapshots, disassembly and raw measurements remain in the
output directories listed below. No production parser changes were made during this investigation.

## 1. Why Clang improved after sharing the body exit

Frozen comparison: `/tmp/md-marker-bench/reusable-shared/`.
The baseline is the outer-whitespace version immediately before sharing the body exit.
The candidate is the shared exit with the earlier **if-based** L0 classification.

Clang's baseline spills the current tag through the stack on the ordinary no-refill path:

```asm
; Load next tag at md_create_baseline+0x1548.
12eb8: mov    ecx,r14d
12ebb: movzx  ecx,BYTE PTR [r9+rcx]
12ec0: mov    QWORD PTR [rsp+0x40],rcx
; Next iteration, before dispatch:
126ac: mov    rbx,QWORD PTR [rsp+0x40]
126b1: cmp    bl,0x21
```

The candidate carries the tag in BL instead:

```asm
15430: mov    ecx,ebp
15432: movzx  ebx,BYTE PTR [r15+rcx]
15437: inc    ebp
; Next no-refill iteration dispatches directly on BL:
14d1f: cmp    bl,0x21
```

Likewise, the old prefix state is repeatedly accessed at `[rsp+0xc]`; the new no-refill path carries it in EAX,
using `test eax,eax`. It spills/restores that state around refill/scanner work instead of on every ordinary run.

This is a concrete reduction in **hot-state spill/reload dependencies**, even though total instruction count rises.
It is not proof that every saved cycle comes from one spill. Cycle samples can skid; a large sample percentage
on a stack load is not an isolated measurement of that load's latency.

Counter replication on the same two inputs:

| Clang | Instructions | Cycles | IPC |
|---|---:|---:|---:|
| README | +3.7% | -4.7% | 3.62 → 3.94 |
| Dense | +3.4% | -9.9% | 4.12 → 4.73 |

The candidate also spills the `run_index < run_count` predicate to a stack slot on its common path. Register
allocation is a trade-off, not wholesale elimination of stack traffic.

## 2. Why GCC did not get the same improvement

In the same frozen comparison, GCC's baseline already carries `run_end` in R8D, but the shared-exit candidate
spills it and reloads it repeatedly:

```asm
85ba: mov [rsp+0x184],edi       ; retain run_end
85c9: mov edi,[rsp+0x184]       ; derive run_length
...
862a: mov edi,[rsp+0x184]       ; advance
8631: mov [rsp+0x80],edi
...
865f: cmp [rsp+0x184],ecx       ; outer-loop bound
```

The tag still travels through `[rsp+0x1a3]` on that path. Thus this refactor makes GCC's allocation worse rather
than inheriting Clang's allocation. Dense retired instructions increase about 7% and cycles about 12%.

## 3. A source-level way to influence this: the existing likely macro

Tested as a temporary candidate, changing only the shared exit:

```c
if (likely(!at_line_start)) {
    line_metadata.trailing_whitespace = false;
    goto next_run;
}
```

This supplies branch-frequency information to optimization/register allocation. It does not add a condition,
force a CPU branch prediction, or pin a variable to a particular register.

On the frozen if-based classifier, GCC then keeps both hot values in registers:

```asm
; Compute/use run_end in R8D.
860c: mov r8d,[rdi+rdx*4]
8610: add r8d,eax
8613: mov edi,r8d
; ... L1 emission ...
8674: mov [rsp+0x80],r8d        ; advance without reloading run_end
8681: mov rdi,[rsp+0x1b0]
8689: mov edx,r9d
868c: add r9d,1
8690: movzx r11d,BYTE PTR [rdi+rdx] ; next tag remains in R11B
8695: cmp r8d,[rsp+0x190]       ; bound check without run_end reload
```

Clang also rearranges its allocation: the hot tag stays in a register and the next-boundary predicate can stay
in CL, replacing the per-run predicate spill. Prefix state may instead live on the stack. There is no single
register assignment that keeps every variable resident.

Across the six earlier inputs (original/large/fragmented README, prose, spec, dense), the hint showed about
**+8.9% Clang / +8.8% GCC** paired thread-CPU geometric-mean throughput. README/dense retired instructions
fell **5.6–7.2% Clang / 7.3–9.9% GCC**. These are results for that frozen classifier, not the newer source edit.

Raw runs:
- `/tmp/md-marker-bench/reusable-likely/`
- `/tmp/md-marker-bench/reusable-likely-rest/`
- `/tmp/md-marker-bench/reusable-likely-fragmented/`

## 4. Retest against the user's newer switch classifier

The source changed to a `switch` during this investigation. The runner compared another pair of frozen headers
with that switch, differing only by the same `likely` hint. This newer baseline already keeps run_end in a
register; the earlier run_end-spill explanation must not be applied wholesale to it. The hinted GCC version
still promotes the hot tag to R11B instead of storing/reloading it between ordinary runs.

| Latest switch version + hint | Paired CPU throughput | Retired instructions | Cycles |
|---|---:|---:|---:|
| Clang / README | +1.8% | -6.2% | -1.1% |
| Clang / dense | +6.5% | -9.1% | -3.3% |
| GCC / README | +0.1% | -3.3% | +1.2% |
| GCC / dense | +4.7% | -4.6% | -2.0% |

So the latest README timing is essentially neutral for GCC, not another 9% win. The retired-work reductions
remain clear. On the original small README, a further run showed +5.0% Clang / +1.1% GCC, with instructions
-6.3% / -3.3%.

Outputs: `/tmp/md-marker-bench/reusable-switch-likely/` and `/tmp/md-marker-bench/reusable-script-check/`.
Both compiler self-checks, full metadata/output fingerprints and separate Clang ASan/UBSan checks passed.
Timings remain subject to shared-resource/frequency interference; wall/thread-CPU gaps were below 0.4%.

## Conclusion

The useful transferable property is **prioritizing tag/end/boundary state on the body path**, not copying a
special arithmetic instruction or replacing C with inline assembly. The smallest demonstrated source-level
candidate is the existing branch hint. Its code generation and benefit vary with the surrounding classifier,
so preserve snapshots and recheck it when that code changes. The hint remains a temporary candidate, not an
unmeasured edit to the concurrently changing production parser.

## 5. Register-pressure follow-up: corrected switch

Baseline: `/tmp/md-marker-bench/register-pressure-before.h`, including the corrected switch assignment.
No production changes were made while running these experiments. The two refill hints were subsequently
applied at the user's request.

### Actual GCC allocation on the no-refill body path

| State | Baseline location |
|---|---|
| Cursor piece / piece offset | R14D / R13D |
| Buffer file offset / length | EAX / EBX |
| Run index / count | R9D / R15D |
| Line indent / prefix flag | R11D / ESI |
| Current run end / length | R8D / EDI |
| Current run offset / tag | Stack slots `[rsp+0x80]` / `[rsp+0x1a7]` |

The two cursor fields remain live in registers between refills, although the body does not use them.
GCC also uses RBP as the frame anchor for its 32-byte-aligned stack; Clang's baseline uses RBP as a general
register instead. So these builds do not even have the same number of allocatable GPRs. This is one reason
not to assume identical C variable counts imply identical pressure or allocations.

### Three small experiments

Each uses the same reusable runner, baseline, three inputs (large README/spec/dense), 11 paired rounds,
full metadata fingerprints, independent expected-marker checks and separate Clang ASan/UBSan checks.
All correctness checks passed.

| Candidate | Clang paired CPU geomean | GCC paired CPU geomean | Retired instructions |
|---|---:|---:|---|
| Merge line_indent/indent into one running column count | -6.9% | +0.6% | Clang -3.2–4.6%; GCC essentially unchanged |
| Mark existing md_fill noinline | -6.7% | +2.4% | Clang 0–1.9% fewer; GCC 1.7–2.8% fewer |
| Mark both refill-index tests unlikely | +7.5% | +5.9% | Clang 4.0–4.7% fewer; GCC 3.9–4.7% fewer |

The indentation merge makes Clang spill its tag, despite having one fewer source variable and fewer total
instructions. GCC's unchanged instruction counts show no meaningful benefit from removing that source variable.

Outlining md_fill removes the persistent inlined cursor state, but **does not** make GCC retain the tag between
ordinary runs: it still stores/loads `[rsp+0x78]`. Freeing a register does not dictate what the allocator puts there.

The smallest positive candidate is instead:

```c
while (unlikely(run_index == run_count) && buffer_file_offset + buffer_length < source->total_length) {
```

Apply to the outer and prefix-whitespace refill tests only. No scanner or state changes. GCC's run_offset then
stays in R11D on the no-refill body path (advance is `mov r11d,r13d`), replacing the old stack store/load cycle.
The tag still spills. Clang carries run_offset in EBP and lexical_tags in R10 on its corresponding path.
These are observed allocation changes, not a guarantee that branch hints always improve allocation.

There was substantial machine interference during the outlining/hint runs: wall/thread-CPU gaps reached about
5.4% / 2.9% respectively, versus below 0.4% in the indentation run. CPU timing still includes shared-resource
and frequency interference, so the small timing differences are provisional. The roughly 4–5% instruction
reduction from the refill hints is stronger evidence of reduced executed work than the +17% README timing.

Outputs, source snapshots, linked assembly, raw trials and counters:
- `/tmp/md-marker-bench/register-pressure-indent/`
- `/tmp/md-marker-bench/register-pressure-fill/`
- `/tmp/md-marker-bench/register-pressure-refill/`

The runner now supports a changed md_fill by cloning that helper under a candidate-only name, preventing
candidate noinline attributes from changing the baseline's compilation. Types/other preceding helpers and
scanner remain shared and checked as before.

### Pinning is not the useful lever here

C register is only a hint; local GNU register-asm declarations constrain extended-asm operands, not an entire
C variable lifetime. Empty asm register operands force residency at a point, not between all uses. GNU global
register variables / fixed-register options have translation-unit and ABI implications and are not a safe,
portable local-parser optimization. volatile generally adds memory traffic rather than fixing allocation.
The useful levers here are actual live-state reduction and accurate hot/cold frequencies (hints or PGO),
verified against linked code and counters rather than source declaration counts.

## 6. Volatile cursor: current unlikely baseline

Tested the user's suggestion with both qualifiers present:

```c
volatile MdCursor cursor = {.source = source};
// Existing helper signature:
internal u32 md_fill(volatile MdCursor* cursor, u8* buffer, u32 capacity)
```

Changing only the local declaration would discard volatile at the old helper boundary, so it would not test
volatile field accesses correctly. The candidate changes both the local and helper parameter; types/layout,
scanner, corrected switch and both existing unlikely refill hints otherwise stay unchanged.

**GCC now keeps run_tag in R10B between ordinary iterations**, replacing the previous stack round-trip:

```asm
a3dc: movzx r10d,BYTE PTR [rdi+rsi]
; next no-refill dispatch:
a359: cmp r10b,0x20
```

Cursor accesses instead use volatile memory operations during refill. This forces accesses, not a general
register reservation for run_tag; the observed promotion remains a compiler-allocation result.

| Input | Clang paired CPU throughput | GCC paired CPU throughput |
|---|---:|---:|
| Large README | -11.2% | +8.4% |
| Spec | +0.6% | +13.2% |
| Dense | -2.2% | +10.8% |
| Large README, 64-byte pieces | -4.6% | +6.1% |

Contiguous-case retired instructions fall 3.2–6.4% for Clang and 2.6–3.4% for GCC. Despite fewer instructions,
Clang's cycles increase (README +11.9%, spec +2.0%, dense +1.6%). Thus volatile is not a portable throughput win.
Wall/thread-CPU gaps were below 0.4%; that still does not exclude shared-resource/frequency interference.

Both compiler self-checks/expected-marker cases and full metadata fingerprints passed. Separate Clang
ASan/UBSan checks also passed. No volatile change was applied to production.

Artifacts and replay commands:
- `/tmp/md-marker-bench/volatile-cursor/`
- `/tmp/md-marker-bench/volatile-cursor-fragmented/`

Both contain manifests, frozen sources, linked disassembly, counters, raw trials and summary tables.

## 7. Volatile cursor reruns while the user kept the CPU quiet

Repeated the same comparison twice sequentially, including fragmented input, on CPU 6. Each pass still uses
11 rotating-order timing rounds and separate counter runs. Verified identical frozen source hashes and compiler
versions across all four outputs. Production still matches the nonvolatile baseline, including both refill hints.

Paired thread-CPU throughput deltas:

| Input | Clang pass 1 | Clang pass 2 | GCC pass 1 | GCC pass 2 |
|---|---:|---:|---:|---:|
| Large README | +0.5% | -7.1% | +7.9% | +9.3% |
| Spec | +3.4% | +3.7% | +12.2% | +9.9% |
| Dense | +6.8% | +1.5% | +11.5% | +10.9% |
| README, 64-byte pieces | -13.8% | -2.0% | +5.9% | +9.2% |
| Contiguous-input geometric mean | +3.6% | -0.8% | +10.5% | +10.1% |

Wall/thread-CPU gaps remained below 0.4%. The earlier blanket Clang slowdown should not be treated as a
settled result: spec and dense improved in both quiet timing passes, whereas README/fragmented timing still
varies substantially. GCC's benefit repeats clearly, including fewer cycles on all inputs in both counter passes.
Clang's independent counter runs also remain variable (e.g. spec cycle delta +12.4% / -1.9%); do not equate the
separate counter samples with the paired timing batches or claim that every Clang case is now faster.

The retired-instruction reductions reproduce. Quiet use reduces one source of interference but does not itself
establish the cause of remaining variability. No additional parser edit was applied.

Artifacts:
- `/tmp/md-marker-bench/volatile-cursor-quiet-1/`
- `/tmp/md-marker-bench/volatile-cursor-quiet-2/`
- `/tmp/md-marker-bench/volatile-cursor-quiet-fragmented-1/`
- `/tmp/md-marker-bench/volatile-cursor-quiet-fragmented-2/`

## 8. Why GCC benefits; what differs in Clang

Inspected the linked hot-loop assembly in `volatile-cursor-quiet-1`, not just the C declarations.

| Hot state | GCC baseline → volatile | Clang baseline → volatile |
|---|---|---|
| run_tag across ordinary iterations | stack `[rsp+0x1e8]` → R10B | BL → BL, already resident |
| at_line_start check | load `[rsp+0x1b0]`, test → test R11D | stack check → stack check |
| trailing_whitespace update | independent byte store → independent byte store | packed word in ESI → packed stack word |

GCC baseline fetches the next tag, stores SIL to `[rsp+0x1e8]`, then reloads it into EBX on the next iteration.
The volatile candidate fetches directly into R10D and dispatches on R10B without that round-trip. It also tests
the prefix state directly in R11D rather than first loading a stack slot. Its L1 output remains three scalar
stores: there is no newly vectorized output loop or special arithmetic instruction to transplant.

This is consistent with shorter hot memory dependencies, not better branch prediction: in quiet pass 1,
GCC retired instructions drop 2.6–3.4%, but branches rise 1.3–1.8% and branch misses do not decrease.
Counters do not isolate exactly how many cycles each register promotion saves.

Clang already keeps run_tag in BL. Its volatile candidate keeps l1_offsets in R8 on the L1 path, saving one
base-pointer reload and several shuffles, but also changes the trailing flag update:

```asm
; Baseline, clear packed trailing flag in a register:
122b5: and esi,0x7fff
; Volatile candidate, clear it with a stack read-modify-write:
14cbd: and DWORD PTR [rsp+0x130],0x7fff
```

This update runs on the body path, not just at refill. The stack RMW has an old-value memory dependency;
its register counterpart does not need that memory round-trip. This is a concrete adverse code-generation
trade-off, not proof that it accounts for all Clang timing variability. Volatile only changes the cursor's
access requirements directly; the metadata spill is an indirect allocation consequence.

A focused C representation to test next is making trailing_whitespace its own byte rather than a packed bit,
using existing metadata padding after marker_byte. Preserve the 12-byte size assertion and rebuild all readers.
Then a spilled flag can be updated with a byte store rather than a packed-word RMW. Alternatively, keep that
pending fact as a separate scalar and pack it only when emitting L0 metadata. Neither approach guarantees
particular register choices; both express the important distinction (independently writable hot flag) in C.
No metadata-layout change was made during this inspection.

## 9. Split-metadata constructor: throughput, counters and memory

The subsequent constructor migration removes trailing_whitespace entirely and replaces dense 12-byte L0
metadata with 4-byte Common per L0 plus 4-byte Extra per nonparagraph L0. The benchmark now supports both
native return layouts, using constant-size pointer/count adapters and normalizing retained metadata facts
outside timing. `md_get` remains excluded and unchanged by this benchmark update.

Baseline: `/tmp/md-marker-bench/volatile-cursor-before.h` (despite the filename, **nonvolatile** cursor,
corrected switch and both unlikely refill hints). Candidate: current constructor captured in each run's
`candidate-source.h`. Release AVX2, Clang/GCC, CPU 6, eleven alternating rounds, warm reused arenas,
full output allocation/copies. Two full runs and two 64-byte-fragment runs were retained:

- `/tmp/md-marker-bench/split-metadata/`
- `/tmp/md-marker-bench/split-metadata-repeat/`
- `/tmp/md-marker-bench/split-metadata-fragmented/`
- `/tmp/md-marker-bench/split-metadata-fragmented-repeat/`

### Throughput

Latest repeat, wall GB/s, old → new:

| Input | Clang | GCC |
|---|---:|---:|
| Original README | 2.826 → 2.770 | 2.964 → 3.390 |
| Large README | 2.393 → 1.851 | 2.474 → 2.708 |
| Prose | 5.356 → 4.820 | 5.241 → 5.523 |
| CommonMark spec | 1.145 → 1.182 | 1.110 → 1.385 |
| Dense markers | 0.897 → 0.911 | 1.015 → 1.095 |
| Large README, 64-byte pieces | 2.167 → 2.183 | 2.294 → 2.466 |

Across the five contiguous inputs, paired thread-CPU geomean improvement was GCC +12.4% / +12.2%,
Clang -1.4% / -6.5%. Fragmented improvement was GCC +16.3% / +7.5%, Clang +0.6% / +0.7%.
The large Clang README varied especially strongly: candidate 2.235 then 1.851 GB/s; paired CPU delta
-3.1% then -22.7%. Do not treat that large regression magnitude as settled. The first full run had
wall/CPU ratios 1.022–1.033; the repeat had 1.002–1.003. A small gap does not exclude other interference.

### perf stat

Whole-process counters include setup, fingerprints and warm-ups; they are not isolated function costs.
Instructions/processed byte reproduced across both runs:

| Large input | Clang old → new | GCC old → new | Repeat cycle delta Clang / GCC |
|---|---:|---:|---:|
| README | 8.64 → 8.46 | 7.97 → 7.19 | -0.4% / -6.0% |
| Prose | 4.36 → 4.44 | 4.24 → 4.03 | +8.4% / -4.9% |
| Spec | 16.18 → 15.31 | 14.31 → 12.45 | -3.4% / -18.2% |
| Dense | 26.18 → 25.99 | 23.16 → 20.27 | -6.1% / -10.5% |

GCC retires 5–13% fewer instructions. Clang is mixed; prose retires 1.9% more instructions and slowed
in both runs. Separate counter batches need not match paired timing: the first Clang README cycle
sample was -30.6%, inconsistent with paired throughput and not reproduced. Raw cycles, instructions,
branches and branch misses, plus linked source disassembly, are saved in every run.

### Memory

Exact retained index arrays and L0 metadata, plus selected-only fresh-process peak RSS (latest Clang run).
MiB = 1048576 bytes. RSS includes input, runtime, document pieces, output and scratch; GCC RSS was similar.

| Input | Retained index MiB old → new | L0 metadata MiB old → new | Peak RSS MiB old → new |
|---|---:|---:|---:|
| Large README | 7.918 → 5.984 | 3.440 → 1.506 | 48.74 → 48.74 |
| Prose | 2.628 → 1.700 | 1.855 → 0.928 | 28.86 → 28.86 |
| Spec | 21.820 → 17.400 | 8.443 → 4.023 | 74.70 → 66.62 |
| Dense | 37.947 → 33.133 | 11.044 → 6.230 | 98.81 → 90.84 |

L0 metadata shrank 44–56%; the complete retained index shrank 13–35%. Measured RSS savings were
approximately 8 MiB for spec/dense and negligible for README/prose; do not infer RSS savings from byte
counts. The allocator requests huge pages and RSS is page-granular. For large README the worst-case
scratch allocated span decreased 671.47 → 607.52 MiB. This is not resident RAM or the total virtual mapping:
each arena still maps 1 GiB. Fragmented README retained the same index but used about 52.8 MiB peak RSS,
including its larger DocPiece array.

Mixed-schema and split-only self-checks passed Clang/GCC and separate Clang ASan/UBSan builds.
Split-only baseline=candidate checks are in `/tmp/md-marker-bench/split-metadata-harness-selfcheck/`;
initial mixed-schema sanitizer checks are in `/tmp/md-marker-bench/split-metadata-smoke/`.
Independent constructor source-oracle validation remains available through `check-create.py`.

## 10. Piece windows: aligned-prefix/32-byte bridge vs full-window batching

After the redundant-work cleanups, race the active `md_fill` against the corrected user `md_fill_v2`.
The latter skips empty/exhausted pieces before its direct fast path, advances the cursor, returns capacity,
and never calls memcpy on NULL-backed empty pieces. It requires capacity bytes of temporary storage.

Snapshots: `/tmp/md-marker-bench/fill-race-before.h` (active aligned-prefix helper) and
`/tmp/md-marker-bench/fill-race-batched.h` (same constructor, three calls switched to v2 and 4096 bytes of
scratch bridge storage instead of a 32-byte stack bridge). Production `md_create` was not switched.

Aligned prefix example: from a 4095-byte piece, scan 4064 directly and bridge the remaining 31 with the next
piece. Nonfinal calls remain multiples of 32, preserving carry and space-suppression alignment. V2 instead
copies across pieces to form a 4096-byte window. The final source window can be shorter in both versions.
This trades extra copying for fewer refills; neither approach is universally faster.

CPU 6, Clang/GCC release AVX2, eleven alternating rounds with full creation/copies and reused arenas.
Paired thread-CPU speedup of full-window v2 over the aligned-prefix helper:

| Pieces / input | Clang | GCC |
|---|---:|---:|
| Contiguous, original README | -2.3% | -2.5% |
| Contiguous, large README | -6.5% | +0.7% |
| Contiguous, prose | +2.7% | +1.4% |
| Contiguous, spec | -8.0% | +0.9% |
| Contiguous, dense | -6.6% | +1.6% |
| 64-byte, README | +27.0% | +34.7% |
| 64-byte, spec | +12.4% | +16.3% |
| 64-byte, dense | -2.0% | -4.1% |
| 4095-byte, README | -3.1% | +1.2% |
| 4095-byte, prose | -3.5% | -4.1% |
| 4097-byte, README | -9.3% | +0.7% |
| 4097-byte, prose | -1.7% | -1.1% |

Contiguous geomean: Clang -4.2%, GCC +0.4%. For 64-byte README, wall throughput was Clang
1.729 → 2.194 GB/s, GCC 1.795 → 2.413 GB/s. Instructions/byte fell 9.22 → 8.52 and 8.94 → 7.81,
respectively. Dense 64-byte fragments did not benefit despite the README/spec improvements.
Whole-process contiguous Clang instruction counts increased 4–8% on README/spec/dense; these are not
isolated helper costs. Do not attribute the complete difference to copy bandwidth or refill counts alone.

Retained index sizes were identical; batching added 4096 bytes of scratch allocated span. Peak RSS was
roughly unchanged. Wall/CPU ratios were 1.002–1.006, which does not exclude shared-resource interference.
Both variants passed the independent source oracle with Clang/GCC and separate Clang ASan/UBSan,
including 1/31/64/4095/4097-byte pieces and the direct/buffered helper checks.

Full results, memory, perf cycles/instructions/branches/misses and linked disassembly:
`/tmp/md-marker-bench/fill-race-{contiguous,64,4095,4097}/`.
Oracle snapshots/logs: `/tmp/md-check-create-ugyx8vsb/` and `/tmp/md-check-create-118amj8c/`.

## 11. Cursor and body-exit races after full-window caller wiring

Updated all three constructor calls to `md_fill(cursor, bridge, capacity, bytes)` and allocated a
4096-byte scratch bridge. Updated source-oracle helper checks to the settled single-helper API.
`md_get` was not changed. Production retains the ordinary cursor and unhinted shared body exit.

Frozen headers under `/tmp/md-marker-bench/cursor-races/`:
- `baseline.h`: corrected full-window constructor.
- `volatile.h`: volatile local cursor **and** volatile helper cursor parameter.
- `likely.h`: only the shared `if (!at_line_start)` exit becomes `if (likely(!at_line_start))`.
- `both.h`: both experiments. The earlier tag-0 early exit remains unchanged in every variant.

CPU 6, release AVX2, Clang/GCC, eleven alternating rounds with full creation/copies and reused arenas.
All groups collect perf cycles/instructions/branches/misses, native linked disassembly and selected-only
memory measurements. Changes below are geomean paired thread-CPU throughput improvements:

| Candidate | Clang contiguous | GCC contiguous | Clang gap-like | GCC gap-like |
|---|---:|---:|---:|---:|
| Volatile cursor | -2.7% | -7.3% | -2.0% | -9.0% |
| Shared-body likely | +2.1% | -8.4% | +2.7% | -10.4% |
| Both | -5.9% | -7.4% | +1.3% | -8.4% |

Contiguous covers original README plus large README/prose/spec/dense. Gap-like covers large
README/spec/dense using two large pieces split 64 bytes past 8 MiB, exercising a cross-piece fallback
rather than thousands of tiny fragments. Separate `*-large-pieces/` controls used a split exactly at
8 MiB (4 KiB-aligned). They likewise showed GCC regressions for all three variants.

Clang combined contiguous timing includes an especially large README slowdown (-24.5%) not reproduced
in the few-large-piece runs. Do not treat that magnitude as settled. Most wall/CPU ratios were
1.002–1.007, with one 1.027 case; low descheduling is not proof of uncontended execution.

Counter evidence: volatile reduced contiguous Clang instructions by 2.3–6.5%, but did not produce an
overall timing win. GCC volatile instructions/byte were essentially unchanged, while most cycle samples
increased. The likely hint reduced Clang instructions by 1.6–7.4%; GCC instructions increased about
0.8–1.2%. Counters include startup/validation/warm-ups and were collected separately from paired timing;
they do not establish an isolated instruction-latency explanation. Retained index sizes were unchanged.

Do not promote either experiment based on these runs. The historical GCC volatile benefit did not survive
the metadata/control-flow/helper changes. The likely hint's small Clang gain came with a larger GCC loss.

All four headers passed independent source-oracle checks with Clang/GCC and separate Clang ASan/UBSan,
including fragmented/empty pieces, direct/buffered windows, EOF and maximum NUL-run count.
Oracle logs: `/tmp/md-check-create-{nqfgtzyp,mcra7s6k,zg_tm91b,t52qsvez}/`.
Race results: `/tmp/md-marker-bench/cursor-races/{volatile,likely,both}-{contiguous,large-pieces,gap-like}/`.

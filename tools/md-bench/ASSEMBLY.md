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

## 12. Thin Extra throughput: code placement changes the answer

Compared the previous 4-byte Extra for every nonparagraph against the current 2-byte Extra for
quote/list only, Common's BLANK union member, and one EOF blank count in `MdState`. The current
snapshot includes unconditional Extra assignments/stores and the Boolean `extra_count` increment
in the hot newline flush. No production code was changed during this investigation.

Artifacts: `/tmp/md-marker-bench/thin-extra-throughput/`. Frozen headers are `wide.h`,
`thin-guarded.h`, and `current.h`; each comparison retains source hashes, linked assembly, raw
trials, selected-only memory results and perf counters. CPU 6, Clang/GCC release AVX2, eleven
rotating paired rounds, reused arenas, full native construction including output copies.
Four large contiguous inputs: README, prose, spec and dense. Positive means thin is faster.

| Constructor placement | Clang initial / repeat | GCC initial / repeat |
|---|---:|---:|
| Default compiler placement | -2.8% / -3.1% | -6.1% / -5.4% |
| Both constructors aligned to 64 bytes | +1.2% / +1.4% | +1.5% / +1.7% |
| Both constructors aligned to 4096 bytes | +2.1% / -0.6% | +1.7% / +1.6% |

These are geomeans of per-input median paired thread-CPU throughput ratios. Repeats rerun the
same linked executables, not newly compiled bodies. Default-placement per-input regressions
were Clang 1.8–3.5%, GCC 2.4–10.9%; the small original README was also measured separately
(Clang -4.6%, GCC -10.3%) but is excluded from the four-large-input geomeans above.

The alignment controls change only a temporary function attribute, not either constructor body.
Linked disassembly was compared between default and 64-byte controls: after ignoring padding,
absolute code locations and RIP-relative relocation displacements, all instructions/registers
match for both constructors and both compilers. The timing change is therefore placement-sensitive,
not evidence that a different constructor algorithm suddenly became faster. It does not establish
which exact frontend mechanism dominates. Do not select the best alignment result and advertise
it as an optimization, or extrapolate the default binary's slowdown to the application binary.

The default spec counters show thin executing about 3.0% more instructions with Clang and 2.0%
more with GCC; dense about 3.0% and 1.0%. Both reduce branch counts. The hot newline flush now
builds quote/list ownership with two comparisons, SETcc/OR and an addition instead of the old
single nonparagraph test and conditional increment. This is real extra work, but it does not
explain the full timing result: changing placement reverses the net throughput comparison.
Counters include setup/validation/warmup and were collected separately from paired timing.
Cycle sampling/annotations are in `wide-v-current/`; sample skid prevents per-instruction blame.

Other temporary controls:
- Thin guarded writes versus current unconditional writes: across all five inputs, Clang +3.3%
  geomean, GCC -0.8%. This combines pending-field/output-write changes with the current hot
  Boolean increment; it is not a pure single-store ablation.
- Restoring an explicit conditional count increment did not provide a consistent win.
- Caching ownership when classifying a line looked promising in the default thin/thin binary
  (Clang +2.5%, GCC +7.3% across four large inputs), but lost its advantage in other placements:
  aligned thin/thin Clang -4.8%, GCC +0.8%. Not promoted.

Retained L0 metadata fell 21.2–34.1% on the large inputs. This saving is independent of timing.
Native fingerprint/self-checks passed for every comparison; the default wide/current,
thin-guarded/current and ownership experiments also passed separate Clang ASan/UBSan checks.
The unfinished getter was not benchmarked or claimed to be validated here.

Conclusion: the default comparator binary really is slower, but a general thin-layout throughput
regression is not established. Keep the smaller representation; no count-caching or alignment
change is justified by these measurements alone. Constructor throughput in the final application
binary remains to be measured once that path is runnable independently.

## 13. Controlled thin-metadata ablations and scoped original-binary counters

Artifacts and reproducible scripts: `/tmp/md-marker-bench/controlled-thin/`.
`frozen/` contains the project source and existing benchmark tools used for every new build;
`current-source.h` is the unmodified parser snapshot. Its SHA-256 is
`f959fc4afeb50c30d3ba52154eaa651cf504d319b0d1a7ccc654b4483c5abfb3`.
No production parser change was made or promoted.

### Method

Each initial ablation changes one source factor relative to the completed thin constructor.
Both functions have 4096-byte entry alignment to remove differing entry offsets within a page;
this is an experimental control, not a proposed production optimization. The source scanner,
metadata schema, inputs and allocator implementation are frozen. Eleven rotating paired rounds,
CPU 6, full construction/copies, large contiguous README/prose/spec/dense inputs, both compilers.
Generated code is inspected: a single C change need not translate into a single machine-code change.
Store-guard tests also run with reversed baseline/candidate roles and with 64-byte entry alignment.

Every initial ablation passes native fingerprint/scanner/parser self-checks and separate Clang
ASan/UBSan checks. The eager-prefix variant additionally passes the independent source oracle,
including random/fragmented/empty pieces, long refills/blank counts, with Clang/GCC and ASan/UBSan:
`/tmp/md-check-create-bji26kg2/`.

### Ablation results

Geomean paired CPU-throughput improvement relative to current, four large inputs, fixed 4096-byte
entry alignment. Positive is faster. These are complete-constructor measurements, not isolated
store latency measurements.

| Change from current | Clang | GCC |
|---|---:|---:|
| Guard only Extra output stores; retain count expression | -4.4% | +1.0% |
| Explicit branch for count increment; retain unconditional stores | -0.8% | -0.5% |
| Guard output stores and branch count increment | -6.3% | +0.0% |
| Guard only pending Extra assignment | -0.1% | -0.8% |
| Restore old Extra scratch footprint/alignment; keep thin retained arrays | -0.6% | -2.0% |
| Cache quote/list ownership when classifying the line | -2.3% | +0.3% |
| Pending Common struct instead of union; retain indexed union | +1.1% | -0.0% |

The pending-Common struct and union compile to identical constructor instructions, registers,
internal branch offsets and padding for both compilers (ignoring external address relocations).
Its apparent Clang gain is not an instruction-level optimization. Likewise, GCC's explicit-count
branch compiles identically to its Boolean count expression. Small timing deltas from these null
controls should not be attributed to their source spelling.

Clang store-guard losses survive reversed function roles: current beats guard-only by 4.5% and
guard-both by 6.8%. At 64-byte entry alignment, guard-only loses 6.4% and guard-both loses 5.7%.
GCC guard-only gains 2.4% at 64 bytes, but its roughly 1% gain at 4096 bytes does not survive the
reversed-role comparison consistently. There is no shared Clang/GCC win from restoring guards.

### Why the store guard hurts Clang

In current's L1-emission path, `l1_count` stays in `r14d`, with a register increment. In the
otherwise identical store-guard source variant it lives at `[rsp+0xe0]`, with a load/increment/store;
`run_offset` also reloads from `[rsp+0x120]`. This affects ordinary L1 events, not just quote/list
lines. See `guard-store/clang-{baseline,candidate}.asm`, around `l1_tags[l1_count] = run_tag`.

Scoped counters, medians of three separately launched measurements per variant/group, show the
consequence on spec: dispatched loads +23.1%, stores +8.1%, frontend-empty slots +23.8%, cycles
+5–6%. Dense dispatched loads also rise about 23% and stores 8%. Eliminating logical Extra stores
has increased actual spill/reload traffic. This is compiler register-allocation/code-generation
sensitivity, not proof that an unconditional metadata store is expensive. Architectural physical-
register-file stalls are a different phenomenon from compiler spilling and were tiny here.

### Scoped counters on the original regression, without relinking

The first scoped-ablation harness uses perf control FIFOs around timed batches, excluding setup,
fingerprinting and warmup. To investigate the earlier default-versus-aligned comparisons without
changing their code placement, `clock-gate.c` interposes only their existing `clock_gettime` calls:
thread-CPU-clock entry/exit gates perf counters around the timed constructor loop. The original
linked executables are neither rebuilt nor patched. This preload is used only for counters, not
for throughput claims. Native addresses/instructions remain those of the original experiment.

`original-counters.py` collects three repetitions, rotating selected-variant process order, with
six or fewer counters per group; CSVs report 100% scheduling, without multiplexing. The counter
scope includes the small timer/control boundary and loop/sink overhead, but not source validation
or constructor warmup. `original-scoped-counters.json` and `original-counter-summary.txt` retain
all values. Cycle values from different groups are separate measurements, not additive samples.

The strongest original regression is GCC dense. Default placement, old wider layout -> current:

| Scoped metric | Change |
|---|---:|
| Retired instructions | +1.0% |
| Cycles (memory / frontend groups) | +12.8% / +11.8% |
| Dispatched loads | -0.4% |
| Dispatched stores | -3.0% |
| Store-queue resource-stall cycles | -13.8% |
| Branch misses | +0.2% |
| Frontend-empty dispatch slots | +70.8% |
| Backend-stalled dispatch slots | -2.6% |

GCC spec similarly shows cycles +5.7%, frontend-empty slots +25.7%, branch misses +1.6%, with
store-queue stalls down 30%. Clang default spec/dense retire about 3% more instructions; dense
cycles rise about 5–6%, frontend-empty slots 15.7%, and backend-stalled slots 11.8%. Thus the
larger GCC loss has a particularly strong frontend-supply signature; Clang is less cleanly isolated.

With aligned entries, GCC dense's frontend comparison reverses: current has about 11–13% fewer
frontend-empty slots and 2–3% fewer cycles than old. Importantly, most of this is the *old* function
losing its unusually favorable default frontend behavior; it is not a large alignment speedup for
current. Store-pressure and branch-miss explanations do not fit that reversal.

`icache.py` adds original-binary GCC dense counters. L1 instruction-cache misses are only about
10–20 thousand per measurement and op-cache misses about 1.4–2.6 million, versus billions of
instructions. Default old/current op-cache hit events are 2.283/2.757 billion; aligned old is about
2.804–2.808 billion for essentially the same retired instruction stream. This is consistent with
less efficient op-cache fetch/dispatch packing, rather than a surge of cache misses. It does not
identify a particular Zen 4 bank conflict, fetch-boundary rule or branch-layout mechanism.

### A simpler pending-state alternative

`eager-extra.h` writes quote/list Extra immediately after prefix padding is known, rather than
holding `line_extra` until newline/EOF. It deletes pending Extra state and both flush-site Extra
stores/count tests. Extra records remain in source order and retained facts are unchanged. This
combines lifetime shortening with moving the ownership branch off the common flush path; it is
not a single-factor store ablation.

Four-large-input geomeans, initial / repeat of the same linked binaries:

| Eager prefix Extra versus current | Clang | GCC |
|---|---:|---:|
| 4096-byte entry alignment | +3.5% / +4.2% | +2.7% / +3.5% |
| 64-byte entry alignment | +0.4% / +2.0% | +2.9% / +2.5% |

Clang dense at 64 bytes is unstable: initial -6.1%, repeat +2.0%; do not promise a universal win.
GCC dense is consistently +6–7% in uninstrumented timings. Scoped 4096-byte GCC spec/dense counters
show retired instructions down 3.3%/2.1%, dispatched stores down 6.8%/6.3%, and cycles down roughly
2%/4–6%. This candidate is worth a separate focused decision, but was not applied automatically.

### Conclusion

The large original GCC regression is primarily consistent with frontend delivery/layout, not
extra stores, store-queue saturation or branch misprediction. There is real additional ownership
arithmetic (about 1–3% more retired instructions), but no single data-layout operation explains the
full loss. Guarding writes can make register allocation substantially worse in Clang. Pending
Common's union spelling itself emits no extra instructions relative to a struct in the controlled
ablation. Keep causal claims at that level; precise op-cache/fetch-boundary attribution remains open.

## 14. Actual change-backout matrix (supersedes section 13's causal scope)

This investigation reverts the actual old-to-new changes, rather than substituting alternative
implementations. Artifacts: `/tmp/md-marker-bench/backout/`. `prepare.py` records eight changes,
creates dependent/combined reversions, verifies the original endpoint, and creates a private
comparator able to decode intermediate layouts. The public benchmark and production parser were
not modified. The source hash remains `f959fc4afeb50c30d3ba52154eaa651cf504d319b0d1a7ccc654b4483c5abfb3`.

### Endpoints and controls

- `old-original.h`: the saved pre-regression constructor, Common struct, four-byte Extra for all
  nonparagraphs, blank/EOF counts in Extra, guarded/postincrement ordinary-line emission.
- `new-original.h`: completed thin representation with unconditional ordinary-line Extra stores,
  quote/list-only count advancement, initialized pending Extra, Common BLANK counts and state EOF count.
- `full-rollback.h`: generated by backing every change out of new, not by copying old.
- Assertions verify the generated full rollback matches original constructor tokens, ignoring only
  whitespace/comments and the buffer variable rename. Metadata declarations, MdState, shared
  declarations and md_fill also match. See `endpoint-check.txt` and the preparation assertions.
- Old-versus-old tests produce instruction-equivalent native constructors, including register choices
  and control-flow targets after removing padding/relocations. Thus they measure the placement floor.

The private decoder reads each intermediate schema using its actual Extra stride, owner set and
blank/EOF storage. Adapters remain O(1); decoding and fingerprinting are outside timed construction.
No required writes are simply deleted, and no uninitialized value is deliberately read. All variants
retain the normalized source facts. Initialization removal is permitted only with conditional stores.
Restoring blank/EOF Extra facts requires four-byte Extra; restoring the Common struct requires moving
blank counts back out of Common. These dependencies are explicit rather than hidden in an invalid test.

### Matrix

28 labeled configurations, 24 distinct flag sets, include individual reversions, combinations, an
8-step cumulative rollback and reintroducing individual changes into original code. Both compiler-
default placement and matched 4096-byte entry alignment are measured; alignment is a control only.
Every configuration compares against the same original implementation, not against current.

Actual changes, with private matrix codes:
- S: restore the old guarded store/postincrement flush structure (ordinary owner predicate is separate).
- O: restore the old nonparagraph owner predicate for ordinary lines.
- W: restore the original four-byte Extra union.
- B: restore blank counts and blank Extra commits.
- E: restore EOF Extra storage and original MdState layout.
- U: restore the indexed Common struct, not merely the pending local variable.
- P: remove the new pending Extra zero initializer, once guarded stores make that safe.
- N: restore the original NUL-mapping postincrement expression, an incidental actual diff change.

The complete matrix and all forward-reintroduction cases are in `summary.md`, `summary.json`,
`variants.json`, `aliases.json`, and per-case `results.json`. Three large contiguous inputs—prose,
spec and dense—eleven rotating paired rounds, CPU 6, full creation/copies and reused arenas.
All 48 distinct configuration/placement runs pass native Clang/GCC checks and separate Clang
ASan/UBSan checks, including randomized inputs, empty/fragmented pieces and added 70,000-line
interior/trailing blank cases. Selected cases were repeated using the same linked executables.

### Strongest backout result: flush structure plus pending initialization

GCC paired CPU-throughput changes **relative to the original wider implementation**. Initial / repeat
are three-input geomeans; positive is faster than original, not merely faster than regressed current.

| Constructor/backout | Default placement | Matched 4096-byte entries |
|---|---:|---:|
| Completed new code | -6.3% / -6.8% | +1.9% / +0.9% |
| Restore guarded/postincrement flush only (S) | -6.5% / -6.1% | +1.8% / +1.4% |
| Also remove pending zero initializer (S+P) | -0.2% / -0.4% | +9.1% / +8.5% |
| Restore four-byte Extra and blank Extra facts (W+B) | +0.9% / -0.1% | +8.5% / +8.0% |
| Full rollback / old-versus-old | -0.3% / -0.6% | +0.0% / -0.1% |

Thus S+P is a small valid rollback that recovers the GCC default regression while retaining the
new Common union, two-byte quote/list Extra and state EOF fact. Restoring S alone does not recover it.
W+B is an alternative recovery involving more of the representation. Width alone, ownership alone,
and width+EOF restoration do not recover the regression. The behavior is not a monotonic cost per
additional write; cumulative rollback crosses fast and slow generated-code shapes.

Forward controls reinforce the interaction: original plus the new pending initializer alone measures
GCC -7.3% / -6.7% at default placement, while adding only the Common union or changing ordinary owners
is roughly neutral there. Moving EOF storage alone also yields a roughly 7–8% default loss. However,
these losses do not survive matched-alignment controls as comparable algorithmic costs.

### Counters and assembly distinguish initializer cost from layout

Scoped original-linked-binary counters use the existing clock-gate preload, without rebuilding or
moving the constructors. `counters.py` and `backout-counters.json` retain three repetitions per
selected variant/group; setup, validation and warmup are excluded.

On GCC dense, guarded-flush S versus S+P at matched entry alignment:
- Retired instructions: both approximately **14.798618 billion**; initialization differences are
  negligible at that scale, not a per-line instruction saving.
- Dispatched loads/stores: approximately **2.419/1.176 billion** versus **2.423/1.178 billion**.
  There is no large spill/reload-traffic reduction explaining the faster S+P.
- Frontend-group cycles: **3.501 -> 3.160 billion**.
- Frontend-empty slots: **5.284 -> 3.171 billion**.
- Op-cache hit events: **2.724 -> 2.320 billion**; branch misses are nearly unchanged.

Assembly shows that deleting the initializer removes cold initialization operations and changes
scheduling/register choices. In particular, the main loop's first compare moves from function offset
**0x830 to 0x820**. That observation alone is not sufficient attribution, so a padding-only experiment
follows. It would be wrong to claim that the cost of writing two zero bytes once per parse directly
accounts for a 7–10% throughput difference.

### Padding-only experiment: no C/code-generation changes

`padding/` retains GCC's emitted assembly and reassembles it with 0/16/32/48/64 NOP bytes inserted
immediately before the main loop label. Padding is outside the backedge target: it executes only on
initial fallthrough, not on every iteration. Instructions, operands, register allocation and semantic
branch targets are verified unchanged (branch targets normalized to instruction ordinals, not just
ignored). Native parser/scanner/fact checks also pass for every binary. This avoids treating a new C
annotation as a supposedly layout-only change when the compiler might regenerate the whole function.

The S and S+P forms show a repeatable approximately 64-byte placement pattern. GCC dense, median of
two fixed-order/reversed-order runs, throughput relative to original:

| Padding before loop | S, initializer retained | S+P, initializer removed |
|---|---:|---:|
| 0 bytes | +2.3% | +14.0% |
| 16 bytes | -5.1% | +2.0% |
| 32 bytes | +1.3% | -5.3% |
| 48 bytes | +13.3% | +1.4% |
| 64 bytes | +2.6% | +11.3% |

Adding 48 bytes to S makes the still-initialized constructor approximately as fast as S+P; shifting
S+P by 16/32 bytes loses most/all of its advantage. Prose/spec show the same phase pattern at smaller
magnitudes. This demonstrates that layout, not the runtime zero store, can explain the S -> S+P swing.
It does not identify a particular documented Zen 4 bank or decoded-op packing rule.

Crucial negative control: the same padding sweep was applied to completed current, at both default
and matched entry placements (`padding/current-sweep-results.json`). It did **not** recover current's
full default GCC regression. The favorable phase of one generated loop is not a universal recipe for
another. Therefore do not claim that aligning the loop to 32/64 bytes alone fixes the original change,
or promote the best padding value as production code. The flush/pending-state interaction produces a
different generated shape, and layout amplifies its behavior.

### Clang and limits

Clang does not give the same clean culprit. Completed current defaults measure -3.0% / -3.2%, while
instruction-equivalent old-versus-old defaults measure -1.6% / -3.5%. At matched entries both are near
zero on aggregate. S+P is not a consistent Clang improvement. Restoring only the NUL increment spelling
can move Clang results even though none of the timed inputs has a NUL mapping, further demonstrating
unexecuted-path/code-generation sensitivity rather than a simple runtime operation count.

Conclusion: the faithful backout matrix isolates **guarded flush + no pending initialization** as a
small practical GCC recovery, without discarding the compact metadata representation. The initialization
is a code-generation/layout trigger, not an expensive zero store. There is no demonstrated universal
single-operation culprit across both compilers. No production patch, alignment attribute or padding
change was applied; choose any implementation change separately, with the Clang tradeoff explicit.

The small S+P rollback also passes the independent thin-schema source oracle on random/fragmented/
empty pieces, long blank counts/refills and real spec/dense inputs, with Clang/GCC and ASan/UBSan:
`/tmp/md-check-create-4yggxzv9/`.

## 15. No-initializer guarded assignment versus unconditional representation copy

Requested direct comparison against the pre-regression wider constructor. Artifacts:
`/tmp/md-marker-bench/guard-v-memcpy/`. The frozen/private comparator and three-input workload are
unchanged from section 14. Both candidates remove `line_extra = {0}`:

- `guarded.h`: quote/list guard around assignment and postincrement at both flush sites (S+P).
- `memcpy.h`: unconditional `memcpy(&l0_extra[extra_count], &line_extra, sizeof line_extra)` at both
  flush sites; retain current hot Boolean count addition and final conditional count advancement.

This compares those complete implementations, not a pure store-instruction ablation. Both retain the
Common union, two-byte quote/list Extra and state EOF count. Source/parser production code is unchanged.

Three large inputs (prose/spec/dense), eleven rotating paired rounds, CPU 6, full native creation and
output copies, Clang/GCC release AVX2. Each candidate is paired directly with original, and the same
linked binaries are repeated. Old-versus-old controls measure the placement floor. Positive means
faster than the **original pre-regression constructor**, not faster than regressed current.

| Variant | Default Clang, initial / repeat | Default GCC, initial / repeat |
|---|---:|---:|
| Guarded assignment, no initialization | -2.9% / -3.1% | +0.3% / +0.3% |
| Unconditional memcpy, no initialization | -3.6% / -3.4% | -4.6% / -4.6% |
| Old-versus-old control | -3.7% / -3.8% | -0.7% / -0.5% |

| Variant | Matched 4096-byte entries, Clang | Matched 4096-byte entries, GCC |
|---|---:|---:|
| Guarded assignment, no initialization | +1.1% / +0.0% | +7.8% / +8.4% |
| Unconditional memcpy, no initialization | +1.4% / +1.1% | +3.7% / +3.5% |
| Old-versus-old control | +0.4% / -0.6% | -0.0% / -0.4% |

The alignment lane remains a placement control, not a recommendation to align production functions.
GCC consistently favors guarded assignment in both lanes: default placement recovers the original
throughput, while memcpy retains about a 4.6% loss. Clang does not distinguish them reliably beyond
placement effects; both default results are comparable to its instruction-equivalent old/old control.

Linked assembly confirms both compilers inline the two-byte memcpy: stack load plus 16-bit store,
not a memcpy library call. Thus the GCC loss cannot be blamed on a tiny-copy function-call overhead.
Clang's shown source local is at `[rsp+0x26]`, GCC's at `[rsp+0x72]` in default memcpy builds.

Both candidates pass the independent source oracle for Common/sparse Extra, source partitions,
EOF, raw L1/NUL mappings, fragmented/empty pieces and long blank runs/refills, including real
spec/dense inputs, in Clang/GCC and separate ASan/UBSan builds:
`/tmp/md-check-create-cg_sljia/` (guarded), `/tmp/md-check-create-tgoxaz5_/` (memcpy).

Additionally, Clang MemorySanitizer with origin tracking, `-O1 -fPIE -pie`, passes the full native
benchmark self-checks for both guarded and memcpy candidates. The memcpy MSan binary also passes
real spec input validation/timed creation. Uncommitted indeterminate bytes propagate through the
representation copies without being consumed by the retained-fact reader; committed records do not
trigger uninitialized-value reports. This supplies the missing uninitialized-read check that ASan/
UBSan cannot provide. Logs: `msan-*`, `guarded-msan-*`, `msan-real-spec.txt` in the artifact directory.

No implementation was promoted automatically. Among these two options, guarded assignment is the
supported GCC performance choice; unconditional representation copying is correct under the tested
commit invariant but does not recover all of the default-placement regression.

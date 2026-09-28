# Parser performance checkpoint

## Decision

**Pause grammar polishing and refactor the incremental engine and quadratic hot paths.** The current implementation does not meet the intended performance goal across ordinary document shapes. This does not imply throwing away the grammar recognizers, piece reader, interface, or annotation model.

Neither `parser_neo.h`, `parser.h`, nor `md_interface.h` was modified during this benchmark investigation.

## Measured results

These are single-byte edit latencies: Neo incrementally updates its retained syntax; the owned fork fully reparses the post-edit source with its parser arena reused. The conservative table below gives the fork a no-op event callback (no index construction or rendering).

| Document / edit | Neo update, median | Fork full reparse, median | Outcome |
|---|---:|---:|---|
| Actual `md_design.md`, 37,463 bytes | 20.90 us | 56.76 us | Neo 2.72x faster |
| Separate prose blocks, 16 KiB | 21.58 us | 84.54 us | Neo 3.92x faster |
| Separate prose blocks, 128 KiB | 167.75 us | 676.68 us | Neo 4.03x faster |
| Separate prose blocks, 1 MiB | 1.526 ms | 5.397 ms | Neo 3.54x faster |
| One list, 128 KiB | 5.852 ms | 0.800 ms | Neo 7.32x slower |
| One formatted paragraph, 128 KiB | 191.604 ms | 0.680 ms | Neo 282x slower |
| One fenced code block, 1 MiB | 1.224 ms | 0.785 ms | Neo 1.56x slower |
| Shared reference destination change, 1 MiB | 26.249 ms | 6.177 ms | Neo 4.25x slower |

The shared-reference edit intentionally affects links throughout the document, so localized constant-time work is not expected there. It measures a necessary broad-invalidation path.

The primary run used a small checksum event callback instead of a no-op. In that run, separate prose blocks achieved about 4.7–4.9x faster updates. Both sets of measurements are retained; the no-op comparison is the more conservative claim about parsing speed.

### Scaling and failures

- Prose update latency grows approximately with total document size, despite replacing only one byte. Retaining syntax is helping, but updates are not sufficiently localized.
- A 1 MiB list **fails during Neo creation** with `MD_ERROR_OUT_OF_MEMORY`; scratch reaches 1,073,425,288 bytes against the project's 1 GiB limit. The fork reparses that input in approximately 8.03 ms with the checksum callback.
- The complete 1 MiB formatted-paragraph benchmark exceeded its 80-second timeout. This is a timeout for the entire benchmark process, **not** a measured 80-second individual parse. The fork-only benchmark reparses that input in approximately 7.08 ms.
- For the successful checksum runs, formatted-paragraph updates rose from 4.07 ms at 16 KiB to 219.38 ms at 128 KiB: about 54x more time for 8x more source.
- Front, middle, end, and insertion/deletion edits in 1 MiB prose all remained near 1.35–1.40 ms in the checksum runs. Edit position does not remove the whole-document overhead.
- Repartitioning that prose into 256 or 4,096 pieces produced approximately 1.39–1.41 ms updates. Piece lookup is not the principal bottleneck in this workload.

### Creation, queries, and memory

- Fresh syntax construction for 1 MiB prose: Neo about 37.05 ms versus the no-op fork's 7.39 ms. Fresh construction is not a strength of the current implementation either.
- A 2 KiB viewport query in formatted prose costs approximately 33–34 us, additional to the update. The actual design document's viewport query costs about 2.2 us. These are annotation queries, not rendering measurements.
- After edits, the 1 MiB prose case occupies about 50.6 MB in Neo's durable arena, including reusable retired allocations. Creation consumes 188.5 MB of scratch; each local update consumes about 14.2 MB of scratch. The fork's peak parse arena usage is about 7.64 MB. Persistent storage and temporary parse storage serve different purposes, so these are not identical memory categories.
- The 128 KiB list consumes about 64.3 MB of scratch per edit.

## What profiling found

Linux `perf` sampled user CPU cycles with frame-pointer call graphs. Profiles are of the benchmark process, including setup, queries, and final consistency validation—not mathematically isolated update-only samples. Long update loops make the prose/list profiles predominantly update work. Percentages are diagnostic estimates, separate from the unprofiled latency measurements above.

### Large paragraphs: repeated linear searches create quadratic work

In the 128 KiB paragraph profile:

- Approximately **88.5%** of sampled cycles are attributed through inlining to `md_inside_link`, called by `md_inline_autolinks`.
- Approximately **4.8%** are in `md_inline_nests` through delimiter resolution.
- Approximately **2.1%** are in `md_inline_link_node`.

`md_inside_link` scans the leaf's annotations for each unclaimed byte, even before determining whether that byte begins a plausible permissive autolink. With source length N and annotation count A, that path is O(N*A). Nesting checks and removal of tentative attribute annotations introduce additional repeated scans. Removing only the largest hotspot would still leave other quadratic paths; a membership index or ordered sweep and bounded delimiter/link processing are needed.

### Lists: whole-container reparsing plus quadratic scratch cloning

The incremental restart boundary is a root child. A document-sized list is one root child, so a local edit reparses all its items and inline contents.

Additionally, `md_parse_one` clones `count - used` remaining lines for each item, rather than just the item's lines. Summed across a list, this is quadratic copying/allocation. It explains the scratch exhaustion and cannot be solved by merely increasing arena size.

The list profile shows parsing and annotation work throughout the entire list, with substantial allocation/copy/zeroing costs; it is not dominated by piece traversal.

### Ordinary prose: whole-state bookkeeping dominates local edits

In the long 1 MiB prose profile, prominent self costs include:

- `md_visit_footnotes`: about 18% — even though this fixture contains no footnotes, the enabled extension triggers traversal.
- `md_finish_content`: about 11%.
- `md_reparse_root`: about 9%.
- `md_prepare_commit`: about 8%.
- `md_commit` and `md_finish_leaves`: about 5% each.
- Additional significant memory copying/zeroing.

The code clones top-level metadata arrays, reconstructs root line/child arrays, and scans the document for dependencies, content metadata, footnotes, and commit bookkeeping. The parser avoids reparsing most prose, but still processes whole-document metadata on every edit.

## Recommended architectural work

1. Replace whole-state cloning/scanning with staged changes to dirty blocks and their ancestor paths. Preserve failure atomicity without copying every slot.
2. Add restart/convergence within containers, so editing one list item does not reparse its entire list. Large code blocks also need finer-grained retained line work.
3. Remove the quadratic inline searches and list-tail cloning before additional grammar polishing.
4. Make dependency and footnote maintenance operate on changed definitions/uses, with a genuinely cheap path when none exist.
5. Re-run these same fixtures as an early performance gate. Local prose/list edits should stop scaling proportionally with unrelated document size; large-paragraph parsing should at least scale linearly rather than quadratically. Broad invalidation should not be several times slower than a complete fork parse without a concrete, measured justification.

This is an incremental-core refactor and algorithmic repair, not a recommendation to discard every parser component. The current output contract can remain unchanged.

## Method and limitations

- CPU: AMD Ryzen 9 7950X3D; process pinned to logical CPU 2. Normal host scheduling and frequency variation remain possible.
- Compiler: Clang 22.1.8, GNU C17, `-O3 -g -fno-omit-frame-pointer`, no sanitizers for timing, no LTO.
- Flags: `MD_DEFAULT_FLAGS | MD_DIALECT_GITHUB` for both parsers.
- Inputs: six synthetic shapes at approximately 16 KiB, 128 KiB and 1 MiB, plus the actual local `md_design.md`. Synthetic prose deliberately contains emphasis, links, code spans, and entities; this is not a representative corpus claim.
- Edits: repeated same-position one-byte replacements; additional front/end edits, alternating single-byte insertion/deletion, a shared-reference destination edit, and closing-fence toggling.
- Five warmup edits excluded. Usually 31 or 51 measured edits; expensive secondary cases use 11. Actual design-document runs use 101. CSVs include median and p95; these are descriptive samples, not confidence intervals.
- Source mutation, piece partitioning, scratch/output resets, consistency checks, and result consumption are outside operation timings. Timers use `CLOCK_MONOTONIC_RAW`.
- Neo uses the real project arena allocators with normal backing sizes and huge pages disabled. Backing pages are reused. Three fresh-state construction samples are taken; their median is reported, not an OS-cold page-fault benchmark.
- The fork reuses `MdParser` and its arena during edit comparisons. It does not retain a queryable annotation index. Any event-to-index conversion or rendering cost is excluded, so the compared operations do not promise identical retained output capabilities.
- Every successful Neo benchmark compares its final full result byte-for-byte with a freshly constructed Neo state, outside timing. This checks incremental consistency, not equivalence to MD4C grammar. The previously failing general storage-reuse test remains unresolved; this profiling task made no correctness fixes.
- A separate ASan/UBSan benchmark build completed all seven successful workload shapes at 64 KiB (three measured edits each) without diagnostics. Those times are not included.

## Files and reproduction

- `parser_neo_bench.c`: document generation, edit loop, timings, memory counters, and consistency checks.
- `parser_neo_bench_fork.c`: separate compilation of the unchanged owned fork, with checksum/no-op sinks.
- `parser_neo_bench_results.csv`: checksum callback runs.
- `parser_neo_bench_noop_results.csv`: conservative no-op callback runs.
- `parser_neo_profile_{prose,list,paragraph,fork}.txt`: saved sampling reports.

Build from this directory:

```sh
clang -std=gnu17 -D_GNU_SOURCE -I/home/max/dev/c/rwmd/src \
  -O3 -g -fno-omit-frame-pointer \
  -Wno-macro-redefined -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses \
  parser_neo_bench.c parser_neo_bench_fork.c -lutf8proc -o .parser_neo_bench
```

Examples:

```sh
# workload KiB pieces edit rounds [both|neo|fork]
taskset -c 2 ./.parser_neo_bench prose 1024 1 middle 51
MD_BENCH_FORK_NOOP=1 taskset -c 2 ./.parser_neo_bench paragraph 128 1 middle 11
taskset -c 2 ./.parser_neo_bench prose 1024 4096 insert 51
taskset -c 2 ./.parser_neo_bench shared 1024 1 definition 11
taskset -c 2 ./.parser_neo_bench design 64 1 middle 101

# Expected resource failure:
timeout 80 taskset -c 2 ./.parser_neo_bench list 1024 1 middle 31

# Sampling; -N avoids updating the external build-ID cache.
perf record -N -F 999 -e cycles:u -g --call-graph fp -o .neo-prose.perf -- \
  taskset -c 2 ./.parser_neo_bench prose 1024 1 middle 1501 neo
perf report --stdio --no-children --percent-limit 1 -i .neo-prose.perf
```

Recorded source SHA-256 values:

```text
parser_neo.h   3bfd9463c118defe0ec88540efe19c9d1eba1f041a8b65520aa71d7569610718
parser.h       63f072406e9204dc5bd6d2dde0ac99a0568313b086553af9566c444b7138a574
md_interface.h e41e352993b0997b9f97bfbabec693dc76523b52eef75488b06eb9ecd0fb4573
md_design.md   b0953ad05b6bc9f0cb4f05ee169406d72b99e93a75455a3c0a7ba6d6c2e1336e
```

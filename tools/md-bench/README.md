# md_create comparator

One C harness and one Python runner; no generated per-experiment scripts or dependencies outside Python's standard library.
Requires the project's Linux/x86 AVX2 toolchain/libraries, Clang, GCC and objdump. `perf` is optional.

## Constructor-only migration check

```sh
python3 tools/md-bench/check-create.py readme.md
python3 tools/md-bench/check-create.py --candidate /tmp/parser-candidate.h readme.md
```

This extracts the actual header through `md_create`, excluding the not-yet-migrated `md_get`.
Clang/GCC release builds and Clang ASan/UBSan validate Common/sparse Extra against a source oracle,
including paragraph/marker content starts, tab columns, blanks/EOF, raw L1, NUL mappings, random bytes,
long refills and fragmented/empty pieces. Direct helper checks cover direct reads, capacity-sized
batching, NULL empty pieces and exhausted cursors. Optional file arguments add real inputs (up to 16 MiB each).
Sources and logs are kept in a fresh `/tmp/md-check-create-*` directory.

Common is a 4-byte tagged union: BLANK stores its physical line count; other tags store pre-indent and
content-start. Extra is a packed 2-byte post-indent only for quote/list candidates. EOF's pending blank
count lives once in `MdState.trailing_blank_count`, alongside its Common indentation.

`bench.c` and `compare.py` support legacy metadata, the previous split layout, and this thin Extra layout.
They exclude `md_get`, so the constructor can be benchmarked independently during its migration.

## First-loop cursor check

```sh
python3 tools/md-bench/check-context.py
```

This compiles the full parser and exercises the actual first-loop source with a scalar cursor/Extra oracle.
It covers quote/list-only Extra ownership, skipped headings/blanks, sparse/dense/no-hit windows, mask/refill
boundaries, guarded tails, rewind and EOF in Clang/GCC release and Clang ASan/UBSan. It also checks the
second loop's refill block directly, without invoking its unfinished output body or claiming to test semantic state updates. `check-get.py` remains the full output oracle
for when those loop bodies are implemented.

## Use

Save a baseline **before** editing:

```sh
cp src/lib/md/md_parser_scratch.h /tmp/md-before.h
# Edit md_create, then:
python3 tools/md-bench/compare.py /tmp/md-before.h readme.md \
  --cpu 6 --out /tmp/md-comparison --sanitize --perf
```

- Inputs are positional paths; each must be nonempty and no larger than 16 MiB (current arena reservation limit).
- `--candidate FILE` tests a temporary candidate header instead of the working tree.
- `--piece-bytes 64` tests fragmented DocViews; default is one contiguous piece.
- `--profile` additionally records user-mode cycles and writes perf reports/annotations for each function/input.
- The output directory must be empty. Nothing in the production parser is changed by the runner.
- Without `--out`, results go into a fresh `/tmp/md-bench-*` directory.

For the shared-body-exit experiment from this session:

```sh
python3 tools/md-bench/compare.py \
  /tmp/md-marker-bench/shared-body-exit/parser.h \
  /tmp/md-marker-bench/v2-inputs/readme-16m.md \
  /tmp/md-marker-bench/v2-inputs/dense-16m.md \
  --cpu 6 --out /tmp/shared-exit-comparison --sanitize --profile
```

## What it measures

Both bodies compile into **one binary per compiler**, in fixed baseline/candidate order. Each retains its
native MdState return layout; metadata declarations may use the legacy, split or thin Extra layout. Small generated
O(1) adapters copy returned pointers/counts into the harness view; this constant overhead is timed, but no
per-entry conversion or fingerprinting occurs inside the timed loop. The large constructor bodies remain
separate out-of-line functions in that binary, without LTO.

Allocator/scanner and other declarations remain shared and checked. Changes to `md_create` and the existing
`md_fill` helper are supported; a changed helper is cloned under a candidate-only name so attributes such as
`noinline` cannot affect the baseline. The scanner is frozen from the current tree for both variants. Other
project headers come from the current checkout: retain that checkout when replaying a run.

Builds use `-O3 -g -march=x86-64-v3 -DWAYLAND`, without `__DEBUG`, sanitizers or LTO.
Clang sanitizer checks, when requested, use a separate binary.

Before timing, both variants must match normalized L0/L1/mapping fingerprints. Legacy metadata is decoded into
retained facts: pre-indent, content start, quote/list post-indent and blank/EOF line count. Deliberately
removed trailing flags and post-indent for other marker candidates are not compared. Marker bytes and
lengths are checked through the complete raw L1 stream. Thin Extra ownership and blank/EOF placement are
also independently checked by the source oracle. Self-checks include expected marker tags, empty input, random bytes, source NUL, empty/fragmented
pieces, exact scanner capacities, lane/tail positions and long refills. Use the constructor-only source oracle
above for independent validation of the new representation.

Timing uses eleven rotating-order rounds, 64 MiB per timed batch, warm reused arenas, and full creation including
result allocations/copies. Wall and thread-CPU times are both retained; paired speedup uses thread CPU time.
No automatic retry or selection of the fastest run. A low wall/CPU gap rules out substantial descheduling,
**not** interference from SMT, shared cache, bandwidth or frequency changes.

Every input also gets fresh, selected-variant-only memory processes (three creations with reused arenas).
They do not invoke the other constructor. Reports distinguish exact retained index bytes (arena length),
L0 metadata bytes, worst-case scratch allocated span (allocator length), and `/proc/self/status` VmHWM peak RSS. RSS includes
input/document pieces, runtime and both arena working sets. Scratch allocator length is **not resident RAM**;
the arenas still map 1 GiB each and request huge pages, so resident savings need not track byte savings closely.
The scratch span measures suballocations inside that unchanged mapping, not total virtual address space.
Memory-mode setup/validation and file I/O are not included in throughput timings.

See [ASSEMBLY.md](ASSEMBLY.md) for earlier assembly investigations and the split-metadata throughput/counter/memory results.

## Outputs

- `summary.md`: throughput and counter comparison tables, including instruction/cycle deltas.
- `results.json`: raw trials, wall/CPU throughput, paired CPU ratios, memory measurements and optional counter totals.
- `manifest.json`: commands, compiler versions, CPU, input and frozen source hashes.
- `baseline-source.h`, `candidate-source.h`, generated headers/adapters, scanner and C harness: measured source snapshots.
- `*-memory.txt`: selected-only process memory measurements.
- `clang-{baseline,candidate}.asm`, `gcc-{baseline,candidate}.asm`: actual linked code with source lines.
- `*-counters.csv`: user-mode cycles, instructions, branches and branch misses.
- `*.perf`, `*-report.txt`, `*-annotate.txt`: optional cycle profiles.
- Build, self-check and timing logs.

Counter runs select one variant, with two setup creations plus eleven rounds of one warm-up and N timed creations:
`processed_bytes = input_size * (2 + 11 * (N + 1))`. Whole-process counts also include startup, validation and logging;
they are not isolated per-function measurements. Inspect perf CSV scheduling percentages for multiplexing.

The C harness can also run directly:

```sh
/tmp/md-comparison/clang                         # self-checks
/tmp/md-comparison/clang readme.md 0             # both variants
/tmp/md-comparison/clang readme.md 64 candidate 8 # selected variant, 8 timed calls/round
/tmp/md-comparison/clang readme.md 0 candidate memory # candidate-only memory measurement
```

#!/usr/bin/env python3
"""Compare md_create throughput, counters and memory across legacy/split/thin metadata."""
import argparse
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shutil
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ROUNDS = 11  # Matches bench.c.
COMMANDS = []


def run(command, output):
    command = list(map(str, command))
    COMMANDS.append(command)
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stdout}")
    return result.stdout


def function(source, declaration="MdState md_create("):
    start = source.index(declaration)
    start = source.rfind("\n", 0, start) + 1
    # Project style: only the function's closing brace starts in column zero.
    end = source.index("\n}", start) + 2
    return start, end


def declarations(source):
    start, end = function(source)
    fill_start, fill_end = function(source, "internal u32 md_fill(")
    if "typedef union MdL0Common {" in source[:start]:
        metadata_start = source.index("typedef union MdL0Common {")
        schema = "thin"
    elif "typedef struct MdL0Common {" in source[:start]:
        metadata_start = source.index("typedef struct MdL0Common {")
        schema = "split"
    else:
        metadata_start = source.index("typedef struct MdL0Metadata {")
        schema = "legacy"
    metadata_end = source.index("typedef struct MdL1Metadata", metadata_start)
    state_start = source.index("typedef struct MdState {")
    state_end = source.index("} MdState;", state_start) + len("} MdState;")
    shared = source[:metadata_start] + source[metadata_end:state_start] + source[state_end:fill_start] + source[fill_end:start]
    shared = "".join(re.sub(r"/\*.*?\*/|//[^\n]*", "", shared, flags=re.S).split())
    return dict(start=start, end=end, fill=source[fill_start:fill_end], shared=shared, schema=schema,
                metadata=source[metadata_start:metadata_end], state=source[state_start:state_end])


def adapter(variant, schema):
    fields = ["l0_tags", "l0_offsets", "l0_count", "l1_tags", "l1_offsets", "l1_lengths", "l1_count",
              "mappings", "mappings_count"]
    if schema in {"split", "thin"}:
        fields += ["l0_common", "l0_extra"]
    else:
        fields += ["l0_metadata"]
    if schema == "thin":
        fields += ["trailing_blank_count"]
    initializers = ", ".join("." + field + " = s." + field for field in fields)
    if schema == "thin":
        initializers += ", .thin_metadata = 1"
    return (f"static BenchState create_{variant}(const DocView* source, Arena* arena, ScratchArena* scratch) {{\n"
            f"    MdState{variant.title()} s = md_create_{variant}(source, arena, scratch);\n"
            f"    return (BenchState){{{initializers}}};\n}}\n")


def parse_trials(text):
    stats, trials = {}, {}
    for line in text.splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "stats":
            stats[fields[1]] = dict(zip(
                ["size", "l0", "l1", "maps", "scratch_bytes", "output_bytes", "fingerprint"],
                map(int, fields[2:])))
        elif fields[0] == "trial":
            trials.setdefault(fields[1], []).append(dict(
                round=int(fields[2]), calls=int(fields[3]), wall=float(fields[4]), cpu=float(fields[5])))
    assert set(trials) == {"baseline", "candidate"}
    assert all(len(t) == ROUNDS for t in trials.values())
    assert stats["baseline"]["fingerprint"] == stats["candidate"]["fingerprint"]
    return stats, trials


def counter_values(path):
    values = {}
    for line in path.read_text().splitlines():
        fields = line.split(",")
        if len(fields) > 2 and fields[0].strip().isdigit():
            values[fields[2]] = int(fields[0])
    assert {"cycles:u", "instructions:u", "branches:u", "branch-misses:u"} <= values.keys(), path
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="Full baseline md_parser_scratch.h")
    parser.add_argument("inputs", type=Path, nargs="+", help="Nonempty Markdown files, at most 16 MiB each")
    parser.add_argument("--candidate", type=Path, default=ROOT / "src/lib/md/md_parser_scratch.h")
    parser.add_argument("--out", type=Path, default=Path("/tmp") / f"md-bench-{time.time_ns()}")
    parser.add_argument("--cpu", type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument("--piece-bytes", type=int, default=0, help="0: contiguous input; e.g. 64: fragmented")
    parser.add_argument("--perf", action="store_true", help="Collect user-mode hardware counters")
    parser.add_argument("--profile", action="store_true", help="Also record cycles and annotate both functions")
    parser.add_argument("--sanitize", action="store_true", help="Run Clang ASan/UBSan self-checks")
    args = parser.parse_args()
    if args.cpu not in os.sched_getaffinity(0) or args.piece_bytes < 0:
        parser.error("CPU must be available and piece size nonnegative")
    for path in args.inputs:
        if not 0 < path.stat().st_size <= 16 * 1024 * 1024:
            parser.error(f"Input must be nonempty and at most 16 MiB: {path}")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        parser.error(f"Output directory must be empty: {out}")

    baseline = args.baseline.read_text()
    candidate = args.candidate.read_text()
    old, new = declarations(baseline), declarations(candidate)
    if old["shared"] != new["shared"]:
        parser.error("Shared declarations differ outside metadata/MdState/md_fill")
    schemas = {old["schema"], new["schema"]}
    if old["schema"] == new["schema"] and old["metadata"] != new["metadata"]:
        parser.error("Metadata declarations differ within the same schema")
    (out / "baseline-source.h").write_text(baseline)
    (out / "candidate-source.h").write_text(candidate)
    flags = (f'#define BENCH_LEGACY {int("legacy" in schemas)}\n'
             f'#define BENCH_SPLIT {int("split" in schemas)}\n'
             f'#define BENCH_THIN {int("thin" in schemas)}\n')
    baseline_body = baseline[:old["end"]].replace("MdState", "MdStateBaseline")
    if old["schema"] == "thin":
        baseline_body = baseline_body.replace("MdL0Common", "MdL0CommonThin").replace("MdL0Extra", "MdL0ExtraThin")
    baseline_body = baseline_body.replace("md_create(", "md_create_baseline(", 1)
    (out / "baseline.h").write_text(flags + baseline_body)
    candidate_decls = ""
    if old["schema"] != new["schema"]:
        candidate_decls += new["metadata"]
    candidate_decls += new["state"].replace("MdState", "MdStateCandidate") + "\n"
    candidate_body = candidate[new["start"]:new["end"]].replace("MdState", "MdStateCandidate")
    if new["schema"] == "thin":
        candidate_decls = candidate_decls.replace("MdL0Common", "MdL0CommonThin").replace("MdL0Extra", "MdL0ExtraThin")
        candidate_body = candidate_body.replace("MdL0Common", "MdL0CommonThin").replace("MdL0Extra", "MdL0ExtraThin")
    candidate_body = candidate_body.replace("md_create(", "md_create_candidate(", 1)
    if old["fill"] != new["fill"]:
        candidate_decls += new["fill"].replace("md_fill(", "md_fill_candidate(") + "\n"
        candidate_body = candidate_body.replace("md_fill(", "md_fill_candidate(")
    (out / "candidate.h").write_text(candidate_decls + candidate_body)
    (out / "adapters.h").write_text(adapter("baseline", old["schema"]) + adapter("candidate", new["schema"]))
    shutil.copyfile(HERE / "bench.c", out / "bench.c")
    includes = out / "include"
    (includes / "lib/grim").mkdir(parents=True)
    shutil.copyfile(ROOT / "src/lib/grim/intrinsics.h", includes / "lib/grim/intrinsics.h")
    # Project assert takes two arguments; repeated libc includes must not replace it.
    (includes / "assert.h").write_text('''#if defined(STATIC_ASSERT)
#pragma push_macro("assert")
#include_next <assert.h>
#pragma pop_macro("assert")
#else
#include_next <assert.h>
#endif
''')
    common = ["-g", "-march=x86-64-v3", "-I" + str(includes), "-I" + str(ROOT / "src"),
              "-DWAYLAND", str(out / "bench.c"), "-lwayland-client", "-lvulkan", "-lm"]
    results, counters, versions = [], [], {}
    if args.sanitize:
        binary = out / "check-asan"
        run(["clang", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *common,
             "-o", binary], out / "sanitize-build.txt")
        run([binary], out / "sanitize-check.txt")

    for compiler in ["clang", "gcc"]:
        versions[compiler] = run([compiler, "--version"], out / f"{compiler}-version.txt")
        binary = out / compiler
        run([compiler, "-O3", *common, "-o", binary], out / f"{compiler}-build.txt")
        run([binary], out / f"{compiler}-check.txt")
        for variant in ["baseline", "candidate"]:
            run(["objdump", "-dlS", "-Mintel", "--no-show-raw-insn",
                 "--disassemble=md_create_" + variant, binary], out / f"{compiler}-{variant}.asm")
        for index, path in enumerate(args.inputs):
            case = f"{compiler}-{index}-{path.stem}"
            invocation = ["taskset", "-c", args.cpu, binary, path.resolve(), args.piece_bytes]
            text = run(invocation, out / f"{case}-trials.txt")
            stats, trials = parse_trials(text)
            row = dict(compiler=compiler, input=str(path.resolve()), piece_bytes=args.piece_bytes,
                       stats=stats, trials=trials, wall_gbps={}, cpu_gbps={})
            for variant in ["baseline", "candidate"]:
                samples = trials[variant]
                row["wall_gbps"][variant] = statistics.median(
                    stats[variant]["size"] * t["calls"] / t["wall"] / 1e9 for t in samples)
                row["cpu_gbps"][variant] = statistics.median(
                    stats[variant]["size"] * t["calls"] / t["cpu"] / 1e9 for t in samples)
            row["paired_cpu_speedup"] = statistics.median(
                old["cpu"] / new["cpu"] for old, new in zip(trials["baseline"], trials["candidate"]))
            row["wall_cpu_ratio"] = max(row["cpu_gbps"][v] / row["wall_gbps"][v] for v in stats)
            row["memory"] = {}
            for variant in ["baseline", "candidate"]:
                text_memory = run([*invocation, variant, "memory"], out / f"{case}-{variant}-memory.txt")
                fields = text_memory.strip().split()
                assert fields[:2] == ["memory", variant], text_memory
                memory = dict(zip(["scratch_bytes", "output_bytes", "metadata_bytes", "peak_rss_kib",
                                   "initial_rss_kib", "fingerprint"], map(int, fields[2:])))
                assert memory["fingerprint"] == stats[variant]["fingerprint"]
                row["memory"][variant] = memory
            results.append(row)
            print(f'{compiler:5} {path.name}: CPU delta {100*(row["paired_cpu_speedup"]-1):+.1f}%; '
                  f'wall/CPU {row["wall_cpu_ratio"]:.3f}', flush=True)
            if args.perf or args.profile:
                values = {}
                calls = max(4, (16 * 1024 * 1024) // path.stat().st_size)
                for variant in ["baseline", "candidate"]:
                    prefix = f"{case}-{variant}"
                    csv = out / f"{prefix}-counters.csv"
                    run(["perf", "stat", "-x,", "-e", "cycles:u,instructions:u,branches:u,branch-misses:u",
                         "-o", csv, "--", *invocation, variant, calls], out / f"{prefix}-stat.txt")
                    values[variant] = counter_values(csv)
                    if args.profile:
                        data = out / f"{prefix}.perf"
                        run(["perf", "record", "-q", "-F", "999", "-e", "cycles:u", "-o", data,
                             "--", *invocation, variant, max(calls, 16)], out / f"{prefix}-record.txt")
                        run(["perf", "report", "--stdio", "--no-children", "-g", "none", "-i", data],
                            out / f"{prefix}-report.txt")
                        run(["perf", "annotate", "--stdio", "--symbol=md_create_" + variant, "-i", data],
                            out / f"{prefix}-annotate.txt")
                counters.append(dict(compiler=compiler, input=str(path.resolve()), counters=values,
                                     processed_bytes=path.stat().st_size * (2 + ROUNDS * (calls + 1))))

    manifest = dict(cpu=args.cpu, compilers=versions, commands=COMMANDS,
                    schemas=dict(baseline=old["schema"], candidate=new["schema"]),
                    runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    sources={name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in
                             ["baseline-source.h", "candidate-source.h", "baseline.h", "candidate.h", "adapters.h",
                              "bench.c", "include/lib/grim/intrinsics.h"]},
                    inputs={str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.inputs})
    (out / "results.json").write_text(json.dumps(dict(results=results, counters=counters), indent=2) + "\n")
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    report = ["# md_create comparison", "", f"CPU {args.cpu}; {ROUNDS} rotating-order rounds; release AVX2.",
              "Thread CPU excludes descheduling, not shared-resource/frequency interference.", "",
              "| Compiler / input | Wall GB/s before → after | Paired CPU delta | Wall/CPU |",
              "|---|---:|---:|---:|"]
    for row in results:
        a, b = row["wall_gbps"]["baseline"], row["wall_gbps"]["candidate"]
        report.append(f'| {row["compiler"]} / {Path(row["input"]).name} | {a:.3f} → {b:.3f} | '
                      f'{100*(row["paired_cpu_speedup"]-1):+.1f}% | {row["wall_cpu_ratio"]:.3f} |')
    for compiler in versions:
        ratios = [r["paired_cpu_speedup"] for r in results if r["compiler"] == compiler]
        ratio = math.exp(statistics.mean(map(math.log, ratios)))
        summary = f"{compiler}: geomean paired CPU delta {100*(ratio-1):+.1f}%"
        print(summary)
        report.extend(["", summary])
    report.extend(["", "Memory: exact retained index/metadata bytes; worst-case scratch allocated span; fresh selected-only process peak RSS.",
                   "MiB = 1048576 bytes. RSS includes input, document pieces, runtime and both arena working sets.", "",
                   "| Compiler / input | Index MiB before → after | L0 metadata MiB before → after | Scratch span MiB before → after | Peak RSS MiB before → after |",
                   "|---|---:|---:|---:|---:|"])
    for row in results:
        a, b = row["memory"]["baseline"], row["memory"]["candidate"]
        report.append(f'| {row["compiler"]} / {Path(row["input"]).name} | '
                      f'{a["output_bytes"]/1048576:.3f} → {b["output_bytes"]/1048576:.3f} | '
                      f'{a["metadata_bytes"]/1048576:.3f} → {b["metadata_bytes"]/1048576:.3f} | '
                      f'{a["scratch_bytes"]/1048576:.2f} → {b["scratch_bytes"]/1048576:.2f} | '
                      f'{a["peak_rss_kib"]/1024:.2f} → {b["peak_rss_kib"]/1024:.2f} |')
    if counters:
        report.extend(["", "Whole-process counters include startup, validation and warm-up.", "",
                       "| Compiler / input | Instructions/byte before → after | Instruction delta | Cycle delta |",
                       "|---|---:|---:|---:|"])
        for row in counters:
            a, b = row["counters"]["baseline"], row["counters"]["candidate"]
            size = row["processed_bytes"]
            report.append(f'| {row["compiler"]} / {Path(row["input"]).name} | '
                          f'{a["instructions:u"]/size:.2f} → {b["instructions:u"]/size:.2f} | '
                          f'{100*(b["instructions:u"]/a["instructions:u"]-1):+.1f}% | '
                          f'{100*(b["cycles:u"]/a["cycles:u"]-1):+.1f}% |')
    (out / "summary.md").write_text("\n".join(report) + "\n")
    print(f"Snapshots, raw logs, assembly, counters and results: {out}")


if __name__ == "__main__":
    main()

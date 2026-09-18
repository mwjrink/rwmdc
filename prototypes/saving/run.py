#!/usr/bin/env python3
"""Build and measure isolated saving experiments; never launches or modifies rwmd.

python3 prototypes/saving/run.py --output prototypes/saving/results/RUN_NAME
Measurements use fresh scratch files under --data-dir (default: repository root),
NOT /tmp. Only that script-owned scratch directory is removed. Sources, metadata,
commands and JSONL results remain. No claim of physical power-cut testing.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "prototypes/saving"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="new directory for retained raw results")
    parser.add_argument("--data-dir", type=Path, default=ROOT, help="existing directory on the measured filesystem")
    parser.add_argument("--only", choices=("all", "io", "storage", "recovery", "queue"), default="all")
    parser.add_argument("--matrix", choices=("baseline", "queue-prealloc"), default="baseline")
    parser.add_argument("--quick", action="store_true", help="small smoke run, not a latency characterization")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    data_dir = args.data_dir.resolve(strict=True)
    queue_matrix = args.matrix == "queue-prealloc"
    iterations = 16 if args.quick else (1024 if queue_matrix else 256)
    repetitions = 1 if args.quick else (5 if queue_matrix else 3)
    flags = ["-std=gnu17", "-O2", "-g", "-Wall", "-Wextra", "-Werror"]
    build_io = args.only in ("all", "io", "queue")
    build_storage = args.only in ("all", "storage", "recovery")
    measured_sources = [Path(__file__)]
    if build_io:
        measured_sources.append(SOURCES / "io_bench.c")
    if build_storage:
        measured_sources.append(SOURCES / "storage_bench.c")
    compiler = subprocess.run(["clang", "--version"], text=True, capture_output=True, check=True).stdout
    metadata = {
        "schema": 1,
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "kernel": platform.release(), "machine": platform.machine(),
        "compiler": compiler.splitlines()[0], "compile_flags": flags,
        "data_directory": str(data_dir), "quick_smoke": args.quick,
        "matrix": args.matrix,
        "repetitions": repetitions, "io_iterations": iterations,
        "source_sha256": {p.name: sha256(p) for p in measured_sources},
        "scope": "QD1 serial durable transactions; API submission separate from persistence; no cold-cache or physical power-loss claim",
        "percentiles": "Each result reports its own samples. Compare per-run percentiles; do not merge percentiles as if they were raw samples.",
        "load_control": "No CPU pinning, frequency lock, cache dropping or control of unrelated system workloads.",
    }
    fs = subprocess.run(["findmnt", "-n", "-T", str(data_dir), "-o", "FSTYPE,OPTIONS"], text=True, capture_output=True)
    metadata["filesystem"] = fs.stdout.strip() if fs.returncode == 0 else "unavailable"
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    with (args.output / "results.jsonl").open("w") as results, tempfile.TemporaryDirectory(prefix=".rwmd-save-experiment-", dir=data_dir) as work:
        work = Path(work)
        io = work / "io-bench"
        storage = work / "storage-bench"

        def execute(command, *, kind, case=None, repetition=None, timeout=120):
            start = time.monotonic_ns()
            p = subprocess.run([str(x) for x in command], text=True, capture_output=True, timeout=timeout, cwd=ROOT)
            parsed = []
            if kind != "build":
                for line in p.stdout.splitlines():
                    try:
                        parsed.append(json.loads(line))
                    except json.JSONDecodeError:
                        pass
            row = {"kind": kind, "case": case, "repetition": repetition,
                   "command": [str(x) for x in command], "exit_code": p.returncode,
                   "process_wall_ns": time.monotonic_ns() - start,
                   "results": parsed, "stdout": p.stdout, "stderr": p.stderr}
            results.write(json.dumps(row) + "\n"); results.flush()
            print(f"{kind}: {case or Path(str(command[-1])).name}: exit={p.returncode}", flush=True)
            if p.returncode not in (0, 77):
                raise RuntimeError(f"experiment failed; see retained results.jsonl: {p.stderr}")
            if kind != "build" and not parsed:
                raise RuntimeError("experiment produced no JSON result")
            return row

        if build_io:
            execute(["clang", *flags, SOURCES / "io_bench.c", "-o", io, "-luring", "-pthread", "-lrt"], kind="build")
        if build_storage:
            execute(["clang", *flags, SOURCES / "storage_bench.c", "-o", storage, "-lcrypto"], kind="build")
        if args.only in ("all", "recovery"):
            execute([storage, "recover-check", work], kind="recovery", case="framing-replay-undo-and-commit-interruptions")
        if args.only == "queue" or (queue_matrix and build_io):
            execute([io, work, "queue-check"], kind="queue", case="atomic-ring-wrap-backpressure-wake-and-errors")
        if args.only in ("all", "io"):
            if queue_matrix:
                workloads = []
                for size in (128, 4096):
                    for allocation in ("off", "chunked-64k", "chunked-1m"):
                        count = 272 if args.quick and size == 4096 else iterations
                        workloads.append((size, 1, allocation, 0, count))
                    for allocation in ("off", "chunked-1m"):
                        workloads.append((size, 1, allocation, 20000, min(iterations, 64)))
            else:
                workloads = [(128, 1, "off", 0), (4096, 1, "off", 0),
                             (128, 16, "off", 0), (4096, 16, "off", 0),
                             (65536, 1, "off", 0),
                             (128, 1, "keep-size", 0), (4096, 1, "keep-size", 0),
                             (128, 1, "off", 20000)]
                workloads = [(size, group, allocation, pace, min(iterations, 64) if pace else iterations)
                             for size, group, allocation, pace in workloads]
            for rep in range(repetitions):
                engines = ["worker", "worker-ring"] if queue_matrix else ["sync", "worker", "aio", "uring", "uring-linked"]
                engines = engines[rep % len(engines):] + engines[:rep % len(engines)]
                if not queue_matrix and rep % 2:
                    engines.reverse()
                for size, group, prealloc, pace, count in workloads:
                    for engine in engines:
                        execute([io, work, engine, size, group, count, prealloc, pace],
                                kind="io", case=f"{engine}-{size}B-group{group}-{prealloc}-pace{pace}", repetition=rep)
        if args.only in ("all", "storage"):
            sizes = [1048576] if queue_matrix else [65536, 1048576, 16777216]
            modes = ["wal-packed", "wal-aligned"] if queue_matrix else [
                "wal-packed", "wal-aligned", "snapshot-direct", "snapshot-replace", "commit-direct", "commit-replace"]
            for rep in range(repetitions):
                ordered = modes[rep % len(modes):] + modes[:rep % len(modes)]
                if not queue_matrix and rep % 2:
                    ordered.reverse()
                for size in sizes:
                    count = 4 if args.quick else (12 if size >= 16777216 else 64)
                    for mode in ordered:
                        if queue_matrix:
                            count = (64 if mode == "wal-packed" else 272) if args.quick else (10000 if mode == "wal-packed" else 1024)
                        groups = [1] if queue_matrix else ([1, 16] if mode.startswith("wal-") else [1])
                        allocations = ["off", "chunked-64k", "chunked-1m"] if queue_matrix else ["off"]
                        allocations = allocations[rep % len(allocations):] + allocations[:rep % len(allocations)]
                        for group in groups:
                            for allocation in allocations:
                                execute([storage, "bench", work, mode, size, count * group, group, allocation], kind="storage",
                                        case=f"{mode}-{size}B-group{group}-{allocation}", repetition=rep)
    metadata["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Retained results: {args.output}")


if __name__ == "__main__":
    main()

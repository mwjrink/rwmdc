#!/usr/bin/env python3
"""Summarize retained saving measurements without pretending percentiles are samples.

Usage: python3 prototypes/saving/summarize.py prototypes/saving/results/RUN_NAME
Writes summary.json, io-summary.csv and storage-summary.csv beside raw JSONL.
Every reported percentile is the median of the separate runs' percentiles.
"""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
from statistics import median


def percentiles(rows, field, scale):
    out = {}
    for name in ("p50", "p95", "p99", "max", "mean"):
        values = [r[field][name] * scale for r in rows]
        out[f"{name}_us_median"] = median(values)
        out[f"{name}_us_min"] = min(values)
        out[f"{name}_us_max"] = max(values)
    return out


def save_csv(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def allocation_summary(rows):
    allocations = [r.get("allocation") for r in rows]
    fields = ("chunk_bytes", "allocation_calls", "growth_calls", "initial_allocation_ns",
              "allocation_ns_total", "allocation_ns_max", "reserved_bytes",
              "initial_size_before", "initial_size_after",
              "initial_blocks_before", "initial_blocks_after")
    return {f"allocation_{key}_median": median(a[key] for a in allocations)
            if all(a is not None for a in allocations) else None for key in fields}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    metadata = json.loads((args.run / "metadata.json").read_text())
    if "finished_utc" not in metadata:
        raise SystemExit("Run did not finish; raw partial results remain available.")
    io_groups, storage_groups = defaultdict(list), defaultdict(list)
    recovery, queue_checks, unsupported = [], [], []
    commands = [json.loads(line) for line in (args.run / "results.jsonl").read_text().splitlines()]
    for command in commands:
        if command["exit_code"] == 77:
            unsupported.append(command)
            continue
        if command["exit_code"] != 0:
            raise SystemExit(f"Failed command: {command['case']}")
        for r in command["results"]:
            if command["kind"] == "io":
                if r.get("verified") is not True:
                    raise SystemExit("I/O byte verification failed")
                key = tuple(r[k] for k in ("engine", "record_bytes", "records_per_sync", "preallocation", "pace_us"))
                io_groups[key].append(r)
            elif command["kind"] == "storage":
                if r.get("validation") is not True:
                    raise SystemExit("Storage replay/readback verification failed")
                key = tuple(r[k] for k in ("mode", "document_bytes", "batch")) + (r.get("preallocation", "off"),)
                storage_groups[key].append(r)
            elif command["kind"] == "recovery":
                if r.get("validation") is not True or r.get("failed") != 0:
                    raise SystemExit("Recovery fixture failed")
                recovery.append(r)
            elif command["kind"] == "queue":
                if r.get("verified") is not True or not all(c["pass"] for c in r.get("checks", [])):
                    raise SystemExit("Atomic queue correctness check failed")
                queue_checks.append(r)
    io = []
    for key, rows in sorted(io_groups.items()):
        row = dict(zip(("engine", "record_bytes", "records_per_sync", "preallocation", "pace_us"), key))
        row.update(runs=len(rows), groups_per_run=rows[0]["iterations"])
        row.update(percentiles(rows, "durable_ns", 0.001))
        row.update(submit_p50_us_median=median(r["submit_ns"]["p50"] / 1000 for r in rows),
                   submit_p99_us_median=median(r["submit_ns"]["p99"] / 1000 for r in rows),
                   write_complete_p50_us_median=median(r["write_complete_ns"]["p50"] / 1000 for r in rows),
                   cpu_us_per_group_median=median(r["cpu_ns"] / r["iterations"] / 1000 for r in rows),
                   setup_us_median=median(r["setup_ns"] / 1000 for r in rows),
                   preallocation_us_median=median(r["prealloc_ns"] / 1000 for r in rows))
        row.update(allocation_summary(rows))
        io.append(row)
    storage = []
    for key, rows in sorted(storage_groups.items()):
        row = dict(zip(("mode", "document_bytes", "batch", "preallocation"), key))
        row.update(runs=len(rows), edits_per_run=rows[0]["iterations"], groups_per_run=rows[0]["groups"])
        row.update(percentiles(rows, "latency_us", 1))
        row.update(bytes_written_per_edit=median(r["bytes_written"] / r["iterations"] for r in rows),
                   cpu_us_per_group_median=median(r["cpu_seconds"] / r["groups"] * 1e6 for r in rows),
                   wall_us_per_group_median=median(r["wall_seconds"] / r["groups"] * 1e6 for r in rows),
                   validation_us_median=median(r["validation_seconds"] * 1e6 for r in rows))
        for phase in rows[0]["phase_seconds"]:
            row[f"{phase}_us_per_group_median"] = median(r["phase_seconds"][phase] / r["groups"] * 1e6 for r in rows)
        row.update(allocation_summary(rows))
        storage.append(row)
    summary = {
        "method": "Median of per-run percentiles; not pooled-sample percentiles. Min/max preserve between-run variation. Batch latency includes waiting within its group, never divided by batch size.",
        "metadata": metadata, "io": io, "storage": storage, "recovery": recovery, "queue_checks": queue_checks,
        "unsupported": unsupported, "commands": len(commands),
    }
    (args.run / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    save_csv(args.run / "io-summary.csv", io)
    save_csv(args.run / "storage-summary.csv", storage)
    print(json.dumps({"run": str(args.run), "commands": len(commands), "io_cases": len(io),
                      "storage_cases": len(storage), "recovery_checks": sum(r["passed"] for r in recovery),
                      "queue_checks": sum(len(r["checks"]) for r in queue_checks),
                      "unsupported_commands": len(unsupported), "quick_smoke": metadata["quick_smoke"]}))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import argparse
import csv
import datetime
import os
import subprocess
import sys
import threading
from pathlib import Path

def parse_list(value):
    if not value:
        return []
    return [item.strip() for item in value.split(',') if item.strip()]

def run_binary(binary, threads, ops, workloads, seed):
    cmd = [str(binary), f"--threads={threads}", f"--ops={ops}", f"--workloads={workloads}", f"--seed={seed}", "--no-header"]
    start = datetime.datetime.utcnow()
    print(f"[bench] running {binary} threads={threads} ops={ops} workloads={workloads}", file=sys.stderr, flush=True)
    
    # Start subprocess and stream stderr for per-config timing.
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)

    stderr_lines = []
    def read_stderr():
        for line in proc.stderr:
            sys.stderr.write(line)
            stderr_lines.append(line)

    stderr_thread = threading.Thread(target=read_stderr)
    stderr_thread.start()

    stdout = proc.stdout.read()
    proc.wait()
    stderr_thread.join()
    stderr = "".join(stderr_lines)
    
    end = datetime.datetime.utcnow()
    duration = (end - start).total_seconds()
    if proc.returncode == 0:
        print(f"[bench] finished {binary} in {duration:.2f}s OK", file=sys.stderr, flush=True)
    else:
        print(f"[bench] failed {binary} in {duration:.2f}s FAIL", file=sys.stderr, flush=True)
    if proc.returncode != 0:
        return []
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    rows = []
    for line in lines:
        if line.startswith("allocator,"):
            continue
        rows.append(line.split(','))
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bins", required=True, help="Comma-separated list of benchmark binaries")
    parser.add_argument("--out", default="bench/results/bench.csv", help="Output CSV path")
    parser.add_argument("--threads", default="1,2,4,8", help="Comma-separated thread counts")
    parser.add_argument("--ops", default="200000", help="Operations per thread")
    parser.add_argument("--workloads", default="rl_small,rl_medium,fragmentation_mix,alignment64", help="Comma-separated workloads")
    parser.add_argument("--seed", default="42")
    args = parser.parse_args()

    bins = [Path(p) for p in parse_list(args.bins)]
    if not bins:
        sys.stderr.write("No binaries provided.\n")
        return 1

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    header = [
        "allocator",
        "workload",
        "threads",
        "ops_per_thread",
        "total_ops",
        "seconds",
        "throughput_ops_s",
        "alloc_p50_ns",
        "alloc_p99_ns",
        "free_p50_ns",
        "free_p99_ns",
        "avg_overhead_ratio",
        "peak_live_requested",
        "peak_live_usable",
        "alignment",
    ]

    all_rows = []
    for binary in bins:
        if not binary.exists():
            sys.stderr.write(f"[bench] Skipping missing binary: {binary}\n")
            continue
        rows = run_binary(binary, args.threads, args.ops, args.workloads, args.seed)
        all_rows.extend(rows)

    with out_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        for row in all_rows:
            if len(row) != len(header):
                sys.stderr.write(f"[bench] Skipping malformed row: {row}\n")
                continue
            writer.writerow(row)

    meta_path = out_path.parent / "bench_meta.txt"
    with meta_path.open("w") as f:
        f.write(f"timestamp={datetime.datetime.utcnow().isoformat()}Z\n")
        f.write(f"bins={','.join(str(b) for b in bins)}\n")
        f.write(f"threads={args.threads}\n")
        f.write(f"ops_per_thread={args.ops}\n")
        f.write(f"workloads={args.workloads}\n")

    print(f"Wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
import argparse
import csv
import datetime
import os
import time
import subprocess
import sys
import threading
from pathlib import Path

def parse_list(value):
    if not value:
        return []
    return [item.strip() for item in value.split(',') if item.strip()]

def allocator_label(binary):
    name = binary.name
    if "bench_system" in name:
        return "system"
    if "bench_mempool_baseline" in name:
        return "mempool_baseline"
    if "bench_mempool_sharded" in name:
        return "mempool_sharded"
    if "bench_jemalloc" in name:
        return "jemalloc"
    if "bench_tcmalloc" in name:
        return "tcmalloc"
    return name


def load_completed_csv(path):
    completed = set()
    if not path or not Path(path).exists():
        return completed
    with open(path, newline="") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or row[0] == "allocator":
                continue
            if len(row) < 4:
                continue
            completed.add(f"{row[0]}|{row[1]}|{row[2]}|{row[3]}")
    return completed


def write_timeout_row(writer, csv_file, allocator, workload, threads, ops, alignment, timeout_s):
    total_ops = int(threads) * int(ops)
    row = [
        allocator,
        workload,
        str(threads),
        str(ops),
        str(total_ops),
        str(float(timeout_s)),
        str(0.0),
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        "0",
        str(alignment),
    ]
    writer.writerow(row)
    csv_file.flush()


def run_binary(binary, threads, ops, workloads, seed, resume_csv, writer, csv_file, header_len, timeout_s):
    cmd = [str(binary), f"--threads={threads}", f"--ops={ops}", f"--workloads={workloads}", f"--seed={seed}", "--no-header"]
    if resume_csv:
        cmd.append(f"--resume-csv={resume_csv}")
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

    timed_out = False
    try:
        for line in proc.stdout:
            line = line.strip()
            if not line or line.startswith("allocator,"):
                continue
            row = line.split(',')
            if len(row) != header_len:
                sys.stderr.write(f"[bench] Skipping malformed row: {row}\n")
                continue
            writer.writerow(row)
            csv_file.flush()
        if timeout_s:
            proc.wait(timeout=timeout_s)
        else:
            proc.wait()
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    except KeyboardInterrupt:
        proc.terminate()
        proc.wait()
        raise
    stderr_thread.join()
    stderr = "".join(stderr_lines)
    
    end = datetime.datetime.utcnow()
    duration = (end - start).total_seconds()
    if proc.returncode == 0:
        print(f"[bench] finished {binary} in {duration:.2f}s OK", file=sys.stderr, flush=True)
    else:
        print(f"[bench] failed {binary} in {duration:.2f}s FAIL", file=sys.stderr, flush=True)
    if timed_out:
        sys.stderr.write(f"[bench] timeout after {timeout_s}s for {binary}\n")
        return "timeout"
    if proc.returncode != 0:
        return False
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bins", required=True, help="Comma-separated list of benchmark binaries")
    parser.add_argument("--out", default="bench/results/bench.csv", help="Output CSV path")
    parser.add_argument("--threads", default="1,2,4,8", help="Comma-separated thread counts")
    parser.add_argument("--ops", default="200000", help="Operations per thread")
    parser.add_argument("--workloads", default="rl_small,rl_medium,fragmentation_mix,alignment64", help="Comma-separated workloads")
    parser.add_argument("--seed", default="42")
    parser.add_argument("--resume", action="store_true", help="Append to existing CSV and skip completed configs")
    parser.add_argument("--timeout", type=int, default=0, help="Per-config timeout in seconds (0 = no timeout)")
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

    resume_csv = ""
    completed = set()
    if args.resume:
        resume_csv = str(out_path)
        completed = load_completed_csv(resume_csv)

    write_header = True
    if resume_csv and out_path.exists():
        write_header = False

    with out_path.open("a" if not write_header else "w", newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(header)
            f.flush()
        try:
            if args.timeout > 0:
                thread_list = [int(t) for t in parse_list(args.threads)]
                workload_list = parse_list(args.workloads)
                for binary in bins:
                    if not binary.exists():
                        sys.stderr.write(f"[bench] Skipping missing binary: {binary}\n")
                        continue
                    allocator = allocator_label(binary)
                    for workload in workload_list:
                        for threads in thread_list:
                            key = f"{allocator}|{workload}|{threads}|{args.ops}"
                            if key in completed:
                                sys.stderr.write(f"[bench] Skipping completed {key}\n")
                                continue
                            ok = run_binary(binary, str(threads), args.ops, workload, args.seed,
                                            resume_csv, writer, f, len(header), args.timeout)
                            if ok == "timeout":
                                alignment = 64 if workload == "alignment64" else 0
                                write_timeout_row(writer, f, allocator, workload, threads, args.ops, alignment, args.timeout)
                            elif ok is False:
                                sys.stderr.write(f"[bench] {binary} failed; continuing.\n")
            else:
                for binary in bins:
                    if not binary.exists():
                        sys.stderr.write(f"[bench] Skipping missing binary: {binary}\n")
                        continue
                    ok = run_binary(binary, args.threads, args.ops, args.workloads, args.seed,
                                    resume_csv, writer, f, len(header), 0)
                    if not ok:
                        sys.stderr.write(f"[bench] {binary} failed; continuing.\n")
        except KeyboardInterrupt:
            sys.stderr.write("\n[bench] Interrupted; partial results saved.\n")
            return 130

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

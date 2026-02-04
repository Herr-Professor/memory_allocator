#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def load_rows(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append({
                "allocator": row["allocator"],
                "workload": row["workload"],
                "threads": int(row["threads"]),
                "throughput": float(row["throughput_ops_s"]),
                "overhead": float(row["avg_overhead_ratio"]),
                "alloc_p50": float(row["alloc_p50_ns"]),
                "alloc_p99": float(row["alloc_p99_ns"]),
            })
    return rows


def plot_throughput_by_workload(rows, out_dir):
    workloads = sorted({r["workload"] for r in rows})
    allocators = sorted({r["allocator"] for r in rows})

    fig, axes = plt.subplots(len(workloads), 1, figsize=(8, 3 * len(workloads)), constrained_layout=True)
    if len(workloads) == 1:
        axes = [axes]

    for ax, workload in zip(axes, workloads):
        subset = [r for r in rows if r["workload"] == workload]
        if not subset:
            continue
        max_threads = max(r["threads"] for r in subset)
        subset = [r for r in subset if r["threads"] == max_threads]

        allocators_sorted = [a for a in allocators if any(r["allocator"] == a for r in subset)]
        values = [next(r for r in subset if r["allocator"] == a)["throughput"] for a in allocators_sorted]

        ax.bar(allocators_sorted, values, color="#4c72b0")
        ax.set_title(f"Throughput @ {max_threads} threads — {workload}")
        ax.set_ylabel("ops/sec")
        ax.tick_params(axis='x', rotation=20)

    fig.savefig(out_dir / "throughput_by_workload.png", dpi=150)


def plot_scaling(rows, out_dir, workload="rl_small"):
    subset = [r for r in rows if r["workload"] == workload]
    if not subset:
        return

    allocators = sorted({r["allocator"] for r in subset})
    fig, ax = plt.subplots(figsize=(8, 4))

    for allocator in allocators:
        points = sorted([r for r in subset if r["allocator"] == allocator], key=lambda r: r["threads"])
        ax.plot([p["threads"] for p in points], [p["throughput"] for p in points], marker="o", label=allocator)

    ax.set_title(f"Scaling — {workload}")
    ax.set_xlabel("threads")
    ax.set_ylabel("ops/sec")
    ax.legend()
    fig.savefig(out_dir / "scaling_rl_small.png", dpi=150)


def plot_overhead(rows, out_dir, workload="fragmentation_mix"):
    subset = [r for r in rows if r["workload"] == workload]
    if not subset:
        return

    max_threads = max(r["threads"] for r in subset)
    subset = [r for r in subset if r["threads"] == max_threads]
    allocators = sorted({r["allocator"] for r in subset})
    values = [next(r for r in subset if r["allocator"] == a)["overhead"] for a in allocators]

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.bar(allocators, values, color="#55a868")
    ax.set_title(f"Average usable/requested @ {max_threads} threads — {workload}")
    ax.set_ylabel("ratio")
    ax.tick_params(axis='x', rotation=20)
    fig.savefig(out_dir / "overhead_ratio.png", dpi=150)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", default="bench/results/bench.csv")
    parser.add_argument("--out", default="bench/plots")
    args = parser.parse_args()

    rows = load_rows(args.csv)
    if not rows:
        print("No data to plot.")
        return 1

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    plot_throughput_by_workload(rows, out_dir)
    plot_scaling(rows, out_dir)
    plot_overhead(rows, out_dir)

    print(f"Plots written to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

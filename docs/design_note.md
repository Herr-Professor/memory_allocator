# Design Note: Memory Allocator for RL/Inference Workloads

## Goal
This allocator targets RL and inference system patterns: many small allocations, high churn, multithreaded workloads, and alignment-sensitive buffers. The design favors predictable latency and low contention under load while keeping fragmentation in check for mixed-size allocations.

## Model Overview
**Core data structures**
- **Fixed-size allocators (32/128/256B)**: chunked slab allocators with per-size free lists. Best for hot small objects.
- **Segregated free lists (32–4096B classes)**: size-class lists for medium blocks to reduce best-fit scanning cost.
- **Best-fit free list**: a size-indexed multimap for larger/mixed sizes, enabling good reuse and lower fragmentation.
- **Thread-local caches**: per-thread stash for fixed-size blocks to avoid locks on hot paths.

**Allocation strategy selection**
- Small/medium sizes are routed to fixed-size allocators first.
- Medium sizes (<=512B) can fall through to segregated lists.
- Larger/mixed sizes use best-fit.

This hybrid strategy keeps the hottest path in O(1) with strong locality, while still giving reasonable fragmentation behavior for larger, variable-sized allocations.

## “Hard” Feature: Contention Reduction
We introduce **sharded locks for segregated size classes**. Instead of a single global lock for all pool operations, each segregated free list has its own mutex. The fast path for the segregated allocator uses only the class lock when no scopes are active:
- **Before**: every segregated alloc/free required the global pool lock.
- **After**: alloc/free on a hot size class only grabs the class lock, avoiding contention across unrelated sizes.

This is intentionally minimal and composable with the existing thread-local caches. It yields a measurable throughput improvement in multi-threaded, small/medium-heavy workloads without large structural changes.

## Workloads (RL/Inference-style)
The benchmark suite simulates common allocation patterns:
- **`rl_small`**: 16–512B sizes, high churn, large live set.
- **`rl_medium`**: 128–4096B sizes, moderate churn.
- **`fragmentation_mix`**: wide size distribution to stress fragmentation.
- **`alignment64`**: 64B-aligned buffers for vectorized kernels.

Each workload runs across multiple thread counts and records:
- **Throughput** (ops/sec)
- **Latency** (p50/p99 for alloc/free)
- **Fragmentation proxy** (usable/requested ratio)
- **Peak live bytes**

## Results Summary (to be generated)
Run `make bench` to produce `bench/results/bench.csv` and plots in `bench/plots/`.

### Results Summary (run: February 4, 2026; BENCH_OPS=20000, BENCH_THREADS=1,2)
These numbers are from `bench/results/bench.csv` on a short run (20k ops, 1–2 threads). At 2 threads:
- **`rl_small` throughput**: system `1.542e7 ops/s`, mempool_baseline `8.865e6`, mempool_sharded `3.840e6`. p99 alloc/free for baseline is `42/42 ns`, sharded `83/6958 ns`.
- **`rl_medium` throughput**: system `1.105e7 ops/s`, mempool_baseline `3.620e6`, mempool_sharded `2.175e6`. p99 alloc/free for baseline `458/417 ns`, sharded `5083/1958 ns`.
- **`fragmentation_mix` throughput**: system `1.632e7 ops/s`, mempool_baseline `3.913e6`, mempool_sharded `2.774e6`. p99 alloc/free for baseline `375/6125 ns`, sharded `3500/13250 ns`.
- **`alignment64` throughput**: system `1.456e7 ops/s`, mempool_baseline `5.287e6`, mempool_sharded `4.534e6`. p99 alloc/free for baseline `250/1083 ns`, sharded `166/4458 ns`.
- **Fragmentation proxy (avg usable/requested)**: baseline and sharded are similar within each workload; e.g. `rl_small` ~`1.17`, `rl_medium` ~`1.00`, `fragmentation_mix` ~`1.01`, `alignment64` ~`1.72`.

**Interpretation:** In this short 1–2 thread run, the sharded segregated path underperformed the baseline in throughput and tail latency. This suggests the extra class‑lock overhead is not paying off at low thread counts, or the sharded fast path is not yet tuned (e.g., refill strategy). Re‑run with higher thread counts and longer ops to validate scaling before drawing final conclusions.

### Surprises / Notes
- Document any unexpected regressions, e.g. alignment-heavy workloads or specific size-class anomalies.
- Note any skew in per-size-class latency distribution.

## Tradeoffs and Future Work
**Pros**
- Fast O(1) hot path for small sizes.
- Lower contention via thread-local caches and sharded locks.
- Predictable latency for common allocations.

**Cons**
- Best-fit path still uses a global lock and can become a bottleneck for large mixed allocations.
- Fragmentation can increase when sizes cluster poorly against the fixed-size buckets.

**Next improvements**
- Per-thread arenas with cross-thread remote free queues.
- Lock-free free lists for fixed-size allocators.
- NUMA-aware shard placement for multi-socket systems.

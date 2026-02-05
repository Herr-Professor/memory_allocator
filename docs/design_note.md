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

## Results Summary
Run `make bench` to produce `bench/results/bench.csv` and plots in `bench/plots/`.

### Results Summary (run: February 5, 2026; BENCH_OPS=200000, BENCH_THREADS=1,2,4,8)
These numbers are from `bench/results/bench.csv` on the paper‑grade run (200k ops, 1–8 threads). At 8 threads:
- **`rl_small` throughput**: system `2.554e7 ops/s`, mempool_baseline `2.697e6`, mempool_sharded `3.454e6`. p99 alloc/free: baseline `2750/98459 ns`, sharded `209/88792 ns`.
- **`rl_medium` throughput**: system `1.696e7 ops/s`, mempool_baseline `7.083e5`, mempool_sharded `7.917e5`. p99 alloc/free: baseline `172500/332333 ns`, sharded `162708/243959 ns`.
- **`fragmentation_mix` throughput**: system `1.047e7 ops/s`, mempool_baseline `2.130e5`, mempool_sharded `2.528e5`. p99 alloc/free: baseline `484584/851417 ns`, sharded `341292/747833 ns`.
- **`alignment64` throughput**: system `1.959e7 ops/s`, mempool_baseline `1.158e6`, mempool_sharded `1.158e6`. p99 alloc/free: baseline `79084/161541 ns`, sharded `58666/189625 ns`.
- **Fragmentation proxy (avg usable/requested)**: baseline and sharded are similar within each workload; e.g. `rl_small` ~`1.18`, `rl_medium` ~`1.00`, `fragmentation_mix` ~`1.01`, `alignment64` ~`1.71`.

**Interpretation:** At 8 threads, the sharded variant improves throughput slightly over baseline across the RL/inference‑style workloads, but both remain far below system malloc and exhibit very high tail latencies, especially in `rl_medium` and `fragmentation_mix`. This is a clear negative result: sharded locks reduce some contention but do not solve the core throughput/latency gap.

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

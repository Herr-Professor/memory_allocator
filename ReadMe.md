This is a research-focused allocator benchmark suite with a custom memory pool and reproducible evaluation harness.

# Custom Memory Allocator (Research Repo)

This repo implements a high-performance, thread-safe allocator with multiple strategies and a benchmark harness tailored to RL/inference allocation patterns. The benchmark suite compares this allocator against system `malloc` and (optionally) `jemalloc`/`tcmalloc`, with reproducible CSV outputs and plots.

## Features

### Multiple Allocation Strategies
- **Best Fit (`AllocationStrategy::BEST_FIT`)**: Optimal memory utilization for variable-sized blocks
- **Fixed Size (`AllocationStrategy::FIXED_SIZE`)**: Fast allocation for uniform-sized blocks
- **Pool Based (`AllocationStrategy::POOL_BASED`)**: Efficient pool of fixed-size blocks
- **Segregated (`AllocationStrategy::SEGREGATED`)**: Separate free lists for different size classes

### Memory Management
- Thread-safe operations with mutex protection
- AVX2 alignment support (16-byte alignment)
- Scope-based memory management
- Memory statistics tracking
- Memory pool with 1MB chunks
- Specialized allocators for small (32 bytes) and medium (128 bytes) allocations

### Performance Optimizations
- Thread-local storage for memory pools
- Fixed-size block optimization for common allocation sizes
- Efficient memory reuse through free lists
- Chunk-based memory allocation (64KB chunks for fixed-size allocators)

### Integration Features
- STL-compatible custom allocator template
- Allocation statistics tracking
- Memory scope management
- Pool reset capability

## Core Components

### 1. AllocationStats
Tracks memory usage statistics including:
- Per-thread allocation counts
- Total bytes allocated
- Global allocation statistics
- Statistics merging and reporting capabilities

### 2. MemoryBlock
Basic memory block management:
- 16-byte alignment for AVX2 compatibility
- Block metadata (size, status, strategy)
- Next block pointer for free list management
- Data access methods

### 3. FixedSizeAllocator<BlockSize>
Specialized allocator for fixed-size blocks:
- Template-based size specification
- Chunk-based memory management (64KB chunks)
- Fast allocation and deallocation
- Thread-safe operations

### 4. MemoryPool
Main memory management component:
- Multiple allocation strategy support
- Scope-based memory management
- Fixed-size allocator integration
- 1MB memory chunks
- Thread-safe operations

### 5. CustomAllocator<T>
STL-compatible allocator template:
- Standard allocator interface
- Automatic strategy selection based on type properties
- Integration with thread-local memory pools
- Exception safety

## Benchmark Suite (RL/Inference Focus)

The benchmark harness lives under `bench/` and is designed to mimic RL/inference allocation patterns (many small alloc/free, multithreaded churn, alignment-heavy buffers).

### Quick Start (paper-grade)
```
make bench
```

### Resume (long runs)
```
make bench_resume
```

### Smoke run (fast)
```
make bench_smoke
```

### Outputs
- CSV: `bench/results/bench.csv`
- Plots: `bench/plots/throughput_by_workload.png`, `bench/plots/scaling_rl_small.png`, `bench/plots/overhead_ratio.png`

### Baselines
By default the suite compares:
- `system` (glibc/Apple malloc)
- `mempool_baseline`
- `mempool_sharded`

If available, it also builds:
- `jemalloc`
- `tcmalloc`

### Timeouts
Paper runs can be long. The default per-config timeout is **1200s**. Override via:
```
make bench BENCH_TIMEOUT=1800
```

## Best Practices

1. **Strategy Selection**
   - Use `FIXED_SIZE` for small, uniform allocations
   - Use `BEST_FIT` for variable-sized allocations
   - Use `POOL_BASED` for frequent allocations/deallocations
   - Use `SEGREGATED` for mixed-size allocations

2. **Scope Management**
   - Use scope-based management for temporary allocations
   - Call `end_scope()` to prevent memory leaks
   - Use `reset()` when reusing pools

3. **Performance Optimization**
   - Leverage thread-local storage for concurrent applications
   - Use fixed-size allocators for small objects
   - Batch allocations when possible

4. **Memory Tracking**
   - Monitor allocation statistics for memory usage patterns
   - Use `AllocationStats::print_stats()` for debugging
   - Merge thread stats when analyzing global memory usage

## Implementation Details

- Thread safety through mutex protection
- Memory alignment for optimal performance
- Efficient free list management
- Smart strategy selection based on allocation patterns
- Exception safety in allocation operations

## Technical Specifications

- Minimum alignment: 16 bytes (AVX2)
- Default pool size: 1MB
- Fixed-size chunk size: 64KB
- Small allocation threshold: 32 bytes
- Medium allocation threshold: 128 bytes

## Thread Safety

The implementation is thread-safe through:
- Mutex protection for critical sections
- Thread-local storage for pools
- Atomic counters for statistics
- Thread-local allocation tracking

This allocator is designed for high-performance applications requiring custom memory management with multiple allocation strategies and thread safety.

## Repo Structure
- `memory_pool.h/.cpp`: allocator implementation
- `bench/`: benchmark harness + allocator adapters
- `scripts/`: bench runner + plotting
- `docs/design_note.md`: 2–4 page design note and results
- `Makefile`: build + bench targets

## Reproduction
To make results comparable, record the following in your report or README:
- **Compiler**: `c++ --version` (or `clang++ --version`)
- **OS**: `uname -a`
- **CPU**: `sysctl -n machdep.cpu.brand_string` (macOS) or `lscpu` (Linux)
- **Build flags**: see `Makefile` and `toolchain.lock`

## Known Issues
- **Sharded variant slowdown**: On higher thread counts and mixed-size workloads, `mempool_sharded` can be significantly slower than `mempool_baseline`. This is a real benchmark result; see `docs/design_note.md` for measured deltas and interpretation.

## Design Note
See `docs/design_note.md` for the allocator model, tradeoffs, and benchmark interpretation.

## Benchmark Suite (RL/Inference Focus)

This repo now includes a reproducible benchmark harness that mimics RL/inference allocation patterns (many small alloc/free, multithreaded churn, and alignment-heavy buffers).

### Quick Start

```
make bench
```

### Outputs
- CSV results: `bench/results/bench.csv`
- Plots: `bench/plots/throughput_by_workload.png`, `bench/plots/scaling_rl_small.png`, `bench/plots/overhead_ratio.png`

### Baselines
By default, the suite compares:
- `mempool_baseline` (segregated lists with a single lock)
- `mempool_sharded` (segregated lists with sharded locks)
- `system` (glibc/Apple malloc)

If available, it also builds:
- `jemalloc`
- `tcmalloc`

### Reproducibility
- Pinned toolchain: `toolchain.lock`
- Benchmark scripts: `scripts/run_bench.py`, `scripts/plot.py`
- CI runs a smaller benchmark on PRs

### Design Note
See `docs/design_note.md` for allocator model, tradeoffs, and how to interpret the benchmark results.

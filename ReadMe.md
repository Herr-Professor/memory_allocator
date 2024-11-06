This is a work in progress;

# Custom Memory Allocator

A high-performance, thread-safe memory allocator implementation with multiple allocation strategies and STL container support.

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

## Tests

### Basic Memory Operations (test1.cpp)
A simple test that demonstrates basic memory pool operations:
- Single block allocation and deallocation
- Memory reuse verification
- Basic cleanup operations

### Stress Test (test2.cpp)
A comprehensive stress test that validates:
- Large-scale allocations (1000 initial blocks)
- Random allocation patterns (5000 operations)
- Memory reuse efficiency
- Variable block sizes (16-256 bytes)
- Three types of operations:
  - New allocations (A)
  - Deallocations (D)
  - Reallocations (R)
- Memory statistics tracking
- Proper cleanup of all allocated blocks

The stress test provides visual feedback of operations and periodic statistics reporting, helping identify potential issues with:
- Memory fragmentation
- Memory leaks
- Free list management
- Memory reuse efficiency
- Thread safety

### Performance Comparison Test (test3.cpp)
A comprehensive benchmark that:
- Compares MemoryPool against standard malloc/free
- Performs 10,000 initial allocations
- Executes 50,000 random operations
- Tracks and visualizes:
  - Memory usage with ASCII progress bars
  - Real-time allocation (R) and deallocation (D) operations
  - Peak memory usage
  - Total execution time
  - Average operation time
  - Allocation/deallocation counts

The test helps identify:
- Performance differences between allocators
- Memory usage patterns
- Memory fragmentation effects
- Allocation/deallocation efficiency

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

This memory allocator is designed for high-performance applications requiring custom memory management with multiple allocation strategies and thread safety.

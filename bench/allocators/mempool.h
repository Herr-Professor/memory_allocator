#pragma once

#include "allocator_api.h"

#include "memory_pool.h"

#ifndef MEMPOOL_VARIANT_LABEL
#define MEMPOOL_VARIANT_LABEL "mempool"
#endif

namespace {
MemoryPool bench_pool(true);
}

inline const char* allocator_name() {
    return MEMPOOL_VARIANT_LABEL;
}

inline void* alloc(size_t size) {
    return bench_pool.allocate(size, AllocationStrategy::BEST_FIT);
}

inline void* alloc_aligned(size_t size, size_t alignment) {
    return bench_pool.allocate_aligned(size, alignment, AllocationStrategy::BEST_FIT);
}

inline void dealloc(void* ptr, bool aligned) {
    if (aligned) {
        bench_pool.deallocate_aligned(ptr);
    } else {
        bench_pool.deallocate(ptr);
    }
}

inline size_t usable_size(void* ptr, size_t /*requested*/, bool aligned) {
    if (!ptr) {
        return 0;
    }
    void* raw = aligned ? reinterpret_cast<void**>(ptr)[-1] : ptr;
    return MemoryPool::usable_size(raw);
}

inline void thread_init() {}
inline void thread_teardown() {
    bench_pool.release_thread_cache();
}
inline void allocator_reset() {
    bench_pool.reset();
}

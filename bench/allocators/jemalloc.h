#pragma once

#include "allocator_api.h"

#include <cstdlib>
#include <cstddef>

#if defined(__APPLE__)
#include <malloc/malloc.h>
static inline size_t usable_size_impl(void* ptr) {
    return ptr ? malloc_size(ptr) : 0;
}
#elif defined(__linux__)
#include <malloc.h>
static inline size_t usable_size_impl(void* ptr) {
    return ptr ? malloc_usable_size(ptr) : 0;
}
#else
static inline size_t usable_size_impl(void* ptr) {
    (void)ptr;
    return 0;
}
#endif

inline const char* allocator_name() {
    return "jemalloc";
}

inline void* alloc(size_t size) {
    return std::malloc(size);
}

inline void* alloc_aligned(size_t size, size_t alignment) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}

inline void dealloc(void* ptr, bool /*aligned*/) {
    std::free(ptr);
}

inline size_t usable_size(void* ptr, size_t requested, bool /*aligned*/) {
    size_t usable = usable_size_impl(ptr);
    return usable > 0 ? usable : requested;
}

inline void thread_init() {}
inline void thread_teardown() {}
inline void allocator_reset() {}

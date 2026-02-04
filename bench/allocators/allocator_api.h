#pragma once

#include <cstddef>

const char* allocator_name();
void* alloc(size_t size);
void* alloc_aligned(size_t size, size_t alignment);
void dealloc(void* ptr, bool aligned);
size_t usable_size(void* ptr, size_t requested, bool aligned);
void thread_init();
void thread_teardown();
void allocator_reset();

#include "memory_pool.h"

// Initialize static members of AllocationStats
thread_local size_t AllocationStats::allocations = 0;
thread_local size_t AllocationStats::deallocations = 0;
thread_local size_t AllocationStats::bytes_allocated = 0;
thread_local size_t AllocationStats::last_reported_bytes = 0;
std::atomic<size_t> AllocationStats::total_allocations{0};
std::atomic<size_t> AllocationStats::total_deallocations{0};
std::atomic<size_t> AllocationStats::total_bytes{0};

#if ENABLE_ALLOC_TIMING
thread_local uint64_t AllocationTimingStats::overall_ns = 0;
thread_local uint64_t AllocationTimingStats::best_fit_ns = 0;
thread_local uint64_t AllocationTimingStats::pool_ns = 0;
thread_local uint64_t AllocationTimingStats::segregated_ns = 0;
thread_local uint64_t AllocationTimingStats::segregated_refill_ns = 0;
thread_local uint64_t AllocationTimingStats::fixed32_ns = 0;
thread_local uint64_t AllocationTimingStats::fixed128_ns = 0;
thread_local uint64_t AllocationTimingStats::dealloc_ns = 0;

thread_local size_t AllocationTimingStats::overall_count = 0;
thread_local size_t AllocationTimingStats::best_fit_count = 0;
thread_local size_t AllocationTimingStats::pool_count = 0;
thread_local size_t AllocationTimingStats::segregated_count = 0;
thread_local size_t AllocationTimingStats::segregated_refill_count = 0;
thread_local size_t AllocationTimingStats::fixed32_count = 0;
thread_local size_t AllocationTimingStats::fixed128_count = 0;
thread_local size_t AllocationTimingStats::dealloc_count = 0;

std::atomic<uint64_t> AllocationTimingStats::total_overall_ns{0};
std::atomic<uint64_t> AllocationTimingStats::total_best_fit_ns{0};
std::atomic<uint64_t> AllocationTimingStats::total_pool_ns{0};
std::atomic<uint64_t> AllocationTimingStats::total_segregated_ns{0};
std::atomic<uint64_t> AllocationTimingStats::total_segregated_refill_ns{0};
std::atomic<uint64_t> AllocationTimingStats::total_fixed32_ns{0};
std::atomic<uint64_t> AllocationTimingStats::total_fixed128_ns{0};
std::atomic<uint64_t> AllocationTimingStats::total_dealloc_ns{0};

std::atomic<size_t> AllocationTimingStats::total_overall_count{0};
std::atomic<size_t> AllocationTimingStats::total_best_fit_count{0};
std::atomic<size_t> AllocationTimingStats::total_pool_count{0};
std::atomic<size_t> AllocationTimingStats::total_segregated_count{0};
std::atomic<size_t> AllocationTimingStats::total_segregated_refill_count{0};
std::atomic<size_t> AllocationTimingStats::total_fixed32_count{0};
std::atomic<size_t> AllocationTimingStats::total_fixed128_count{0};
std::atomic<size_t> AllocationTimingStats::total_dealloc_count{0};

void AllocationTimingStats::record(TimingCategory category, uint64_t ns) {
    switch (category) {
        case TimingCategory::OverallAllocate:
            overall_ns += ns;
            overall_count += 1;
            break;
        case TimingCategory::BestFit:
            best_fit_ns += ns;
            best_fit_count += 1;
            break;
        case TimingCategory::PoolBased:
            pool_ns += ns;
            pool_count += 1;
            break;
        case TimingCategory::Segregated:
            segregated_ns += ns;
            segregated_count += 1;
            break;
        case TimingCategory::SegregatedRefill:
            segregated_refill_ns += ns;
            segregated_refill_count += 1;
            break;
        case TimingCategory::Fixed32:
            fixed32_ns += ns;
            fixed32_count += 1;
            break;
        case TimingCategory::Fixed128:
            fixed128_ns += ns;
            fixed128_count += 1;
            break;
        case TimingCategory::Deallocate:
            dealloc_ns += ns;
            dealloc_count += 1;
            break;
    }
}

void AllocationTimingStats::merge_thread_stats() {
    total_overall_ns.fetch_add(overall_ns, std::memory_order_relaxed);
    total_best_fit_ns.fetch_add(best_fit_ns, std::memory_order_relaxed);
    total_pool_ns.fetch_add(pool_ns, std::memory_order_relaxed);
    total_segregated_ns.fetch_add(segregated_ns, std::memory_order_relaxed);
    total_segregated_refill_ns.fetch_add(segregated_refill_ns, std::memory_order_relaxed);
    total_fixed32_ns.fetch_add(fixed32_ns, std::memory_order_relaxed);
    total_fixed128_ns.fetch_add(fixed128_ns, std::memory_order_relaxed);
    total_dealloc_ns.fetch_add(dealloc_ns, std::memory_order_relaxed);

    total_overall_count.fetch_add(overall_count, std::memory_order_relaxed);
    total_best_fit_count.fetch_add(best_fit_count, std::memory_order_relaxed);
    total_pool_count.fetch_add(pool_count, std::memory_order_relaxed);
    total_segregated_count.fetch_add(segregated_count, std::memory_order_relaxed);
    total_segregated_refill_count.fetch_add(segregated_refill_count, std::memory_order_relaxed);
    total_fixed32_count.fetch_add(fixed32_count, std::memory_order_relaxed);
    total_fixed128_count.fetch_add(fixed128_count, std::memory_order_relaxed);
    total_dealloc_count.fetch_add(dealloc_count, std::memory_order_relaxed);

    overall_ns = best_fit_ns = pool_ns = segregated_ns = segregated_refill_ns = 0;
    fixed32_ns = fixed128_ns = dealloc_ns = 0;

    overall_count = best_fit_count = pool_count = segregated_count = segregated_refill_count = 0;
    fixed32_count = fixed128_count = dealloc_count = 0;
}

void AllocationTimingStats::print_stats() {
    merge_thread_stats();
    auto print_line = [](const char* label, uint64_t total_ns, size_t count) {
        if (count == 0) {
            std::cout << label << ": count=0 total_ns=0 avg_ns=0" << std::endl;
            return;
        }
        double avg = static_cast<double>(total_ns) / static_cast<double>(count);
        std::cout << label << ": count=" << count
                  << " total_ns=" << total_ns
                  << " avg_ns=" << avg << std::endl;
    };

    print_line("Overall allocate", total_overall_ns.load(std::memory_order_relaxed),
               total_overall_count.load(std::memory_order_relaxed));
    print_line("Best fit", total_best_fit_ns.load(std::memory_order_relaxed),
               total_best_fit_count.load(std::memory_order_relaxed));
    print_line("Pool based", total_pool_ns.load(std::memory_order_relaxed),
               total_pool_count.load(std::memory_order_relaxed));
    print_line("Segregated allocate", total_segregated_ns.load(std::memory_order_relaxed),
               total_segregated_count.load(std::memory_order_relaxed));
    print_line("Segregated refill", total_segregated_refill_ns.load(std::memory_order_relaxed),
               total_segregated_refill_count.load(std::memory_order_relaxed));
    print_line("Fixed 32 allocate", total_fixed32_ns.load(std::memory_order_relaxed),
               total_fixed32_count.load(std::memory_order_relaxed));
    print_line("Fixed 128 allocate", total_fixed128_ns.load(std::memory_order_relaxed),
               total_fixed128_count.load(std::memory_order_relaxed));
    print_line("Deallocate", total_dealloc_ns.load(std::memory_order_relaxed),
               total_dealloc_count.load(std::memory_order_relaxed));
}
#endif
// Thread-local instances: each thread gets its own pool/cache unless explicitly shared.
thread_local MemoryPool global_pool(false);
thread_local MemoryPool::ThreadLocalCache MemoryPool::thread_cache{};

void* MemoryPool::allocate_best_fit(size_t size) {
    ScopedTiming timing(TimingCategory::BestFit);
    auto it = size_index.lower_bound(size);
    if (it == size_index.end()) {
        add_new_chunk();
        it = size_index.lower_bound(size);
        if (it == size_index.end()) {
            return nullptr;
        }
    }

    MemoryBlock* block = it->second;
    detach_free_block(block);

    constexpr size_t MIN_SPLIT_PAYLOAD = 32;
    if (block->size >= size + sizeof(MemoryBlock) + MIN_SPLIT_PAYLOAD) {
        size_t remainder_total = block->size - size;
        MemoryBlock* remainder = MemoryBlock::init(
            block->data() + size,
            remainder_total,
            AllocationStrategy::BEST_FIT
        );
        block->size = size;
        insert_free_block(remainder);
    }

    block->is_free = false;
    block->strategy = static_cast<uint8_t>(AllocationStrategy::BEST_FIT);

    AllocationStats::allocations++;
    AllocationStats::bytes_allocated += block->size;
    return block->data();
}

void* MemoryPool::allocate_from_pool(size_t size) {
    ScopedTiming timing(TimingCategory::PoolBased);
    auto it = size_index.lower_bound(size);
    if (it == size_index.end()) {
        add_new_chunk();
        it = size_index.lower_bound(size);
        if (it == size_index.end()) {
            return nullptr;
        }
    }

    MemoryBlock* block = it->second;
    detach_free_block(block);

    block->is_free = false;
    block->strategy = static_cast<uint8_t>(AllocationStrategy::POOL_BASED);

    AllocationStats::allocations++;
    AllocationStats::bytes_allocated += block->size;
    return block->data();
}

void* MemoryPool::allocate_segregated(size_t size) {
    ScopedTiming timing(TimingCategory::Segregated);
    const size_t class_index = select_segregated_class(size);
    if (class_index == SEGREGATED_CLASS_COUNT) {
        return allocate_best_fit(size);
    }

    MemoryBlock* block = nullptr;
#if MEMPOOL_SHARDED_SEGREGATED
    bool need_refill = false;
    {
        std::lock_guard<std::mutex> class_lock(segregated_mutexes[class_index]);
        if (!segregated_free_lists[class_index]) {
            need_refill = true;
        }
    }
    if (need_refill) {
        replenish_segregated_class(class_index);
    }
    {
        std::lock_guard<std::mutex> class_lock(segregated_mutexes[class_index]);
        block = segregated_free_lists[class_index];
        if (block) {
            segregated_free_lists[class_index] = block->next;
            block->next = nullptr;
            block->prev = nullptr;
            block->is_free = false;
            block->strategy = static_cast<uint8_t>(AllocationStrategy::SEGREGATED);
        }
    }
#else
    if (!segregated_free_lists[class_index]) {
        replenish_segregated_class(class_index);
    }

    block = segregated_free_lists[class_index];
    if (block) {
        segregated_free_lists[class_index] = block->next;
        block->next = nullptr;
        block->prev = nullptr;
        block->is_free = false;
        block->strategy = static_cast<uint8_t>(AllocationStrategy::SEGREGATED);
    }
#endif

    if (!block) {
        return allocate_best_fit(size);
    }

    AllocationStats::allocations++;
    AllocationStats::bytes_allocated += block->size;
    return block->data();
}

void* MemoryPool::allocate_segregated_fast(size_t size) {
#if MEMPOOL_SHARDED_SEGREGATED
    ScopedTiming timing(TimingCategory::Segregated);
    const size_t class_index = select_segregated_class(size);
    if (class_index == SEGREGATED_CLASS_COUNT) {
        // Let the caller fall back under the global lock.
        return nullptr;
    }

    std::lock_guard<std::mutex> class_lock(segregated_mutexes[class_index]);
    MemoryBlock* block = segregated_free_lists[class_index];
    if (!block) {
        // Let the caller fall back under the global lock.
        return nullptr;
    }

    segregated_free_lists[class_index] = block->next;
    block->next = nullptr;
    block->prev = nullptr;
    block->is_free = false;
    block->strategy = static_cast<uint8_t>(AllocationStrategy::SEGREGATED);

    AllocationStats::allocations++;
    AllocationStats::bytes_allocated += block->size;
    return block->data();
#else
    (void)size;
    return nullptr;
#endif
}

void MemoryPool::deallocate_block(MemoryBlock* block) {
    if (!block) return;
    
    size_t freed_size = block->size;
    insert_free_block(block);

    AllocationStats::deallocations++;
    if (AllocationStats::bytes_allocated >= freed_size) {
        AllocationStats::bytes_allocated -= freed_size;
    } else {
        AllocationStats::bytes_allocated = 0;
    }
}

void MemoryPool::insert_free_block(MemoryBlock* block, AllocationStrategy strat, bool allow_coalesce) {
    if (!block) return;

    block->is_free = true;
    block->strategy = static_cast<uint8_t>(strat);

    MemoryBlock* prev = nullptr;
    MemoryBlock* current = free_list;

    while (current && current < block) {
        prev = current;
        current = current->next;
    }

    block->prev = prev;
    block->next = current;
    if (current) {
        current->prev = block;
    }
    if (prev) {
        prev->next = block;
    } else {
        free_list = block;
    }

    add_to_size_index(block);

    if (allow_coalesce && strat == AllocationStrategy::BEST_FIT) {
        coalesce_neighbors(block);
    }
}

void MemoryPool::detach_free_block(MemoryBlock* block) {
    if (!block) return;

    remove_from_size_index(block);

    MemoryBlock* prev = block->prev;
    MemoryBlock* next = block->next;

    if (prev) {
        prev->next = next;
    } else if (free_list == block) {
        free_list = next;
    }

    if (next) {
        next->prev = prev;
    }

    block->prev = nullptr;
    block->next = nullptr;
    block->is_free = false;
}

void MemoryPool::coalesce_neighbors(MemoryBlock* block) {
    if (!block) return;

    if (static_cast<AllocationStrategy>(block->strategy) != AllocationStrategy::BEST_FIT) {
        return;
    }

    remove_from_size_index(block);

    MemoryBlock* current = block;

    MemoryBlock* next = current->next;
    if (next &&
        next->is_free &&
        static_cast<AllocationStrategy>(next->strategy) == AllocationStrategy::BEST_FIT &&
        reinterpret_cast<char*>(current) + sizeof(MemoryBlock) + current->size ==
            reinterpret_cast<char*>(next)) {
        remove_from_size_index(next);
        current->size += next->size + sizeof(MemoryBlock);
        current->next = next->next;
        if (next->next) {
            next->next->prev = current;
        }
    }

    MemoryBlock* prev = current->prev;
    if (prev &&
        prev->is_free &&
        static_cast<AllocationStrategy>(prev->strategy) == AllocationStrategy::BEST_FIT &&
        reinterpret_cast<char*>(prev) + sizeof(MemoryBlock) + prev->size ==
            reinterpret_cast<char*>(current)) {
        remove_from_size_index(prev);
        prev->size += current->size + sizeof(MemoryBlock);
        prev->next = current->next;
        if (current->next) {
            current->next->prev = prev;
        }
        current = prev;
    }

    add_to_size_index(current);
}

void MemoryPool::add_to_size_index(MemoryBlock* block) {
    if (!block) return;
    auto iter = size_index.emplace(block->size, block);
    size_lookup[block] = iter;
}

void MemoryPool::remove_from_size_index(MemoryBlock* block) {
    if (!block) return;

    auto lookup = size_lookup.find(block);
    if (lookup != size_lookup.end()) {
        size_index.erase(lookup->second);
        size_lookup.erase(lookup);
    }
}

MemoryBlock* MemoryPool::ptr_to_block(void* ptr) {
    return reinterpret_cast<MemoryBlock*>(reinterpret_cast<char*>(ptr) - sizeof(MemoryBlock));
}

bool MemoryPool::owns_ptr(void* ptr) {
    if (!ptr) {
        return false;
    }
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    for (const auto chunk : memory_chunks) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(chunk);
        if (addr >= base && addr < base + POOL_SIZE) {
            return true;
        }
    }

    if (small_allocator.owns(ptr)) {
        return true;
    }
    if (medium_allocator.owns(ptr)) {
        return true;
    }
    if (large_allocator.owns(ptr)) {
        return true;
    }

    return false;
}

void MemoryPool::release_thread_cache() {
    ThreadLocalCache& cache = thread_cache;

    for (void* ptr : cache.small_blocks) {
        small_allocator.deallocate_raw(ptr);
    }
    cache.small_blocks.clear();

    for (void* ptr : cache.medium_blocks) {
        medium_allocator.deallocate_raw(ptr);
    }
    cache.medium_blocks.clear();

    for (void* ptr : cache.large_blocks) {
        large_allocator.deallocate_raw(ptr);
    }
    cache.large_blocks.clear();
}

void* MemoryPool::acquire_small_from_cache(ThreadLocalCache& cache) {
    if (cache.small_blocks.empty()) {
        refill_small_cache(cache);
        if (cache.small_blocks.empty()) {
            return small_allocator.allocate();
        }
    }
    if (!cache.small_blocks.empty()) {
        void* ptr = cache.small_blocks.back();
        cache.small_blocks.pop_back();
        MemoryBlock* block = ptr_to_block(ptr);
        block->is_free = false;
        block->next = nullptr;
        block->prev = nullptr;
        block->strategy = static_cast<uint8_t>(AllocationStrategy::FIXED_SIZE);
        AllocationStats::allocations++;
        AllocationStats::bytes_allocated += block->size;
        return ptr;
    }
    return nullptr;
}

void* MemoryPool::acquire_medium_from_cache(ThreadLocalCache& cache) {
    if (cache.medium_blocks.empty()) {
        refill_medium_cache(cache);
        if (cache.medium_blocks.empty()) {
            return medium_allocator.allocate();
        }
    }
    if (!cache.medium_blocks.empty()) {
        void* ptr = cache.medium_blocks.back();
        cache.medium_blocks.pop_back();
        MemoryBlock* block = ptr_to_block(ptr);
        block->is_free = false;
        block->next = nullptr;
        block->prev = nullptr;
        block->strategy = static_cast<uint8_t>(AllocationStrategy::FIXED_SIZE);
        AllocationStats::allocations++;
        AllocationStats::bytes_allocated += block->size;
        return ptr;
    }
    return nullptr;
}

void* MemoryPool::acquire_large_from_cache(ThreadLocalCache& cache) {
    if (cache.large_blocks.empty()) {
        refill_large_cache(cache);
        if (cache.large_blocks.empty()) {
            return large_allocator.allocate();
        }
    }
    if (!cache.large_blocks.empty()) {
        void* ptr = cache.large_blocks.back();
        cache.large_blocks.pop_back();
        MemoryBlock* block = ptr_to_block(ptr);
        block->is_free = false;
        block->next = nullptr;
        block->prev = nullptr;
        block->strategy = static_cast<uint8_t>(AllocationStrategy::FIXED_SIZE);
        AllocationStats::allocations++;
        AllocationStats::bytes_allocated += block->size;
        return ptr;
    }
    return nullptr;
}

bool MemoryPool::release_small_to_cache(ThreadLocalCache& cache, void* ptr) {
    if (cache.small_blocks.size() < SMALL_CACHE_LIMIT) {
        MemoryBlock* block = ptr_to_block(ptr);
        block->is_free = true;
        block->next = nullptr;
        block->prev = nullptr;
        cache.small_blocks.push_back(ptr);
        AllocationStats::deallocations++;
        if (AllocationStats::bytes_allocated >= block->size) {
            AllocationStats::bytes_allocated -= block->size;
        } else {
            AllocationStats::bytes_allocated = 0;
        }
        return true;
    }
    small_allocator.deallocate(ptr);
    return true;
}

bool MemoryPool::release_medium_to_cache(ThreadLocalCache& cache, void* ptr) {
    if (cache.medium_blocks.size() < MEDIUM_CACHE_LIMIT) {
        MemoryBlock* block = ptr_to_block(ptr);
        block->is_free = true;
        block->next = nullptr;
        block->prev = nullptr;
        cache.medium_blocks.push_back(ptr);
        AllocationStats::deallocations++;
        if (AllocationStats::bytes_allocated >= block->size) {
            AllocationStats::bytes_allocated -= block->size;
        } else {
            AllocationStats::bytes_allocated = 0;
        }
        return true;
    }
    medium_allocator.deallocate(ptr);
    return true;
}

bool MemoryPool::release_large_to_cache(ThreadLocalCache& cache, void* ptr) {
    if (cache.large_blocks.size() < LARGE_CACHE_LIMIT) {
        MemoryBlock* block = ptr_to_block(ptr);
        block->is_free = true;
        block->next = nullptr;
        block->prev = nullptr;
        cache.large_blocks.push_back(ptr);
        AllocationStats::deallocations++;
        if (AllocationStats::bytes_allocated >= block->size) {
            AllocationStats::bytes_allocated -= block->size;
        } else {
            AllocationStats::bytes_allocated = 0;
        }
        return true;
    }
    large_allocator.deallocate(ptr);
    return true;
}

void MemoryPool::refill_small_cache(ThreadLocalCache& cache) {
    constexpr size_t REFILL_BATCH = 64;
    for (size_t i = 0; i < REFILL_BATCH && cache.small_blocks.size() < SMALL_CACHE_LIMIT; ++i) {
        void* ptr = small_allocator.allocate_raw();
        if (!ptr) {
            break;
        }
        MemoryBlock* block = ptr_to_block(ptr);
        if (block) {
            block->is_free = true;
            block->next = nullptr;
            block->prev = nullptr;
        }
        cache.small_blocks.push_back(ptr);
    }
}

void MemoryPool::refill_medium_cache(ThreadLocalCache& cache) {
    constexpr size_t REFILL_BATCH = 32;
    for (size_t i = 0; i < REFILL_BATCH && cache.medium_blocks.size() < MEDIUM_CACHE_LIMIT; ++i) {
        void* ptr = medium_allocator.allocate_raw();
        if (!ptr) {
            break;
        }
        MemoryBlock* block = ptr_to_block(ptr);
        if (block) {
            block->is_free = true;
            block->next = nullptr;
            block->prev = nullptr;
        }
        cache.medium_blocks.push_back(ptr);
    }
}

void MemoryPool::refill_large_cache(ThreadLocalCache& cache) {
    constexpr size_t REFILL_BATCH = 32;
    for (size_t i = 0; i < REFILL_BATCH && cache.large_blocks.size() < LARGE_CACHE_LIMIT; ++i) {
        void* ptr = large_allocator.allocate_raw();
        if (!ptr) {
            break;
        }
        MemoryBlock* block = ptr_to_block(ptr);
        if (block) {
            block->is_free = true;
            block->next = nullptr;
            block->prev = nullptr;
        }
        cache.large_blocks.push_back(ptr);
    }
}

size_t MemoryPool::select_segregated_class(size_t size) {
    for (size_t index = 0; index < SEGREGATED_CLASS_COUNT; ++index) {
        if (SEGREGATED_CLASS_SIZES[index] >= size) {
            return index;
        }
    }
    return SEGREGATED_CLASS_COUNT;
}

void MemoryPool::replenish_segregated_class(size_t class_index) {
    if (class_index >= SEGREGATED_CLASS_COUNT) {
        return;
    }

    ScopedTiming timing(TimingCategory::SegregatedRefill);

    add_new_chunk();
    MemoryBlock* chunk_block = reinterpret_cast<MemoryBlock*>(memory_chunks.back());
    detach_free_block(chunk_block);

    const size_t class_size = SEGREGATED_CLASS_SIZES[class_index];
    if (chunk_block->size > std::numeric_limits<size_t>::max() - sizeof(MemoryBlock)) {
        return;
    }
    size_t total_bytes = chunk_block->size + sizeof(MemoryBlock);
    char* cursor = reinterpret_cast<char*>(chunk_block);

    MemoryBlock* head = nullptr;
    MemoryBlock* tail = nullptr;

    while (total_bytes >= class_size + sizeof(MemoryBlock)) {
        MemoryBlock* new_block = MemoryBlock::init(
            cursor,
            class_size + sizeof(MemoryBlock),
            AllocationStrategy::SEGREGATED
        );
        new_block->next = head;
        new_block->prev = nullptr;
        head = new_block;
        if (!tail) {
            tail = new_block;
        }
        cursor += class_size + sizeof(MemoryBlock);
        total_bytes -= class_size + sizeof(MemoryBlock);
    }

    if (head) {
        #if MEMPOOL_SHARDED_SEGREGATED
        {
            std::lock_guard<std::mutex> class_lock(segregated_mutexes[class_index]);
            tail->next = segregated_free_lists[class_index];
            segregated_free_lists[class_index] = head;
        }
        #else
        tail->next = segregated_free_lists[class_index];
        segregated_free_lists[class_index] = head;
        #endif
    }

    if (total_bytes > sizeof(MemoryBlock)) {
        MemoryBlock* remainder = MemoryBlock::init(cursor, total_bytes);
        insert_free_block(remainder);
    }
}

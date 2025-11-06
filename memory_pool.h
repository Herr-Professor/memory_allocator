// memory/allocator.h
#ifndef MEMORY_ALLOCATOR_H
#define MEMORY_ALLOCATOR_H

#include <cstddef>
#include <thread>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cassert>
#include <new>
#include <atomic>
#include <iostream>
#include <type_traits>
#include <limits>
#include <utility>
#include <map>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <chrono>

#ifndef ENABLE_ALLOC_TIMING
#define ENABLE_ALLOC_TIMING 0
#endif

// Statistics tracking for memory allocations
struct AllocationStats {
    static thread_local size_t allocations;
    static thread_local size_t deallocations;
    static thread_local size_t bytes_allocated;
    static thread_local size_t last_reported_bytes;
    static std::atomic<size_t> total_allocations;
    static std::atomic<size_t> total_deallocations;
    static std::atomic<size_t> total_bytes;
    
    static void merge_thread_stats() {
        total_allocations.fetch_add(allocations, std::memory_order_relaxed);
        total_deallocations.fetch_add(deallocations, std::memory_order_relaxed);
        
        size_t current = bytes_allocated;
        size_t previous = last_reported_bytes;
        if (current >= previous) {
            total_bytes.fetch_add(current - previous, std::memory_order_relaxed);
        } else {
            total_bytes.fetch_sub(previous - current, std::memory_order_relaxed);
        }
        last_reported_bytes = current;
        allocations = 0;
        deallocations = 0;
    }
    
    static void print_stats() {
        merge_thread_stats();
        std::cout << "Total allocations: " << total_allocations.load(std::memory_order_relaxed)
                  << "\nTotal deallocations: " << total_deallocations.load(std::memory_order_relaxed)
                  << "\nOutstanding bytes: " << total_bytes.load(std::memory_order_relaxed) << std::endl;
    }
};

enum class TimingCategory {
    OverallAllocate,
    BestFit,
    PoolBased,
    Segregated,
    SegregatedRefill,
    Fixed32,
    Fixed128,
    Deallocate
};

#if ENABLE_ALLOC_TIMING
struct AllocationTimingStats {
    static thread_local uint64_t overall_ns;
    static thread_local uint64_t best_fit_ns;
    static thread_local uint64_t pool_ns;
    static thread_local uint64_t segregated_ns;
    static thread_local uint64_t segregated_refill_ns;
    static thread_local uint64_t fixed32_ns;
    static thread_local uint64_t fixed128_ns;
    static thread_local uint64_t dealloc_ns;

    static thread_local size_t overall_count;
    static thread_local size_t best_fit_count;
    static thread_local size_t pool_count;
    static thread_local size_t segregated_count;
    static thread_local size_t segregated_refill_count;
    static thread_local size_t fixed32_count;
    static thread_local size_t fixed128_count;
    static thread_local size_t dealloc_count;

    static std::atomic<uint64_t> total_overall_ns;
    static std::atomic<uint64_t> total_best_fit_ns;
    static std::atomic<uint64_t> total_pool_ns;
    static std::atomic<uint64_t> total_segregated_ns;
    static std::atomic<uint64_t> total_segregated_refill_ns;
    static std::atomic<uint64_t> total_fixed32_ns;
    static std::atomic<uint64_t> total_fixed128_ns;
    static std::atomic<uint64_t> total_dealloc_ns;

    static std::atomic<size_t> total_overall_count;
    static std::atomic<size_t> total_best_fit_count;
    static std::atomic<size_t> total_pool_count;
    static std::atomic<size_t> total_segregated_count;
    static std::atomic<size_t> total_segregated_refill_count;
    static std::atomic<size_t> total_fixed32_count;
    static std::atomic<size_t> total_fixed128_count;
    static std::atomic<size_t> total_dealloc_count;

    static void record(TimingCategory category, uint64_t ns);
    static void merge_thread_stats();
    static void print_stats();
};

class ScopedTiming {
public:
    explicit ScopedTiming(TimingCategory category)
        : category(category),
          start(std::chrono::steady_clock::now()),
          active(true) {}

    ~ScopedTiming() {
        if (active) {
            auto end = std::chrono::steady_clock::now();
            uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            AllocationTimingStats::record(category, ns);
        }
    }

    ScopedTiming(const ScopedTiming&) = delete;
    ScopedTiming& operator=(const ScopedTiming&) = delete;

    void cancel() { active = false; }

private:
    TimingCategory category;
    std::chrono::steady_clock::time_point start;
    bool active;
};
#else
struct AllocationTimingStats {
    static void record(TimingCategory, uint64_t) {}
    static void merge_thread_stats() {}
    static void print_stats() {}
};

class ScopedTiming {
public:
    explicit ScopedTiming(TimingCategory) {}
    void cancel() {}
};
#endif

// Allocation strategies
enum class AllocationStrategy {
    BEST_FIT,       // Best-fit allocation for variable size blocks
    FIXED_SIZE,     // Fixed-size blocks for uniform allocations
    POOL_BASED,     // Pool of fixed-size blocks with fast allocation
    SEGREGATED      // Segregated free lists for different size classes
};

// Memory block header for managing allocations
class MemoryBlock {
    static constexpr size_t ALIGNMENT = 16;  // AVX2 alignment requirement
    
public:
    MemoryBlock* next;
    MemoryBlock* prev;
    size_t size;
    bool is_free;
    uint8_t strategy;  // Store the allocation strategy used
    
    char* data() { return reinterpret_cast<char*>(this + 1); }
    
    static MemoryBlock* init(void* ptr, size_t size, AllocationStrategy strat = AllocationStrategy::BEST_FIT) {
        MemoryBlock* block = reinterpret_cast<MemoryBlock*>(ptr);
        block->next = nullptr;
        block->prev = nullptr;
        block->size = size - sizeof(MemoryBlock);
        block->is_free = true;
        block->strategy = static_cast<uint8_t>(strat);
        return block;
    }
    
    static size_t align_size(size_t size) {
        return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }
};

// Fixed-size block allocator for small objects
template<size_t BlockSize>
class FixedSizeAllocator {
    static constexpr size_t BLOCK_SIZE = BlockSize;
    std::vector<char*> memory_chunks;
    MemoryBlock* free_list;
    std::mutex mutex;

public:
    FixedSizeAllocator() : free_list(nullptr) {
        add_new_chunk();
    }

    void* allocate() {
        const TimingCategory category = (BlockSize <= 32)
            ? TimingCategory::Fixed32
            : TimingCategory::Fixed128;
        ScopedTiming timing(category);
        std::lock_guard<std::mutex> lock(mutex);
        if (!free_list) {
            add_new_chunk();
        }
        
        MemoryBlock* block = free_list;
        free_list = free_list->next;
        block->is_free = false;
        block->strategy = static_cast<uint8_t>(AllocationStrategy::FIXED_SIZE);
        
        AllocationStats::allocations++;
        AllocationStats::bytes_allocated += block->size;
        
        return block->data();
    }

    void deallocate(void* ptr) {
        if (!ptr) return;
        const TimingCategory category = (BlockSize <= 32)
            ? TimingCategory::Fixed32
            : TimingCategory::Fixed128;
        ScopedTiming timing(category);
        
        std::lock_guard<std::mutex> lock(mutex);
        MemoryBlock* block = reinterpret_cast<MemoryBlock*>(
            reinterpret_cast<char*>(ptr) - sizeof(MemoryBlock)
        );
        
        block->is_free = true;
        block->next = free_list;
        free_list = block;
        
        AllocationStats::deallocations++;
        if (AllocationStats::bytes_allocated >= block->size) {
            AllocationStats::bytes_allocated -= block->size;
        } else {
            AllocationStats::bytes_allocated = 0;
        }
    }

private:
    void add_new_chunk() {
        static constexpr size_t CHUNK_SIZE = 64 * 1024;  // 64KB chunks
        char* chunk = new char[CHUNK_SIZE];
        memory_chunks.push_back(chunk);
        
        size_t num_blocks = CHUNK_SIZE / (BLOCK_SIZE + sizeof(MemoryBlock));
        for (size_t i = 0; i < num_blocks; ++i) {
            char* block_ptr = chunk + i * (BLOCK_SIZE + sizeof(MemoryBlock));
            MemoryBlock* block = MemoryBlock::init(
                block_ptr, 
                BLOCK_SIZE + sizeof(MemoryBlock), 
                AllocationStrategy::FIXED_SIZE
            );
            block->next = free_list;
            free_list = block;
        }
    }
};

// Main memory pool with support for different allocation strategies
class MemoryPool {
    static constexpr size_t POOL_SIZE = 1024 * 1024;  // 1MB per pool
    std::vector<char*> memory_chunks;
    MemoryBlock* free_list;
    std::mutex mutex;
    std::atomic<size_t> active_scope_count{0};
    const bool thread_safe;
    
    // Fixed-size allocators for common sizes
    FixedSizeAllocator<32> small_allocator;
    FixedSizeAllocator<128> medium_allocator;
    FixedSizeAllocator<256> large_allocator;
    
    // Scope tracking
    struct ScopeEntry {
        size_t scope_index;
        size_t position;
    };
    std::vector<std::vector<void*>> scope_stack;
    std::unordered_map<void*, ScopeEntry> scope_lookup;
    std::multimap<size_t, MemoryBlock*> size_index;
    std::unordered_map<MemoryBlock*, std::multimap<size_t, MemoryBlock*>::iterator> size_lookup;
    
    inline static constexpr std::array<size_t, 8> SEGREGATED_CLASS_SIZES{{32, 64, 128, 256, 512, 1024, 2048, 4096}};
    inline static constexpr size_t SEGREGATED_CLASS_COUNT = SEGREGATED_CLASS_SIZES.size();
    std::array<MemoryBlock*, SEGREGATED_CLASS_COUNT> segregated_free_lists{};

public:
    explicit MemoryPool(bool thread_safe = true)
        : free_list(nullptr), thread_safe(thread_safe) {
        segregated_free_lists.fill(nullptr);
        add_new_chunk();
    }
    
    ~MemoryPool() {
        for (auto chunk : memory_chunks) {
            delete[] chunk;
        }
    }

    // Begin a new allocation scope
    void begin_scope() {
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if (thread_safe) lock.lock();
        scope_stack.emplace_back();
        active_scope_count.fetch_add(1, std::memory_order_relaxed);
    }

    // End current scope and free all allocations made in it
    void end_scope() {
        std::vector<void*> scoped_allocations;
        {
            std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
            if (thread_safe) lock.lock();
            if (scope_stack.empty()) {
                if (active_scope_count.load(std::memory_order_relaxed) > 0) {
                    active_scope_count.fetch_sub(1, std::memory_order_relaxed);
                }
                return;
            }
            scoped_allocations = std::move(scope_stack.back());
            for (void* ptr : scoped_allocations) {
                scope_lookup.erase(ptr);
            }
            scope_stack.pop_back();
            active_scope_count.fetch_sub(1, std::memory_order_relaxed);
        }
        
        for (void* ptr : scoped_allocations) {
            deallocate(ptr);
        }
    }
    
    void* allocate(size_t size, AllocationStrategy strategy = AllocationStrategy::BEST_FIT) {
        size_t aligned_size = MemoryBlock::align_size(size);
        ScopedTiming overall_timer(TimingCategory::OverallAllocate);

        AllocationStrategy effective_strategy = strategy;
        if (strategy == AllocationStrategy::BEST_FIT) {
            if (aligned_size <= 32) {
                effective_strategy = AllocationStrategy::FIXED_SIZE;
            } else if (aligned_size <= 128) {
                effective_strategy = AllocationStrategy::FIXED_SIZE;
            } else if (aligned_size <= 256) {
                effective_strategy = AllocationStrategy::FIXED_SIZE;
            } else if (aligned_size <= 512) {
                effective_strategy = AllocationStrategy::SEGREGATED;
            }
        }

        // Use fixed-size allocators for small allocations first
        if (effective_strategy == AllocationStrategy::FIXED_SIZE) {
            ThreadLocalCache& cache = thread_cache;
            void* fixed_result = nullptr;
            if (aligned_size <= 32) {
                fixed_result = acquire_small_from_cache(cache);
            } else if (aligned_size <= 128) {
                fixed_result = acquire_medium_from_cache(cache);
            } else if (aligned_size <= 256) {
                fixed_result = acquire_large_from_cache(cache);
            }
            
            if (fixed_result) {
                if (thread_safe && active_scope_count.load(std::memory_order_relaxed) > 0) {
                    std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
                    lock.lock();
                    if (!scope_stack.empty()) {
                        auto& scope = scope_stack.back();
                        const size_t position = scope.size();
                        scope.push_back(fixed_result);
                        scope_lookup[fixed_result] = {scope_stack.size() - 1, position};
                    }
                }
                return fixed_result;
            }
        }
        
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if (thread_safe) lock.lock();
        
        void* result = nullptr;
        switch (effective_strategy) {
            case AllocationStrategy::BEST_FIT:
                result = allocate_best_fit(aligned_size);
                break;
            case AllocationStrategy::POOL_BASED:
                result = allocate_from_pool(aligned_size);
                break;
            case AllocationStrategy::SEGREGATED:
                result = allocate_segregated(aligned_size);
                break;
            default:
                result = allocate_best_fit(aligned_size);
        }
        
        if (result && !scope_stack.empty()) {
            auto& scope = scope_stack.back();
            const size_t position = scope.size();
            scope.push_back(result);
            scope_lookup[result] = {scope_stack.size() - 1, position};
        }
        
        return result;
    }
    
    void deallocate(void* ptr) {
        if (!ptr) return;
        
        ScopedTiming timing(TimingCategory::Deallocate);
        MemoryBlock* block = ptr_to_block(ptr);

        bool need_scope_handling = thread_safe && active_scope_count.load(std::memory_order_relaxed) > 0;
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if (need_scope_handling) {
            lock.lock();

            auto scope_it = scope_lookup.find(ptr);
            if (scope_it != scope_lookup.end()) {
                const size_t scope_index = scope_it->second.scope_index;
                const size_t position = scope_it->second.position;
                if (scope_index < scope_stack.size()) {
                    auto& scope = scope_stack[scope_index];
                    if (position < scope.size()) {
                        void* tail = scope.back();
                        scope[position] = tail;
                        scope.pop_back();
                        if (tail != ptr) {
                            scope_lookup[tail].position = position;
                        }
                    }
                }
                scope_lookup.erase(scope_it);
            }
        }

        AllocationStrategy block_strategy = static_cast<AllocationStrategy>(block->strategy);
        if (block_strategy == AllocationStrategy::FIXED_SIZE) {
            if (lock.owns_lock()) {
                lock.unlock();
            }
            ThreadLocalCache& cache = thread_cache;
            bool handled = false;
            if (block->size <= 32) {
                handled = release_small_to_cache(cache, ptr);
                if (!handled) {
                    small_allocator.deallocate(ptr);
                }
            } else if (block->size <= 128) {
                handled = release_medium_to_cache(cache, ptr);
                if (!handled) {
                    medium_allocator.deallocate(ptr);
                }
            } else if (block->size <= 256) {
                handled = release_large_to_cache(cache, ptr);
                if (!handled) {
                    large_allocator.deallocate(ptr);
                }
            } else {
                if (!lock.owns_lock()) {
                    lock.lock();
                }
                deallocate_block(block);
            }
            return;
        }

        if (!lock.owns_lock()) {
            lock.lock();
        }
        switch (static_cast<AllocationStrategy>(block->strategy)) {
            case AllocationStrategy::FIXED_SIZE:
                break;
            case AllocationStrategy::SEGREGATED:
                {
                    const size_t class_index = select_segregated_class(block->size);
                    block->is_free = true;
                    block->prev = nullptr;
                    block->next = nullptr;
                    if (class_index < SEGREGATED_CLASS_COUNT) {
                        block->strategy = static_cast<uint8_t>(AllocationStrategy::SEGREGATED);
                        block->next = segregated_free_lists[class_index];
                        segregated_free_lists[class_index] = block;
                    } else {
                        insert_free_block(block);
                    }
                }
                AllocationStats::deallocations++;
                if (AllocationStats::bytes_allocated >= block->size) {
                    AllocationStats::bytes_allocated -= block->size;
                } else {
                    AllocationStats::bytes_allocated = 0;
                }
                break;
            default:
                deallocate_block(block);
        }
    }
    
    // Reset the entire pool
    void reset() {
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if (thread_safe) lock.lock();
        free_list = nullptr;
        size_index.clear();
        size_lookup.clear();
        for (auto chunk : memory_chunks) {
            MemoryBlock* block = MemoryBlock::init(chunk, POOL_SIZE);
            insert_free_block(block);
        }
        scope_stack.clear();
        scope_lookup.clear();
        segregated_free_lists.fill(nullptr);
        active_scope_count.store(0, std::memory_order_relaxed);
        thread_cache.small_blocks.clear();
        thread_cache.medium_blocks.clear();
        thread_cache.large_blocks.clear();
    }

private:
    void add_new_chunk() {
        char* chunk = new char[POOL_SIZE];
        memory_chunks.push_back(chunk);
        
        MemoryBlock* new_block = MemoryBlock::init(chunk, POOL_SIZE);
        insert_free_block(new_block);
    }
    
    void* allocate_best_fit(size_t size);    // Implementation from original code
    void* allocate_from_pool(size_t size);   // Pool-based allocation
    void* allocate_segregated(size_t size);  // Segregated lists allocation
    void deallocate_block(MemoryBlock* block); // Common deallocation logic
    void insert_free_block(MemoryBlock* block, AllocationStrategy strat = AllocationStrategy::BEST_FIT, bool allow_coalesce = true);
    void detach_free_block(MemoryBlock* block);
    void coalesce_neighbors(MemoryBlock* block);
    void add_to_size_index(MemoryBlock* block);
    void remove_from_size_index(MemoryBlock* block);
    static size_t select_segregated_class(size_t size);
    void replenish_segregated_class(size_t class_index);
    
    struct ThreadLocalCache {
        std::vector<void*> small_blocks;
        std::vector<void*> medium_blocks;
        std::vector<void*> large_blocks;
    };

    static thread_local ThreadLocalCache thread_cache;
    static constexpr size_t SMALL_CACHE_LIMIT = 256;
    static constexpr size_t MEDIUM_CACHE_LIMIT = 256;
    static constexpr size_t LARGE_CACHE_LIMIT = 256;

    static MemoryBlock* ptr_to_block(void* ptr);
    void* acquire_small_from_cache(ThreadLocalCache& cache);
    void* acquire_medium_from_cache(ThreadLocalCache& cache);
    void* acquire_large_from_cache(ThreadLocalCache& cache);
    bool release_small_to_cache(ThreadLocalCache& cache, void* ptr);
    bool release_medium_to_cache(ThreadLocalCache& cache, void* ptr);
    bool release_large_to_cache(ThreadLocalCache& cache, void* ptr);
    void refill_small_cache(ThreadLocalCache& cache);
    void refill_medium_cache(ThreadLocalCache& cache);
    void refill_large_cache(ThreadLocalCache& cache);
};

// Thread-local storage for memory pools
extern thread_local MemoryPool global_pool;

// Custom allocator template for STL containers
template<typename T>
class CustomAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;
    
    template<typename U>
    struct rebind {
        using other = CustomAllocator<U>;
    };
    
    // Make all CustomAllocator instantiations friends
    template<typename U>
    friend class CustomAllocator;
    
    CustomAllocator(MemoryPool& p = global_pool) : pool(p) {}
    template<typename U> CustomAllocator(const CustomAllocator<U>& other) : pool(other.pool) {}
        
    T* allocate(size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }
        
        AllocationStrategy strategy = 
            std::is_trivially_destructible<T>::value && sizeof(T) <= 256 
            ? AllocationStrategy::FIXED_SIZE 
            : AllocationStrategy::BEST_FIT;
            
        if (auto p = static_cast<T*>(pool.allocate(n * sizeof(T), strategy))) {
            return p;
        }
        
        throw std::bad_alloc();
    }
    
    void deallocate(T* p, size_t) noexcept {
        pool.deallocate(p);
    }

    // Add construct method
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }

    // Add destroy method
    template<typename U>
    void destroy(U* p) {
        p->~U();
    }

    // Add equality operators
    template<typename U>
    bool operator==(const CustomAllocator<U>& other) const {
        return &pool == &other.pool;
    }

    template<typename U>
    bool operator!=(const CustomAllocator<U>& other) const {
        return !(*this == other);
    }

private:
    MemoryPool& pool;
};

#endif // MEMORY_ALLOCATOR_H

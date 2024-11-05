// memory/allocator.h
#ifndef MEMORY_ALLOCATOR_H
#define MEMORY_ALLOCATOR_H

#include <cstddef>
#include <thread>
#include <vector>
#include <mutex>
#include <cassert>
#include <new>
#include <atomic>
#include <iostream>
#include <type_traits>

// Statistics tracking for memory allocations
struct AllocationStats {
    static thread_local size_t allocations;
    static thread_local size_t deallocations;
    static thread_local size_t bytes_allocated;
    static std::atomic<size_t> total_allocations;
    static std::atomic<size_t> total_bytes;
    
    static void merge_thread_stats() {
        total_allocations += allocations;
        total_bytes += bytes_allocated;
    }
    
    static void print_stats() {
        std::cout << "Total allocations: " << total_allocations 
                  << "\nTotal bytes: " << total_bytes << std::endl;
    }
};

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
    size_t size;
    bool is_free;
    uint8_t strategy;  // Store the allocation strategy used
    
    char* data() { return reinterpret_cast<char*>(this + 1); }
    
    static MemoryBlock* init(void* ptr, size_t size, AllocationStrategy strat = AllocationStrategy::BEST_FIT) {
        MemoryBlock* block = reinterpret_cast<MemoryBlock*>(ptr);
        block->next = nullptr;
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
        std::lock_guard<std::mutex> lock(mutex);
        if (!free_list) {
            add_new_chunk();
        }
        
        MemoryBlock* block = free_list;
        free_list = free_list->next;
        block->is_free = false;
        
        AllocationStats::allocations++;
        AllocationStats::bytes_allocated += BLOCK_SIZE;
        
        return block->data();
    }

    void deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        MemoryBlock* block = reinterpret_cast<MemoryBlock*>(
            reinterpret_cast<char*>(ptr) - sizeof(MemoryBlock)
        );
        
        block->is_free = true;
        block->next = free_list;
        free_list = block;
        
        AllocationStats::deallocations++;
    }

private:
    void add_new_chunk() {
        static constexpr size_t CHUNK_SIZE = 64 * 1024;  // 64KB chunks
        char* chunk = new char[CHUNK_SIZE];
        memory_chunks.push_back(chunk);
        
        size_t num_blocks = CHUNK_SIZE / (BLOCK_SIZE + sizeof(MemoryBlock));
        for (size_t i = 0; i < num_blocks; ++i) {
            char* block_ptr = chunk + i * (BLOCK_SIZE + sizeof(MemoryBlock));
            MemoryBlock* block = MemoryBlock::init(block_ptr, BLOCK_SIZE, AllocationStrategy::FIXED_SIZE);
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
    
    // Fixed-size allocators for common sizes
    FixedSizeAllocator<32> small_allocator;
    FixedSizeAllocator<128> medium_allocator;
    
    // Scope tracking
    size_t current_scope_id{0};
    std::vector<std::pair<void*, size_t>> scope_allocations;

public:
    MemoryPool() : free_list(nullptr) {
        add_new_chunk();
    }
    
    ~MemoryPool() {
        for (auto chunk : memory_chunks) {
            delete[] chunk;
        }
    }

    // Begin a new allocation scope
    void begin_scope() {
        std::lock_guard<std::mutex> lock(mutex);
        current_scope_id++;
        scope_allocations.clear();
    }

    // End current scope and free all allocations made in it
    void end_scope() {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& alloc : scope_allocations) {
            deallocate(alloc.first);
        }
        scope_allocations.clear();
    }
    
    void* allocate(size_t size, AllocationStrategy strategy = AllocationStrategy::BEST_FIT) {
        // Use fixed-size allocators for small allocations
        if (strategy == AllocationStrategy::FIXED_SIZE) {
            if (size <= 32) return small_allocator.allocate();
            if (size <= 128) return medium_allocator.allocate();
        }
        
        size_t aligned_size = MemoryBlock::align_size(size);
        std::lock_guard<std::mutex> lock(mutex);
        
        void* result = nullptr;
        switch (strategy) {
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
        
        if (result) {
            scope_allocations.emplace_back(result, aligned_size);
        }
        
        return result;
    }
    
    void deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        MemoryBlock* block = reinterpret_cast<MemoryBlock*>(
            reinterpret_cast<char*>(ptr) - sizeof(MemoryBlock)
        );
        
        // Route to appropriate deallocator based on strategy
        switch (static_cast<AllocationStrategy>(block->strategy)) {
            case AllocationStrategy::FIXED_SIZE:
                if (block->size <= 32) small_allocator.deallocate(ptr);
                else if (block->size <= 128) medium_allocator.deallocate(ptr);
                break;
            default:
                deallocate_block(block);
        }
    }
    
    // Reset the entire pool
    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        free_list = nullptr;
        for (auto chunk : memory_chunks) {
            MemoryBlock* block = MemoryBlock::init(chunk, POOL_SIZE);
            block->next = free_list;
            free_list = block;
        }
        scope_allocations.clear();
        current_scope_id = 0;
    }

private:
    void add_new_chunk() {
        char* chunk = new char[POOL_SIZE];
        memory_chunks.push_back(chunk);
        
        MemoryBlock* new_block = MemoryBlock::init(chunk, POOL_SIZE);
        new_block->next = free_list;
        free_list = new_block;
    }
    
    void* allocate_best_fit(size_t size);    // Implementation from original code
    void* allocate_from_pool(size_t size);   // Pool-based allocation
    void* allocate_segregated(size_t size);  // Segregated lists allocation
    void deallocate_block(MemoryBlock* block); // Common deallocation logic
};

// Thread-local storage for memory pools
extern MemoryPool global_pool;

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
            std::is_trivially_destructible<T>::value && sizeof(T) <= 128 
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
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include "../memory_pool.h"

struct AllocationRecord {
    void* ptr;
    size_t size;
    bool is_allocated;
};

int main() {
    MemoryPool pool;
    std::vector<AllocationRecord> allocations;
    const int NUM_ALLOCATIONS = 1000;
    const int NUM_OPERATIONS = 5000;
    
    // Random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(16, 256);  // Random sizes between 16 and 256 bytes
    std::uniform_int_distribution<> op_dist(0, 1);       // 0 = deallocate, 1 = allocate
    std::uniform_int_distribution<> index_dist;          // Will be updated based on vector size
    
    // Initial allocations
    std::cout << "Performing initial allocations...\n";
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = size_dist(gen);
        void* ptr = pool.allocate(size);
        allocations.push_back({ptr, size, true});
        
        if (i % 100 == 0) {
            std::cout << "Allocated " << i << " blocks\n";
            AllocationStats::print_stats();
        }
    }
    
    // Random operations
    std::cout << "\nPerforming random operations...\n";
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        index_dist = std::uniform_int_distribution<>(0, allocations.size() - 1);
        int index = index_dist(gen);
        bool should_allocate = op_dist(gen) == 1 || !allocations[index].is_allocated;
        
        if (should_allocate && !allocations[index].is_allocated) {
            // Reallocate
            void* ptr = pool.allocate(allocations[index].size);
            allocations[index].ptr = ptr;
            allocations[index].is_allocated = true;
            std::cout << "R";  // Reallocated
        } else if (!should_allocate && allocations[index].is_allocated) {
            // Deallocate
            pool.deallocate(allocations[index].ptr);
            allocations[index].is_allocated = false;
            std::cout << "D";  // Deallocated
        } else {
            // New allocation
            size_t size = size_dist(gen);
            void* ptr = pool.allocate(size);
            allocations.push_back({ptr, size, true});
            std::cout << "A";  // Allocated
        }
        
        if (i % 50 == 49) std::cout << "\n";
    }
    
    // Cleanup
    std::cout << "\n\nCleaning up...\n";
    for (auto& record : allocations) {
        if (record.is_allocated) {
            pool.deallocate(record.ptr);
        }
    }
    
    std::cout << "Test complete!\n";
    AllocationStats::print_stats();
    return 0;
}

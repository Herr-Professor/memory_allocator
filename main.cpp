#include "memory_pool.h"
#include <iostream>
#include <vector>
#include <chrono>

void print_separator() {
    std::cout << "\n----------------------------------------\n";
}

int main() {
    // Initial stats
    std::cout << "Initial stats:";
    AllocationStats::print_stats();
    print_separator();

    // Test fixed-size allocator
    {
        std::cout << "Testing fixed-size allocator:\n";
        FixedSizeAllocator<32> small_allocator;
        void* p1 = small_allocator.allocate();
        void* p2 = small_allocator.allocate();
        small_allocator.deallocate(p1);
        small_allocator.deallocate(p2);
        AllocationStats::print_stats();
        print_separator();
    }

    // Test memory pool with different strategies
    {
        std::cout << "Testing memory pool with different strategies:\n";
        MemoryPool pool;
        
        // Test BEST_FIT
        std::cout << "\nBest Fit allocation:\n";
        global_pool.begin_scope();
        [[maybe_unused]] void* p1 = pool.allocate(64, AllocationStrategy::BEST_FIT);
        [[maybe_unused]] void* p2 = pool.allocate(128, AllocationStrategy::BEST_FIT);
        global_pool.end_scope();
        AllocationStats::print_stats();

        // Test POOL_BASED
        std::cout << "\nPool-based allocation:\n";
        global_pool.begin_scope();
        [[maybe_unused]] void* p3 = pool.allocate(32, AllocationStrategy::POOL_BASED);
        [[maybe_unused]] void* p4 = pool.allocate(32, AllocationStrategy::POOL_BASED);
        global_pool.end_scope();
        AllocationStats::print_stats();

        // Test SEGREGATED
        std::cout << "\nSegregated allocation:\n";
        global_pool.begin_scope();
        [[maybe_unused]] void* p5 = pool.allocate(256, AllocationStrategy::SEGREGATED);
        [[maybe_unused]] void* p6 = pool.allocate(512, AllocationStrategy::SEGREGATED);
        global_pool.end_scope();
        AllocationStats::print_stats();
        print_separator();
    }

    // Test custom allocator with std::vector
    {
        std::cout << "Testing custom allocator with std::vector:\n";
        std::vector<int, CustomAllocator<int>> vec;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 1000; i++) {
            vec.push_back(i);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "Vector operations completed in " << duration.count() << " microseconds\n";
        AllocationStats::print_stats();
        print_separator();
    }

    std::cout << "Final stats:\n";
    AllocationStats::print_stats();

    return 0;
}
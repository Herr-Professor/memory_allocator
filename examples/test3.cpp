#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include <malloc.h>
#include "../memory_pool.h"

struct AllocationRecord {
    void* ptr;
    size_t size;
    bool is_allocated;
    std::chrono::high_resolution_clock::time_point allocation_time;
};

struct PerformanceResults {
    long int total_time;
    size_t peak_memory;
    size_t allocations;
    size_t deallocations;
    double avg_time;
};

// Utility function to print memory usage bar
void print_memory_bar(size_t used, size_t total, int width = 50) {
    int used_bars = static_cast<int>((static_cast<double>(used) / total) * width);
    std::cout << "[";
    for (int i = 0; i < width; ++i) {
        std::cout << (i < used_bars ? "█" : "░");
    }
    std::cout << "] " << (used * 100.0 / total) << "%" << std::endl;
}

// Performance test function template
template<typename Allocator>
PerformanceResults run_performance_test(const std::string& allocator_name, Allocator& allocator) {
    const int NUM_ALLOCATIONS = 10000;
    const int NUM_OPERATIONS = 50000;
    std::vector<AllocationRecord> allocations;
    size_t total_memory_used = 0;
    size_t peak_memory_used = 0;
    
    // Random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(16, 256);
    std::uniform_int_distribution<> op_dist(0, 1);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Initial allocations
    std::cout << "\nPerforming " << NUM_ALLOCATIONS << " initial allocations for " << allocator_name << "...\n";
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = size_dist(gen);
        auto alloc_start = std::chrono::high_resolution_clock::now();
        void* ptr = allocator.allocate(size);
        allocations.push_back({ptr, size, true, alloc_start});
        
        total_memory_used += size;
        peak_memory_used = std::max(peak_memory_used, total_memory_used);
        
        if (i % 1000 == 0) {
            print_memory_bar(total_memory_used, peak_memory_used);
        }
    }
    
    // Random operations
    std::cout << "\nPerforming " << NUM_OPERATIONS << " random operations...\n";
    std::uniform_int_distribution<> index_dist(0, allocations.size() - 1);
    
    size_t allocations_count = 0;
    size_t deallocations_count = 0;
    
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        int index = index_dist(gen);
        bool should_allocate = op_dist(gen) == 1 || !allocations[index].is_allocated;
        
        if (should_allocate && !allocations[index].is_allocated) {
            // Reallocate
            auto alloc_start = std::chrono::high_resolution_clock::now();
            void* ptr = allocator.allocate(allocations[index].size);
            allocations[index].ptr = ptr;
            allocations[index].is_allocated = true;
            allocations[index].allocation_time = alloc_start;
            total_memory_used += allocations[index].size;
            allocations_count++;
            std::cout << "R";
        } else if (!should_allocate && allocations[index].is_allocated) {
            // Deallocate
            allocator.deallocate(allocations[index].ptr);
            allocations[index].is_allocated = false;
            total_memory_used -= allocations[index].size;
            deallocations_count++;
            std::cout << "D";
        }
        
        peak_memory_used = std::max(peak_memory_used, total_memory_used);
        
        if (i % 50 == 49) {
            std::cout << "\n";
            print_memory_bar(total_memory_used, peak_memory_used);
        }
    }
    
    // Cleanup
    for (auto& record : allocations) {
        if (record.is_allocated) {
            allocator.deallocate(record.ptr);
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Print statistics
    std::cout << "\nPerformance Results for " << allocator_name << ":\n";
    std::cout << "Total time: " << duration.count() << "ms\n";
    std::cout << "Peak memory usage: " << peak_memory_used << " bytes\n";
    std::cout << "Total allocations: " << allocations_count << "\n";
    std::cout << "Total deallocations: " << deallocations_count << "\n";
    std::cout << "Average operation time: " 
              << static_cast<double>(duration.count()) / (allocations_count + deallocations_count) 
              << "ms\n\n";
              
    return PerformanceResults{
        duration.count(),
        peak_memory_used,
        allocations_count,
        deallocations_count,
        static_cast<double>(duration.count()) / (allocations_count + deallocations_count)
    };
}

// Wrapper for standard allocator
struct StandardAllocator {
    void* allocate(size_t size) { return malloc(size); }
    void deallocate(void* ptr) { free(ptr); }
};

// Add this new function to print performance table
void print_performance_table(const std::string& name1, const std::string& name2,
                           long int time1, long int time2,
                           size_t peak_mem1, size_t peak_mem2,
                           size_t allocs1, size_t allocs2,
                           size_t deallocs1, size_t deallocs2,
                           double avg_time1, double avg_time2) {
    const int width = 20;
    std::cout << std::string(80, '-') << "\n";
    std::cout << std::setw(width) << "Metric" 
              << std::setw(width) << name1
              << std::setw(width) << name2 
              << std::setw(width) << "Improvement" << "\n";
    std::cout << std::string(80, '-') << "\n";
    
    // Total time comparison
    std::cout << std::setw(width) << "Total Time (ms)"
              << std::setw(width) << time1
              << std::setw(width) << time2
              << std::setw(width) << std::fixed << std::setprecision(2) 
              << ((time1 < time2) ? 
                  ((time2 - time1) * 100.0 / time2) : 
                  (-1 * (time1 - time2) * 100.0 / time2)) << "%" << "\n";
    
    // Peak memory comparison
    std::cout << std::setw(width) << "Peak Memory (bytes)"
              << std::setw(width) << peak_mem1
              << std::setw(width) << peak_mem2
              << std::setw(width) << std::fixed << std::setprecision(2)
              << (static_cast<double>(peak_mem2 - peak_mem1) / peak_mem2 * 100) << "%" << "\n";
    
    // Allocations comparison
    std::cout << std::setw(width) << "Allocations"
              << std::setw(width) << allocs1
              << std::setw(width) << allocs2
              << std::setw(width) << "N/A" << "\n";
    
    // Deallocations comparison
    std::cout << std::setw(width) << "Deallocations"
              << std::setw(width) << deallocs1
              << std::setw(width) << deallocs2
              << std::setw(width) << "N/A" << "\n";
    
    // Average operation time comparison
    std::cout << std::setw(width) << "Avg Time (ms)"
              << std::setw(width) << std::scientific << avg_time1
              << std::setw(width) << avg_time2
              << std::setw(width) << std::fixed << std::setprecision(2)
              << (static_cast<double>(avg_time2 - avg_time1) / avg_time2 * 100) << "%" << "\n";
    
    std::cout << std::string(80, '-') << "\n\n";
}

int main() {
    // Test MemoryPool
    MemoryPool memory_pool;
    auto pool_results = run_performance_test("MemoryPool", memory_pool);
    
    // Test standard allocator
    StandardAllocator std_allocator;
    auto std_results = run_performance_test("Standard Allocator", std_allocator);
    
    // Print comparison table
    std::cout << "\nPerformance Comparison:\n";
    print_performance_table(
        "Memory Pool", "Standard Malloc",
        pool_results.total_time, std_results.total_time,
        pool_results.peak_memory, std_results.peak_memory,
        pool_results.allocations, std_results.allocations,
        pool_results.deallocations, std_results.deallocations,
        pool_results.avg_time, std_results.avg_time
    );
    
    return 0;
} 
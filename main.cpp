#include <iostream>
#include "memory_pool.h"

int main() {
    MemoryPool pool;

    // Allocate a block
    void* block1 = pool.allocate(32);
    std::cout << "Allocated block1 at address: " << block1 << std::endl;

    // Allocate another block
    void* block2 = pool.allocate(32);
    std::cout << "Allocated block2 at address: " << block2 << std::endl;

    // Deallocate the first block
    pool.deallocate(block1);
    std::cout << "Deallocated block1" << std::endl;

    // Reallocate to see if memory is reused
    void* block3 = pool.allocate(32);
    std::cout << "Allocated block3 (should reuse block1) at address: " << block3 << std::endl;

    // Cleanup
    pool.deallocate(block2);
    pool.deallocate(block3);

    std::cout << "Test complete!" << std::endl;
    return 0;
}

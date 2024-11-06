#include "memory_pool.h"

// Initialize static members of AllocationStats
thread_local size_t AllocationStats::allocations = 0;
thread_local size_t AllocationStats::deallocations = 0;
thread_local size_t AllocationStats::bytes_allocated = 0;
std::atomic<size_t> AllocationStats::total_allocations{0};
std::atomic<size_t> AllocationStats::total_bytes{0};

MemoryPool global_pool;

void* MemoryPool::allocate_best_fit(size_t size) {
    MemoryBlock* best_fit = nullptr;
    MemoryBlock* best_fit_prev = nullptr;
    MemoryBlock* current = free_list;
    MemoryBlock* prev = nullptr;
    
    while (current) {
        if (current->is_free && current->size >= size && 
            (!best_fit || current->size < best_fit->size)) {
            best_fit = current;
            best_fit_prev = prev;
        }
        prev = current;
        current = current->next;
    }
    
    if (best_fit) {
        if (best_fit->size > size + sizeof(MemoryBlock)) {
            MemoryBlock* new_block = MemoryBlock::init(
                best_fit->data() + size, 
                best_fit->size - size - sizeof(MemoryBlock),
                AllocationStrategy::BEST_FIT
            );
            new_block->next = best_fit->next;
            best_fit->next = new_block;
            best_fit->size = size;
        }
        
        best_fit->is_free = false;
        if (best_fit_prev) {
            best_fit_prev->next = best_fit->next;
        } else {
            free_list = best_fit->next;
        }
        
        AllocationStats::allocations++;
        AllocationStats::bytes_allocated += best_fit->size;
        return best_fit->data();
    }
    
    add_new_chunk();
    return allocate_best_fit(size);
}

void* MemoryPool::allocate_from_pool(size_t size) {
    MemoryBlock* best_fit = nullptr;
    MemoryBlock* prev = nullptr;
    MemoryBlock* current = free_list;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            best_fit = current;
            prev = current;
            break;
        }
        current = current->next;
    }
    
    if (best_fit) {
        best_fit->is_free = false;
        if (prev) {
            prev->next = best_fit->next;
        } else {
            free_list = best_fit->next;
        }
        
        AllocationStats::allocations++;
        AllocationStats::bytes_allocated += best_fit->size;
        return best_fit->data();
    }
    
    add_new_chunk();
    return allocate_from_pool(size);
}

void* MemoryPool::allocate_segregated(size_t size) {
    // Define size classes (powers of 2 for simplicity)
    static const size_t SIZE_CLASSES[] = {
        32, 64, 128, 256, 512, 1024, 2048, 4096
    };
    
    // Find appropriate size class
    size_t class_size = SIZE_CLASSES[0];
    for (size_t s : SIZE_CLASSES) {
        if (s >= size) {
            class_size = s;
            break;
        }
    }
    
    // Try to find a block in the appropriate free list
    MemoryBlock* current = free_list;
    MemoryBlock* prev = nullptr;
    
    while (current) {
        if (current->is_free && current->size == class_size) {
            // Found a perfect fit
            current->is_free = false;
            
            // Remove from free list
            if (prev) {
                prev->next = current->next;
            } else {
                free_list = current->next;
            }
            
            AllocationStats::allocations++;
            AllocationStats::bytes_allocated += class_size;
            return current->data();
        }
        prev = current;
        current = current->next;
    }
    
    // No suitable block found, allocate new chunk
    add_new_chunk();
    
    // Split the new chunk into blocks of the appropriate size
    char* chunk = memory_chunks.back();
    size_t remaining = POOL_SIZE;
    
    while (remaining >= class_size + sizeof(MemoryBlock)) {
        MemoryBlock* new_block = MemoryBlock::init(
            chunk, 
            class_size,
            AllocationStrategy::SEGREGATED
        );
        
        // Add to free list
        new_block->next = free_list;
        free_list = new_block;
        
        chunk += class_size + sizeof(MemoryBlock);
        remaining -= class_size + sizeof(MemoryBlock);
    }
    
    // Try allocation again with the new blocks
    return allocate_segregated(size);
}

void MemoryPool::deallocate_block(MemoryBlock* block) {
    if (!block) return;
    
    // Mark block as free
    block->is_free = true;
    block->next = nullptr;  // Important: clear the next pointer
    
    // Handle empty free list case
    if (!free_list) {
        free_list = block;
        return;
    }
    
    // Find the correct position to insert the block
    MemoryBlock* current = free_list;
    MemoryBlock* prev = nullptr;
    
    // Find the correct position in address order
    while (current && current < block) {
        prev = current;
        current = current->next;
    }
    
    // Insert the block
    if (prev) {
        prev->next = block;
    } else {
        free_list = block;
    }
    block->next = current;
    
    // Coalesce with next block if possible
    if (current && 
        reinterpret_cast<char*>(block) + block->size + sizeof(MemoryBlock) == 
        reinterpret_cast<char*>(current) && 
        current->is_free) {
        block->size += current->size + sizeof(MemoryBlock);
        block->next = current->next;
    }
    
    // Coalesce with previous block if possible
    if (prev && 
        reinterpret_cast<char*>(prev) + prev->size + sizeof(MemoryBlock) == 
        reinterpret_cast<char*>(block) && 
        prev->is_free) {
        prev->size += block->size + sizeof(MemoryBlock);
        prev->next = block->next;
    }
    
    AllocationStats::deallocations++;
    AllocationStats::bytes_allocated -= block->size;
}
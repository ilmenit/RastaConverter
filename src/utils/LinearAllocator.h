#ifndef LINEAR_ALLOCATOR_H
#define LINEAR_ALLOCATOR_H

#include <cstddef>
#include <cstdint>
#include <malloc.h>

/**
 * Simple linear allocator for efficient memory allocation
 * 
 * Allocates memory in a linear fashion without individual free operations.
 * Memory is freed all at once when clear() is called.
 */
class linear_allocator
{
public:
    static const uint32_t BLOCK_SIZE = 8388608; // 8MB per block

    linear_allocator()
        : chunk_list(NULL)
        , alloc_left(0)
        , alloc_total(0)
    {
    }

    ~linear_allocator()
    {
        clear();
    }

    /**
     * Clear all allocated memory
     */
    void clear()
    {
        while(chunk_list)
        {
            chunk_node *next = chunk_list->next;
            free(chunk_list);
            chunk_list = next;
        }

        alloc_left = 0;
        alloc_total = 0;
    }

    /**
     * Get the total size of allocated memory
     * 
     * @return Size in bytes
     */
    size_t size() const { return alloc_total; }

    /**
     * Allocate an object of type T
     * 
     * @return Pointer to allocated object
     */
    template<class T>
    T *allocate()
    {
        return new(allocate(sizeof(T))) T;
    }

    /**
     * Allocate memory of given size
     * 
     * @param n Size in bytes to allocate
     * @return Pointer to allocated memory
     */
    void *allocate(size_t n)
    {
        // Align to 8-byte boundary
        n = (n + 7) & ~7;

        if (alloc_left < n)
        {
            size_t to_alloc = BLOCK_SIZE - 64 - sizeof(chunk_node);

            if (to_alloc < n)
                to_alloc = n;

            chunk_node *new_node = (chunk_node *)malloc(sizeof(chunk_node) + to_alloc);
            new_node->next = chunk_list;
            chunk_list = new_node;
            alloc_ptr = (char *)(new_node + 1);
            alloc_left = to_alloc;
            alloc_total += to_alloc + sizeof(chunk_node);
        }

        void *p = alloc_ptr;
        alloc_ptr += n;
        alloc_left -= n;
        return p;
    }

private:
    struct chunk_node
    {
        chunk_node *next;
        void *align_pad;
    };

    chunk_node *chunk_list;
    char *alloc_ptr;
    size_t alloc_left;
    size_t alloc_total;
};

#endif // LINEAR_ALLOCATOR_H

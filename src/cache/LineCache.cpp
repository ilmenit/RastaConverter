#include "LineCache.h"
#include <cstring>
#include <stdexcept>  // Added include for exception handling

line_cache::line_cache()
{
    clear();
}

void line_cache::clear()
{
    for (int i = 0; i < LINE_CACHE_HTABLE_SIZE; ++i)
    {
        hash_table[i].first = NULL;
        hash_table[i].offset = 0;
    }
}

const line_cache_result* line_cache::find(const line_cache_key& key, uint32_t hash) const
{
    const hash_chain& hc = hash_table[hash & (LINE_CACHE_HTABLE_SIZE - 1)];
    const hash_block* hb = hc.first;
    int hbidx = hc.offset;

    for (; hb; hb = hb->next)
    {
        for (int i = hbidx - 1; i >= 0; --i)
        {
            // Check for null pointers before dereferencing
            if (hb->nodes[i].value != nullptr && 
                hb->nodes[i].hash == hash && 
                key == hb->nodes[i].value->first)
            {
                return &hb->nodes[i].value->second;
            }
        }

        hbidx = hash_block::N;
    }

    return NULL;
}

line_cache_result& line_cache::insert(const line_cache_key& key, uint32_t hash, linear_allocator& alloc)
{
    hash_chain& hc = hash_table[hash & (LINE_CACHE_HTABLE_SIZE - 1)];
    hash_block* hb = hc.first;
    int hbidx = hc.offset;

    if (!hb || hbidx >= hash_block::N)
    {
        hash_block* hb2 = alloc.allocate<hash_block>();
        if (!hb2) {
            // Handle allocation failure - throw exception
            throw std::bad_alloc();
        }
        
        memset(hb2, 0, sizeof(*hb2));
        hb2->next = hb;
        hc.first = hb2;
        hb = hb2;
        hbidx = 0;
    }

    hc.offset = hbidx + 1;

    value_type* value = alloc.allocate<value_type>();
    if (!value) {
        // Handle allocation failure - throw exception
        throw std::bad_alloc();
    }
    
    value->first = key;

    hash_node node = { hash, value };
    hb->nodes[hbidx] = node;

    return value->second;
}
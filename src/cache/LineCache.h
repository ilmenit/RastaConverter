#ifndef LINECACHE_H
#define LINECACHE_H

#include <stdint.h>
#include <vector>
#include "../color/Distance.h"
#include "../raster/RegisterState.h"
#include "../utils/LinearAllocator.h"
#include "InsnSequenceCache.h"

struct line_cache_key
{
    register_state entry_state;
    const insn_sequence *insn_seq;

    uint32_t hash()
    {
        uint32_t hash = 0;
        
        hash += (size_t)entry_state.reg_a;
        hash += (size_t)entry_state.reg_x << 8;
        hash += (size_t)entry_state.reg_y << 16;

        for(int i=0; i<E_TARGET_MAX; ++i)
            hash += (size_t)entry_state.mem_regs[i] << (8*(i & 3));

        hash += insn_seq->hash;

        hash += (hash * 0x1a572cf3) >> 20;

        return hash;
    }
};

inline bool operator==(const line_cache_key& key1, const line_cache_key& key2)
{
    if (key1.insn_seq != key2.insn_seq) return false;
    if (key1.entry_state.reg_a != key2.entry_state.reg_a) return false;
    if (key1.entry_state.reg_x != key2.entry_state.reg_x) return false;
    if (key1.entry_state.reg_y != key2.entry_state.reg_y) return false;
    if (memcmp(key1.entry_state.mem_regs, key2.entry_state.mem_regs, sizeof key1.entry_state.mem_regs)) return false;

    return true;
}

struct line_cache_result
{
    distance_accum_t line_error;
    register_state new_state;
    unsigned char *color_row;
    unsigned char *target_row;
    unsigned char sprite_data[4][8];
};

class line_cache
{
public:
    typedef std::pair<line_cache_key, line_cache_result> value_type;

    struct hash_node
    {
        uint32_t hash;
        value_type *value;
    };

    struct hash_block
    {
        static const int N = 15;

        hash_node nodes[N];
        hash_block *next;
    };

    struct hash_chain
    {
        hash_block *first;
        int offset;
    };

    typedef std::vector<hash_node> hash_list;

    static const int HTSIZE = 8192;

    line_cache();

    /**
     * Clear all cached entries
     */
    void clear();

    /**
     * Find a cached line result
     * 
     * @param key Cache key to look up
     * @param hash Pre-computed hash value
     * @return Pointer to cached result or NULL if not found
     */
    const line_cache_result *find(const line_cache_key& key, uint32_t hash) const;

    /**
     * Insert a new cache entry
     * 
     * @param key Cache key to insert
     * @param hash Pre-computed hash value
     * @param alloc Linear allocator for memory allocation
     * @return Reference to the inserted cache result (to be filled by caller)
     */
    line_cache_result& insert(const line_cache_key& key, uint32_t hash, linear_allocator& alloc);

private:
    hash_chain hash_table[HTSIZE];
};

#endif // LINECACHE_H

#ifndef LINECACHE_H
#define LINECACHE_H

#include <stdint.h>
#include <vector>
#include <cstring>
#include "../color/Distance.h"
#include "../raster/RegisterState.h"
#include "../utils/LinearAllocator.h"
#include "InsnSequenceCache.h"

struct line_cache_key
{
    register_state entry_state;
    const insn_sequence* insn_seq;

    uint32_t hash()
    {
        // Mix register state
        uint32_t h = 0;

        h += (uint32_t)entry_state.reg_a;
        h += (uint32_t)entry_state.reg_x << 8;
        h += (uint32_t)entry_state.reg_y << 16;

        for (int i = 0; i < E_TARGET_MAX; ++i)
            h += (uint32_t)entry_state.mem_regs[i] << (8 * (i & 3));

        // WARNING: Never dereference insn_seq here. It may be a dangling pointer
        // if the underlying instruction-sequence cache was cleared by another thread
        // (dual-frame mode evaluates two programs with different caches). We only use
        // the pointer value to distinguish instruction sequences; equality also
        // relies on pointer identity.
        if (insn_seq != nullptr) {
            uintptr_t p = (uintptr_t)insn_seq;
            // Mix pointer bits (portable 32-bit fold)
            uint32_t lo = (uint32_t)(p & 0xFFFFFFFFu);
            uint32_t hi = (uint32_t)((p >> 32) & 0xFFFFFFFFu);
            h ^= (lo * 2654435761u) ^ (hi * 2246822519u);
        }

        // Final avalanche
        h ^= (h >> 17);
        h *= 0x85ebca6b;
        h ^= (h >> 13);
        h *= 0xc2b2ae35;
        h ^= (h >> 16);

        return h;
    }
};

inline bool operator==(const line_cache_key& key1, const line_cache_key& key2)
{
    // Check for null pointers first
    if (key1.insn_seq == nullptr && key2.insn_seq == nullptr) {
        // Both are null, consider equal based on registers only
    }
    else if (key1.insn_seq == nullptr || key2.insn_seq == nullptr) {
        // One is null but the other isn't, they can't be equal
        return false;
    }
    else if (key1.insn_seq != key2.insn_seq) {
        // Both non-null but different pointers
        return false;
    }

    // Check register state equality
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
    unsigned char* color_row;
    unsigned char* target_row;
    unsigned char sprite_data[4][8];
};

class line_cache
{
public:
    typedef std::pair<line_cache_key, line_cache_result> value_type;

    struct hash_node
    {
        uint32_t hash;
        value_type* value;
    };

    struct hash_block
    {
        static const int N = 15;

        hash_node nodes[N];
        hash_block* next;
    };

    struct hash_chain
    {
        hash_block* first;
        int offset;
    };

    typedef std::vector<hash_node> hash_list;

    // Use a unique name to avoid collision with Windows' HTSIZE macro (WM_NCHITTEST constants)
    static const int LINE_CACHE_HTABLE_SIZE = 8192;

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
    const line_cache_result* find(const line_cache_key& key, uint32_t hash) const;

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
    hash_chain hash_table[LINE_CACHE_HTABLE_SIZE];
};

#endif // LINECACHE_H
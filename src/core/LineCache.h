#ifndef LINECACHE_H
#define LINECACHE_H

#include <stdint.h>
#include <vector>
#include <cassert>

#include "RegisterState.h"
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

		// Robust: never dereference insn_seq here; if a stale pointer slips through across evaluators,
		// using its address for hashing avoids crashes while equality still guards correctness.
		uintptr_t pval = (uintptr_t)insn_seq;
		hash += (uint32_t)(pval ^ (pval >> 16));

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

	line_cache()
	{
		clear();
	}

	void clear()
	{
		for(int i=0; i<HTSIZE; ++i)
		{
			hash_table[i].first = NULL;
			hash_table[i].offset = 0;
		}
	}

	const line_cache_result *find(const line_cache_key& key, uint32_t hash) const
	{
		const hash_chain& hc = hash_table[hash & (HTSIZE - 1)];
		const hash_block *hb = hc.first;
		int hbidx = hc.offset;

		for(; hb; hb = hb->next)
		{
			for(int i=hbidx - 1; i>=0; --i)
			{
				if (hb->nodes[i].hash == hash
					&& key == hb->nodes[i].value->first)
				{
					return &hb->nodes[i].value->second;
				}
			}

			hbidx = hash_block::N;
		}

		return NULL;
	}

	line_cache_result& insert(const line_cache_key& key, uint32_t hash, linear_allocator& alloc)
	{
		hash_chain& hc = hash_table[hash & (HTSIZE - 1)];
		hash_block *hb = hc.first;
		int hbidx = hc.offset;

		if (!hb || hbidx >= hash_block::N)
		{
			hash_block *hb2 = alloc.allocate<hash_block>();
			memset(hb2, 0, sizeof *hb2);
			hb2->next = hb;
			hc.first = hb2;
			hb = hb2;
			hbidx = 0;
		}

		hc.offset = hbidx+1;

		value_type *value = alloc.allocate<value_type>();
		value->first = key;

		hash_node node = { hash, value };
		hb->nodes[hbidx] = node;

		return value->second;
	}

private:
	hash_chain hash_table[HTSIZE];
};

#endif

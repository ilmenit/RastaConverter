#ifndef INSNSEQUENCECACHE_H
#define INSNSEQUENCECACHE_H

#include <stdint.h>

#include "RasterInstruction.h"
#include "LinearAllocator.h"

struct insn_sequence
{
	const SRasterInstruction *insns;
	unsigned insn_count;
	uint32_t hash;

	bool operator==(const insn_sequence &other) const
	{
		if (hash != other.hash) return false;
		if (insn_count != other.insn_count) return false;

		uint32_t diff = 0;
		for(unsigned i=0; i<insn_count; ++i)
			diff |= (insns[i].packed ^ other.insns[i].packed);

		return diff == 0;
	}
};

class insn_sequence_cache
{
public:
	struct hash_block
	{
		static const int N = 63;

		insn_sequence nodes[N];
		hash_block *next;
	};

	struct hash_chain
	{
		hash_block *first;
		int offset;
	};

	insn_sequence_cache()
	{
		clear();
	}

	void clear()
	{
		for(int i=0; i<1024; ++i)
		{
			hash_table[i].first = NULL;
			hash_table[i].offset = 0;
		}
	}

	const insn_sequence *insert(const insn_sequence& key, linear_allocator& alloc)
	{
		hash_chain& hc = hash_table[key.hash & 1023];
		hash_block *hb = hc.first;
		int hbidx = hc.offset;
		int hbidx2 = hbidx;

		for(const hash_block *hb2 = hb; hb2; hb2 = hb2->next)
		{
			for(int i=hbidx2 - 1; i>=0; --i)
			{
				if (key == hb2->nodes[i])
				{
					return &hb2->nodes[i];
				}
			}

			hbidx2 = hash_block::N;
		}

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

		SRasterInstruction *ri = NULL;
		
		if (key.insn_count) {
			ri = (SRasterInstruction *)alloc.allocate(sizeof(SRasterInstruction) * key.insn_count);
			memcpy(ri, key.insns, sizeof(SRasterInstruction) * key.insn_count);
		}

		insn_sequence& node = hb->nodes[hbidx];
		node.hash = key.hash;
		node.insns = ri;
		node.insn_count = key.insn_count;

		return &node;
	}

private:
	hash_chain hash_table[1024];
};

#endif

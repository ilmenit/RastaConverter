#ifndef LINEAR_ALLOCATOR_H
#define LINEAR_ALLOCATOR_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <cassert>

class linear_allocator
{
public:
	static const uint32_t BLOCK_SIZE = 8388608;

	enum allocation_type
	{
		LINE_CACHE_ENTRY,
		LINE_CACHE_HASH_BLOCK,
		LINE_CACHE_COLOR_ROW,
		LINE_CACHE_TARGET_ROW,
		INSN_CACHE_HASH_BLOCK,
		INSN_CACHE_DATA,
		ALLOCATION_TYPE_COUNT
	};

	struct statistics
	{
		size_t resident_bytes = 0;
		size_t allocated_by_type[ALLOCATION_TYPE_COUNT]{};
	};

	explicit linear_allocator(size_t block_size = BLOCK_SIZE, statistics *stats = NULL)
		: chunk_list(NULL)
		, alloc_left(0)
		, alloc_total(0)
		, m_block_size(block_size)
		, m_stats(stats ? stats : &m_owned_stats)
	{
	}

	linear_allocator(const linear_allocator&) = delete;
	linear_allocator& operator=(const linear_allocator&) = delete;

	linear_allocator(linear_allocator&& other) noexcept
		: chunk_list(other.chunk_list)
		, alloc_ptr(other.alloc_ptr)
		, alloc_left(other.alloc_left)
		, alloc_total(other.alloc_total)
		, m_block_size(other.m_block_size)
		, m_owned_stats(other.m_owned_stats)
		, m_stats(other.m_stats == &other.m_owned_stats ? &m_owned_stats : other.m_stats)
	{
		other.chunk_list = NULL;
		other.alloc_ptr = NULL;
		other.alloc_left = 0;
		other.alloc_total = 0;
	}

	linear_allocator& operator=(linear_allocator&& other) noexcept
	{
		if (this == &other)
			return *this;
		clear();
		chunk_list = other.chunk_list;
		alloc_ptr = other.alloc_ptr;
		alloc_left = other.alloc_left;
		alloc_total = other.alloc_total;
		m_block_size = other.m_block_size;
		m_owned_stats = other.m_owned_stats;
		m_stats = other.m_stats == &other.m_owned_stats ? &m_owned_stats : other.m_stats;
		other.chunk_list = NULL;
		other.alloc_ptr = NULL;
		other.alloc_left = 0;
		other.alloc_total = 0;
		return *this;
	}

	~linear_allocator()
	{
		clear();
	}

	void clear()
	{
		while(chunk_list)
		{
			chunk_node *next = chunk_list->next;

			free(chunk_list);

			chunk_list = next;
		}

		alloc_left = 0;
		m_stats->resident_bytes -= alloc_total;
		alloc_total = 0;
	}

	size_t size() const { return alloc_total; }
	size_t allocated_bytes(allocation_type type) const { return m_stats->allocated_by_type[type]; }

	void set_statistics(statistics *stats)
	{
		assert(alloc_total == 0);
		m_stats = stats ? stats : &m_owned_stats;
	}

	template<class T>
	T *allocate(allocation_type type)
	{
		return new(allocate(sizeof(T), type)) T;
	}

	void *allocate(size_t n, allocation_type type)
	{
		n = (n + 7) & ~7;
		m_stats->allocated_by_type[type] += n;

		if (alloc_left < n)
		{
			size_t to_alloc = m_block_size - 64 - sizeof(chunk_node);

			if (to_alloc < n)
				to_alloc = n;

			chunk_node *new_node = (chunk_node *)malloc(sizeof(chunk_node) + to_alloc);
			new_node->next = chunk_list;
			chunk_list = new_node;
			alloc_ptr = (char *)(new_node + 1);
			alloc_left = to_alloc;
			const size_t chunk_size = to_alloc + sizeof(chunk_node);
			alloc_total += chunk_size;
			m_stats->resident_bytes += chunk_size;
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
	size_t m_block_size;
	statistics m_owned_stats;
	statistics *m_stats;
};

#endif

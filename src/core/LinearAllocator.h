#ifndef LINEAR_ALLOCATOR_H
#define LINEAR_ALLOCATOR_H

#include <stdlib.h>

class linear_allocator
{
public:
	static const uint32_t BLOCK_SIZE = 8388608;

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

	size_t size() const { return alloc_total; }

	template<class T>
	T *allocate()
	{
		return new(allocate(sizeof(T))) T;
	}

	void *allocate(size_t n)
	{
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

#endif

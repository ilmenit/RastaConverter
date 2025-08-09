#ifndef PROGRAM_H
#define PROGRAM_H

#include <vector>
#include "../color/rgb.h"
#include "RasterInstruction.h"
#include "cache/InsnSequenceCache.h"

const int sprite_screen_color_cycle_start=48;
const int sprite_size=32;
const int free_cycles=53; // must be set depending on the mode, PMG, LMS etc.

typedef unsigned char sprites_row_memory_t[4][8];
typedef sprites_row_memory_t sprites_memory_t[240]; // we convert it to 240 bytes of PMG memory at the end of processing.

struct ScreenCycle {
	int offset; // position on the screen (can be <0 - previous line)
	int length; // length in pixels for 2 CPU cycles
};

const int CYCLES_MAX = 114;

extern ScreenCycle screen_cycles[CYCLES_MAX];

// Safely get screen cycle offset; returns a large value if out of range to skip instruction scheduling
inline int safe_screen_cycle_offset(int cycle)
{
    if (cycle < 0)
        return -100000;
    if (cycle >= CYCLES_MAX)
        return 1000; // past end of drawable area
    return screen_cycles[cycle].offset;
}

inline int safe_screen_cycle_length(int cycle)
{
    if (cycle < 0 || cycle >= CYCLES_MAX)
        return 0;
    return screen_cycles[cycle].length;
}

enum e_raster_instruction {
	// DO NOT CHANGE ORDER OF THOSE. A LOT OF THINGS DEPEND ON THE ORDER. ADD STH AT THE END IF YOU NEED!
	// 2 bytes instruction
	E_RASTER_LDA,
	E_RASTER_LDX,
	E_RASTER_LDY,
	E_RASTER_NOP,
	// 4 bytes intructions
	E_RASTER_STA,
	E_RASTER_STX,
	E_RASTER_STY,

	E_RASTER_MAX,
}; 

enum e_target {
	E_COLOR0,
	E_COLOR1,
	E_COLOR2,
	E_COLBAK,
	E_COLPM0,
	E_COLPM1,
	E_COLPM2,
	E_COLPM3,
	E_HPOSP0,
	E_HPOSP1,
	E_HPOSP2,
	E_HPOSP3,
	E_TARGET_MAX,
};

enum e_mutation_type {
	E_MUTATION_PUSH_BACK_TO_PREV, 
	E_MUTATION_COPY_LINE_TO_NEXT_ONE, 
	E_MUTATION_SWAP_LINE_WITH_PREV_ONE, 
	E_MUTATION_ADD_INSTRUCTION,
	E_MUTATION_REMOVE_INSTRUCTION,
	E_MUTATION_SWAP_INSTRUCTION,
	E_MUTATION_CHANGE_TARGET, 
	E_MUTATION_CHANGE_VALUE, // -1,+1,-16,+16
	E_MUTATION_CHANGE_VALUE_TO_COLOR, 
	E_MUTATION_MAX,
};

class screen_line {
private:
	std::vector < rgb > pixels;
public:
	void Resize(size_t i)
	{
		pixels.resize(i);
	}

	rgb& operator[](size_t i)
	{
		return pixels[i];
	}

	const rgb& operator[](size_t i) const
	{
		return pixels[i];
	}

	size_t size() const
	{
		return pixels.size();
	}
};

struct raster_line {
	std::vector<SRasterInstruction> instructions;

	raster_line()
	{
		cycles = 0;
		hash = 0;
		cache_key = NULL;
		// Pre-allocate memory to avoid frequent reallocations
		instructions.reserve(16); // Typical instruction count
	}

	void rehash()
	{
		unsigned h = 0;

		for(std::vector<SRasterInstruction>::const_iterator it = instructions.begin(), itEnd = instructions.end(); it != itEnd; ++it)
		{
			h += (unsigned) it->hash();

			h = (h >> 27) + (h << 5);
		}

		this->hash = h;
	}

	void recache_insns(insn_sequence_cache& cache, linear_allocator& alloc)
	{
		insn_sequence is;
		is.hash = hash;
		is.insns = instructions.data();
		is.insn_count = (uint32_t)instructions.size();

		cache_key = cache.insert(is, alloc);
	}

	void swap(raster_line& other)
	{
		instructions.swap(other.instructions);
		std::swap(cycles, other.cycles);
		std::swap(hash, other.hash);
		std::swap(cache_key, other.cache_key);
	}

	int cycles; // cache, to chech if we can add/remove new instructions
	unsigned hash;
	const insn_sequence *cache_key;
};

struct raster_picture {
	unsigned char mem_regs_init[E_TARGET_MAX];
	std::vector < raster_line > raster_lines;
	raster_picture()
	{
	}

	raster_picture(size_t height)
	{
		raster_lines.resize(height);
	}

	void recache_insns(insn_sequence_cache& cache, linear_allocator& alloc)
	{
		size_t n = raster_lines.size();

		for(size_t i=0; i<n; ++i)
		{
            // Defensive: ensure vector data is contiguous and count matches
            raster_lines[i].rehash();
            raster_lines[i].recache_insns(cache, alloc);
		}
	}

    void recache_insns_if_needed(insn_sequence_cache& cache, linear_allocator& alloc)
    {
        size_t n = raster_lines.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (raster_lines[i].cache_key == NULL) {
                raster_lines[i].rehash();
                raster_lines[i].recache_insns(cache, alloc);
            }
        }
    }

	void uncache_insns()
	{
		size_t n = raster_lines.size();

		for(size_t i=0; i<n; ++i)
		{
			raster_lines[i].cache_key = NULL;
		}
	}
};

inline int GetInstructionCycles(const SRasterInstruction &instr)
{
	switch(instr.loose.instruction)
	{
	case E_RASTER_NOP:
	case E_RASTER_LDA:
	case E_RASTER_LDX:
	case E_RASTER_LDY:
		return 2;
	}
	return 4;
}

#endif

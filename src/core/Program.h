#ifndef PROGRAM_H
#define PROGRAM_H

#include <vector>
#include <cstring>
#include "rgb.h"
#include "RasterInstruction.h"
#include "InsnSequenceCache.h"

const int sprite_screen_color_cycle_start=48;
const int sprite_size=32;

// The emitted display uses ANTIC mode E, normal-width playfield DMA, LMS on
// every line, and single-line player/missile DMA. That fixed profile leaves 57
// CPU execution slots. Each raster line ends in a 3-cycle zero-page CMP, so
// the optimized body may use all of the preceding 54 slots.
constexpr int raster_cpu_slots = 57;
constexpr int raster_tail_cycles = 3;
constexpr int raster_program_cycle_limit = raster_cpu_slots - raster_tail_cycles;
constexpr int antic_scanline_cycles = 114;
constexpr int cycle_map_size = antic_scanline_cycles + 9;

typedef unsigned char sprites_row_memory_t[4][8];
typedef sprites_row_memory_t sprites_memory_t[240]; // we convert it to 240 bytes of PMG memory at the end of processing.

struct ScreenCycle {
	int offset; // position on the screen (can be <0 - previous line)
	int length; // length in pixels for 2 CPU cycles
};

const int CYCLES_MAX = 114;

extern ScreenCycle screen_cycles[CYCLES_MAX];
extern int screen_cpu_slots;

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
	E_MUTATION_COMPLEMENT_VALUE_DUAL,
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
			raster_lines[i].recache_insns(cache, alloc);
		}
	}

	void recache_missing_insns(insn_sequence_cache& cache, linear_allocator& alloc)
	{
		for (raster_line& line : raster_lines)
		{
			if (!line.cache_key)
				line.recache_insns(cache, alloc);
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

struct raster_patch_stats
{
	unsigned copied_lines = 0;
	unsigned reused_lines = 0;
};

inline raster_patch_stats patch_raster_picture(
	raster_picture& destination, const raster_picture& source)
{
	raster_patch_stats stats;
	memcpy(destination.mem_regs_init, source.mem_regs_init,
		sizeof destination.mem_regs_init);
	if (destination.raster_lines.size() != source.raster_lines.size())
	{
		destination.raster_lines = source.raster_lines;
		stats.copied_lines = static_cast<unsigned>(source.raster_lines.size());
		for (raster_line& line : destination.raster_lines)
			line.cache_key = NULL;
		return stats;
	}

	for (size_t index = 0; index < source.raster_lines.size(); ++index)
	{
		raster_line& destinationLine = destination.raster_lines[index];
		const raster_line& sourceLine = source.raster_lines[index];
		if (destinationLine.cycles == sourceLine.cycles
			&& destinationLine.hash == sourceLine.hash
			&& destinationLine.instructions == sourceLine.instructions)
		{
			++stats.reused_lines;
			continue;
		}
		destinationLine = sourceLine;
		destinationLine.cache_key = NULL;
		++stats.copied_lines;
	}
	return stats;
}

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

enum raster_program_validation_error
{
	E_RASTER_VALID = 0,
	E_RASTER_INVALID_INSTRUCTION = 1 << 0,
	E_RASTER_INVALID_TARGET = 1 << 1,
	E_RASTER_CYCLE_MISMATCH = 1 << 2,
	E_RASTER_CYCLE_LIMIT_EXCEEDED = 1 << 3,
};

inline unsigned ValidateRasterLine(const raster_line& line)
{
	unsigned errors = E_RASTER_VALID;
	int calculatedCycles = 0;
	for (const SRasterInstruction& instruction : line.instructions)
	{
		const unsigned opcode = static_cast<unsigned>(instruction.loose.instruction);
		if (opcode >= static_cast<unsigned>(E_RASTER_MAX))
		{
			errors |= E_RASTER_INVALID_INSTRUCTION;
			continue;
		}
		calculatedCycles += GetInstructionCycles(instruction);
		if (instruction.loose.instruction >= E_RASTER_STA
			&& static_cast<unsigned>(instruction.loose.target)
				>= static_cast<unsigned>(E_TARGET_MAX))
		{
			errors |= E_RASTER_INVALID_TARGET;
		}
	}
	if (line.cycles != calculatedCycles)
		errors |= E_RASTER_CYCLE_MISMATCH;
	if (line.cycles < 0 || line.cycles > raster_program_cycle_limit
		|| calculatedCycles > raster_program_cycle_limit)
	{
		errors |= E_RASTER_CYCLE_LIMIT_EXCEEDED;
	}
	return errors;
}

inline unsigned ValidateRasterPicture(const raster_picture& picture)
{
	unsigned errors = E_RASTER_VALID;
	for (const raster_line& line : picture.raster_lines)
		errors |= ValidateRasterLine(line);
	return errors;
}

#endif

#include "Program.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>

void create_cycles_table();

namespace
{
void Require(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

void TestRasterPicturePatchCopiesOnlyChangedLines()
{
	raster_picture source(3);
	raster_picture destination(3);
	for (int index = 0; index < E_TARGET_MAX; ++index)
	{
		source.mem_regs_init[index] = static_cast<unsigned char>(index + 1);
		destination.mem_regs_init[index] = 0;
	}
	for (int line = 0; line < 3; ++line)
	{
		SRasterInstruction instruction{};
		instruction.packed = static_cast<unsigned>(line + 10);
		source.raster_lines[line].instructions.push_back(instruction);
		source.raster_lines[line].cycles = 2;
		source.raster_lines[line].rehash();
		destination.raster_lines[line] = source.raster_lines[line];
		destination.raster_lines[line].cache_key =
			reinterpret_cast<const insn_sequence*>(static_cast<uintptr_t>(line + 1));
	}
	source.raster_lines[1].instructions[0].packed = 99;
	source.raster_lines[1].rehash();

	const insn_sequence* reusedLine0 = destination.raster_lines[0].cache_key;
	const insn_sequence* reusedLine2 = destination.raster_lines[2].cache_key;
	const raster_patch_stats stats = patch_raster_picture(destination, source);
	Require(stats.copied_lines == 1, "patch must copy the one changed line");
	Require(stats.reused_lines == 2, "patch must reuse unchanged lines");
	Require(destination.raster_lines[0].cache_key == reusedLine0,
		"patch must retain the instruction-cache key for reused line 0");
	Require(destination.raster_lines[1].cache_key == nullptr,
		"patch must invalidate the copied line's instruction-cache key");
	Require(destination.raster_lines[2].cache_key == reusedLine2,
		"patch must retain the instruction-cache key for reused line 2");
	Require(destination.raster_lines[1].instructions == source.raster_lines[1].instructions,
		"patch must copy changed instructions exactly");
	Require(destination.mem_regs_init[5] == source.mem_regs_init[5],
		"patch must copy initial register state");
}

void TestRasterProgramValidation()
{
	raster_picture picture(1);
	SRasterInstruction load{};
	load.loose.instruction = E_RASTER_LDA;
	load.loose.value = 42;
	load.loose.target = E_TARGET_MAX; // Loads do not address a hardware target.
	picture.raster_lines[0].instructions.push_back(load);
	picture.raster_lines[0].cycles = 2;
	Require(ValidateRasterPicture(picture) == E_RASTER_VALID,
		"legal load-only line must validate");

	picture.raster_lines[0].cycles = 4;
	Require((ValidateRasterPicture(picture) & E_RASTER_CYCLE_MISMATCH) != 0,
		"cached cycle mismatch must be detected");
	picture.raster_lines[0].cycles = 2;

	SRasterInstruction store{};
	store.loose.instruction = E_RASTER_STA;
	store.loose.target = E_TARGET_MAX;
	picture.raster_lines[0].instructions.push_back(store);
	picture.raster_lines[0].cycles = 6;
	Require((ValidateRasterPicture(picture) & E_RASTER_INVALID_TARGET) != 0,
		"store to the target sentinel must be rejected");

	picture.raster_lines[0].instructions.assign(28, load);
	picture.raster_lines[0].cycles = 56;
	Require((ValidateRasterPicture(picture) & E_RASTER_CYCLE_LIMIT_EXCEEDED) != 0,
		"line beyond the fixed program-cycle limit must be detected");
}

}

int main()
{
	create_cycles_table();
	Require(antic_scanline_cycles == 114, "ANTIC scanline must contain 114 machine cycles");
	Require(screen_cpu_slots == raster_cpu_slots,
		"fixed ANTIC profile must expose exactly 57 CPU slots");
	Require(raster_program_cycle_limit == 54,
		"three-cycle line tail must leave 54 program cycles");

	for (int bodyCycles = 0; bodyCycles <= raster_program_cycle_limit; bodyCycles += 2)
	{
		int emittedBodyCycles = bodyCycles;
		while (emittedBodyCycles < raster_program_cycle_limit)
			emittedBodyCycles += 2;
		Require(emittedBodyCycles + raster_tail_cycles == raster_cpu_slots,
			"program, filler, and tail must consume exactly one line's CPU slots");
	}
	TestRasterPicturePatchCopiesOnlyChangedLines();
	TestRasterProgramValidation();

	std::cout << "Timing model tests passed\n";
	return 0;
}

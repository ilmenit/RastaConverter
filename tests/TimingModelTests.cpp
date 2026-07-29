#include "Program.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <initializer_list>

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

void RequireSlots(const DmaTimingProfile& profile,
	std::initializer_list<int> expected, const char* message)
{
	Require(profile.cpu_slots == static_cast<int>(expected.size()), message);
	int index = 0;
	for (int logicalSlot : expected)
	{
		Require(profile.cycles[index].offset == (logicalSlot - 24) * 2, message);
		++index;
	}
}

void TestAntic4TimingProfiles()
{
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::Antic4LmsBadline),
		{-8,-7,-6,-5,-4,-3,-2,-1,8,9,10,11,12,13,14,15,16,17,19,
			100,101,102,103,104,105},
		"ANTIC 4 LMS badline slot map differs from the hardware contract");
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::Antic4Badline),
		{-8,-7,-6,-5,-4,-3,-2,-1,6,7,8,9,10,11,12,13,14,15,16,17,19,
			100,101,102,103,104,105},
		"ANTIC 4 ordinary badline slot map differs from the hardware contract");
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::Antic4Continuation),
		{-8,-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
			22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,72,74,76,78,80,82,
			84,86,88,90,92,94,96,98,100,101,102,103,104,105},
		"ANTIC 4 continuation slot map differs from the hardware contract");

	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 0).optimizer_cycle_limit == 22,
		"line 0 must reserve the LMS badline suffix");
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 8).optimizer_cycle_limit == 24,
		"ordinary ANTIC 4 badlines must allow 24 optimizer cycles");
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 1).optimizer_cycle_limit == 54,
		"ordinary continuation lines must allow 54 optimizer cycles");
	const RasterLineSchedule transition =
		GetRasterLineSchedule(GraphicsMode::Antic4, 23);
	Require(transition.optimizer_cycle_limit == 48
		&& transition.fixed_suffix_cycles == 12
		&& transition.chbase_transition,
		"CHBASE transition lines must reserve the verified 12-cycle suffix");
	Require(!GetRasterLineSchedule(GraphicsMode::Antic4, 23, 24).chbase_transition,
		"the final scanline must not switch to a charset that is never displayed");
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 23, 32).chbase_transition,
		"line 23 must switch CHBASE when another character row follows");
	Require(NormalizeAntic4Height(181) == 184
		&& NormalizeAntic4Height(240) == 240
		&& NormalizeAntic4Height(1) == 8,
		"ANTIC 4 height normalization must use complete nearest character rows");
	const DmaTimingProfile& continuation =
		GetDmaTimingProfile(DmaTimingKind::Antic4Continuation);
	// 48 body + BIT zp (3) + LDA # (2) + STA abs (4): the store write
	// consumes logical slot 56, ANTIC cycle 102. AltirraBridge proves that
	// the former cycle-98 write changes the final cell on the old row.
	Require(continuation.cycles[56].offset == (102 - 24) * 2,
		"verified CHBASE store must land at ANTIC cycle 102");
	Require(102 + 2 == 104,
		"queued CHBASE update must become effective at ANTIC cycle 104");
	SRasterInstruction timedStore{};
	timedStore.loose.instruction = E_RASTER_STA;
	Require(RasterInstructionCompletionOffset(
			continuation.cycles.data(), 28, timedStore)
			== (48 - 24) * 2,
		"raster stores must take effect on their fourth CPU slot, not opcode fetch");

	raster_picture picture(24);
	picture.graphics_mode = GraphicsMode::Antic4;
	picture.antic4_attributes.assign(3, 0);
	SRasterInstruction load{};
	load.loose.instruction = E_RASTER_LDA;
	picture.raster_lines[0].instructions.assign(12, load);
	picture.raster_lines[0].cycles = 24;
	Require((ValidateRasterPicture(picture) & E_RASTER_CYCLE_LIMIT_EXCEEDED) != 0,
		"picture validation must use line 0's 22-cycle schedule");

	raster_picture legacy(1);
	Require(legacy.antic4_attributes.empty(),
		"mode-E pictures must not carry ANTIC 4 attribute storage");
	SRasterInstruction store{};
	store.loose.instruction = E_RASTER_STA;
	store.loose.target = E_COLOR3;
	legacy.raster_lines[0].instructions.push_back(store);
	legacy.raster_lines[0].cycles = 4;
	Require((ValidateRasterPicture(legacy) & E_RASTER_INVALID_TARGET) != 0,
		"mode E must reject COLPF3 stores");
	raster_picture antic4(8);
	antic4.graphics_mode = GraphicsMode::Antic4;
	antic4.antic4_attributes.assign(1, 0);
	antic4.raster_lines[0] = legacy.raster_lines[0];
	Require(ValidateRasterPicture(antic4) == E_RASTER_VALID,
		"ANTIC 4 must accept COLPF3 stores");
	antic4.raster_lines[0].instructions[0].loose.target = E_HPOSP0;
	Require((ValidateRasterPicture(antic4) & E_RASTER_INVALID_TARGET) != 0,
		"ANTIC 4 must reject mid-line HPOSP writes that the evaluator cannot model");
}

void TestAntic4EncodingPrimitives()
{
	unsigned char encoded = 255;
	Require(EncodeAntic4PlayfieldTarget(E_COLBAK, false, encoded) && encoded == 0,
		"COLBK must encode as glyph value 00");
	Require(EncodeAntic4PlayfieldTarget(E_COLOR0, true, encoded) && encoded == 1,
		"COLPF0 must encode as glyph value 01");
	Require(EncodeAntic4PlayfieldTarget(E_COLOR1, false, encoded) && encoded == 2,
		"COLPF1 must encode as glyph value 10");
	Require(EncodeAntic4PlayfieldTarget(E_COLOR2, false, encoded) && encoded == 3,
		"normal glyph value 11 must select COLPF2");
	Require(!EncodeAntic4PlayfieldTarget(E_COLOR2, true, encoded),
		"alternate cells must reject COLPF2");
	Require(EncodeAntic4PlayfieldTarget(E_COLOR3, true, encoded) && encoded == 3,
		"alternate glyph value 11 must select COLPF3");
	Require(!EncodeAntic4PlayfieldTarget(E_COLOR3, false, encoded),
		"normal cells must reject COLPF3");
	Require(Antic4ScreenCode(0, 39, false) == 39,
		"first private-glyph row must end at glyph 39");
	Require(Antic4ScreenCode(1, 0, false) == 40,
		"second private-glyph row must begin at glyph 40");
	Require(Antic4ScreenCode(2, 39, true) == (119 | 0x80),
		"third private-glyph row must end at inverse glyph 119");
	Require(Antic4ScreenCode(3, 0, false) == 0,
		"next charset must reuse glyph zero");
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
	TestAntic4TimingProfiles();
	TestAntic4EncodingPrimitives();

	std::cout << "Timing model tests passed\n";
	return 0;
}

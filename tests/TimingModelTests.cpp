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
	std::initializer_list<int> expected, int displayStart, const char* message)
{
	Require(profile.cpu_slots == static_cast<int>(expected.size()), message);
	int index = 0;
	for (int logicalSlot : expected)
	{
		Require(profile.cycles[index].offset
			== (logicalSlot - displayStart) * 2, message);
		++index;
	}
}

void TestAntic4TimingProfiles()
{
	// Wide-playfield mode-4 DMA, derived by hand from the Altirra hardware
	// reference and confirmed on AltirraBridge: a 200-line program built to
	// exactly these counts runs a full frame without drifting one cycle, and a
	// store placed on each slot changes color exactly where these offsets say.
	// Do not regenerate these from the production builder.
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::Antic4WideLmsBadline),
		{-8,-7,-6,-5,-4,-3,-2,-1,8,9,11}, 22,
		"ANTIC 4 LMS badline slot map differs from the hardware contract");
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::Antic4WideBadline),
		{-8,-7,-6,-5,-4,-3,-2,-1,6,7,8,9,11}, 22,
		"ANTIC 4 ordinary badline slot map differs from the hardware contract");
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::Antic4WideContinuation),
		{-8,-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,
			14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
			72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104}, 22,
		"ANTIC 4 continuation slot map differs from the hardware contract");
	// A wide badline stays contended from cycle 10 to 105, so its ninth refresh
	// cannot run before cycle 106 - the slot the next logical line would have
	// opened with. The line after a badline is one CPU slot shorter.
	RequireSlots(
		GetDmaTimingProfile(DmaTimingKind::Antic4WideContinuationAfterBadline),
		{-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,
			14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
			72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104}, 22,
		"the line after an ANTIC 4 badline loses cycle 106 to deferred refresh");
	RequireSlots(GetDmaTimingProfile(
			DmaTimingKind::Antic4NormalLmsBadline),
		{-8,-7,-6,-5,-4,-3,-2,-1,8,9,10,11,12,13,14,15,16,17,19,
			100,101,102,103,104,105}, 24,
		"normal ANTIC 4 LMS badline slots differ from hardware DMA");
	RequireSlots(GetDmaTimingProfile(
			DmaTimingKind::Antic4NormalBadline),
		{-8,-7,-6,-5,-4,-3,-2,-1,6,7,8,9,10,11,12,13,14,15,16,17,19,
			100,101,102,103,104,105}, 24,
		"normal ANTIC 4 badline slots differ from hardware DMA");
	RequireSlots(GetDmaTimingProfile(
			DmaTimingKind::Antic4NormalContinuation),
		{-8,-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,13,14,15,16,17,
			18,19,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
			72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,101,102,103,
			104,105}, 24,
		"normal ANTIC 4 continuation slots differ from hardware DMA");
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 0, 240,
			PlayfieldWidth::Normal).optimizer_cycle_limit == 20
		&& GetRasterLineSchedule(GraphicsMode::Antic4, 8, 240,
			PlayfieldWidth::Normal).optimizer_cycle_limit == 22
		&& GetRasterLineSchedule(GraphicsMode::Antic4, 2, 240,
			PlayfieldWidth::Normal).optimizer_cycle_limit == 56
		&& GetRasterLineSchedule(GraphicsMode::Antic4, 23, 240,
			PlayfieldWidth::Normal).optimizer_cycle_limit == 50,
		"normal ANTIC 4 schedules must spend their additional DMA-free slots");

	// Raster instructions cost 2 or 4 cycles, so every optimizer limit must be
	// even, and limit + suffix must exactly equal the line's CPU slots: the
	// kernel never resynchronizes on WSYNC, so one stray cycle shifts the rest
	// of the frame.
	struct { int y; int slots; } const lines[] = {
		{0, 11}, {8, 13}, {1, 52}, {9, 52}, {2, 53}, {7, 53}, {23, 53},
	};
	for (const auto& line : lines)
	{
		const RasterLineSchedule schedule =
			GetRasterLineSchedule(GraphicsMode::Antic4, line.y, 200,
				PlayfieldWidth::Wide);
		Require(schedule.timing->cpu_slots == line.slots
			&& schedule.optimizer_cycle_limit % 2 == 0
			&& schedule.optimizer_cycle_limit + schedule.fixed_suffix_cycles
				== line.slots,
			"ANTIC 4 line budget must spend every CPU slot the hardware offers");
	}
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 0, 240,
		PlayfieldWidth::Wide).optimizer_cycle_limit == 6,
		"line 0 must reserve the LMS badline suffix");
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 8, 240,
		PlayfieldWidth::Wide).optimizer_cycle_limit == 8,
		"ordinary ANTIC 4 badlines must allow 8 optimizer cycles");
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 2, 240,
		PlayfieldWidth::Wide).optimizer_cycle_limit == 48,
		"ordinary continuation lines must allow 48 optimizer cycles");
	const RasterLineSchedule transition =
		GetRasterLineSchedule(GraphicsMode::Antic4, 23, 240,
			PlayfieldWidth::Wide);
	Require(transition.optimizer_cycle_limit == 44
		&& transition.fixed_suffix_cycles == 9
		&& transition.chbase_transition,
		"CHBASE transition lines must reserve the wide-playfield suffix");
	Require(!GetRasterLineSchedule(GraphicsMode::Antic4, 23, 24,
		PlayfieldWidth::Wide).chbase_transition,
		"the final scanline must not switch to a charset that is never displayed");
	Require(GetRasterLineSchedule(GraphicsMode::Antic4, 23, 32,
		PlayfieldWidth::Wide).chbase_transition,
		"line 23 must switch CHBASE when another character row follows");
	Require(NormalizeAntic4Height(181) == 184
		&& NormalizeAntic4Height(240) == 240
		&& NormalizeAntic4Height(1) == 8,
		"ANTIC 4 height normalization must use complete nearest character rows");
	const DmaTimingProfile& continuation =
		GetDmaTimingProfile(DmaTimingKind::Antic4WideContinuation);
	// 44 body + BIT zp (3) + LDA # (2) + STA abs (4) consumes all 53
	// wide-playfield CPU slots. The store starts on cycle 49 and completes on
	// slot 52, ANTIC cycle 104: the line's own last character-data fetch at 105
	// still reads the old charset, and the queued update takes effect at 106,
	// before the next line fetches anything.
	SRasterInstruction timedStore{};
	timedStore.loose.instruction = E_RASTER_STA;
	Require(RasterInstructionCompletionOffset(
			continuation.cycles.data(), 44 + 3 + 2, timedStore, true)
			== (104 - 22) * 2,
		"wide-playfield CHBASE store must land at ANTIC cycle 104");
	Require(RasterInstructionCompletionOffset(
			continuation.cycles.data(), 28, timedStore, true)
			== (62 - 22) * 2,
		"raster stores must take effect on their fourth CPU slot, not opcode fetch");

	// Mode E must keep taking effect on the opcode-fetch slot. Its map folds the
	// store's completion delay into the display-start constant, so applying the
	// delay again puts every write three slots late - invisible in the preview,
	// but it shears the generated .xex. Measured against Altirra: 99% of color
	// clocks match without the delay, 90% with it.
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::AnticENormal),
		{7,8,9,10,11,12,13,14,15,16,17,18,19,21,23,27,31,35,39,43,47,
			51,55,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,
			95,97,99,100,101,102,103,104,105,106,107,108,109,110,111,112},
		24, "normal ANTIC E slots differ from hardware DMA");
	RequireSlots(GetDmaTimingProfile(DmaTimingKind::AnticEWide),
		{7,8,9,10,11,13,15,17,19,21,23,27,31,35,39,43,47,51,55,59,61,
			63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,
			103,105,106,107,108,109,110,111,112},
		22, "wide ANTIC E slots differ from hardware DMA");
	create_cycles_table();
	Require(GetRasterLineSchedule(GraphicsMode::AnticE, 0).timing
			== &GetDmaTimingProfile(DmaTimingKind::AnticENormal),
		"mode E must keep using the proven mode-E DMA profile");
	for (int cycle = 0; cycle <= raster_program_cycle_limit; ++cycle)
		Require(RasterInstructionCompletionOffset(
				screen_cycles, cycle, timedStore, false)
				== screen_cycles[cycle].offset,
			"mode E stores must resolve to their opcode-fetch slot");
	Require(GetRasterLineSchedule(GraphicsMode::AnticE, 0, 240,
			PlayfieldWidth::Normal).timing->cpu_slots == 57
		&& GetRasterLineSchedule(GraphicsMode::AnticE, 0, 240,
			PlayfieldWidth::Wide).timing->cpu_slots == 50
		&& GetRasterLineSchedule(GraphicsMode::AnticE, 0, 240,
			PlayfieldWidth::Wide).optimizer_cycle_limit == 46,
		"mode E normal and wide widths need distinct exact DMA budgets");

	raster_picture picture(24);
	picture.graphics_mode = GraphicsMode::Antic4;
	picture.playfield_width = PlayfieldWidth::Wide;
	picture.antic4_attributes.assign(3, 0);
	SRasterInstruction load{};
	load.loose.instruction = E_RASTER_LDA;
	picture.raster_lines[0].instructions.assign(12, load);
	picture.raster_lines[0].cycles = 24;
	Require((ValidateRasterPicture(picture) & E_RASTER_CYCLE_LIMIT_EXCEEDED) != 0,
		"picture validation must use line 0's 6-cycle schedule");

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
	antic4.playfield_width = PlayfieldWidth::Wide;
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
	Require(Antic4ScreenCode(0, 41, 42, false) == 41,
		"first private-glyph row must end at glyph 41");
	Require(Antic4ScreenCode(1, 0, 42, false) == 42,
		"second private-glyph row must begin at glyph 42");
	Require(Antic4ScreenCode(2, 41, 42, true) == (125 | 0x80),
		"third private-glyph row must end at inverse glyph 125");
	Require(Antic4ScreenCode(3, 0, 42, false) == 0,
		"next charset must reuse glyph zero");
}

void TestGtiaPriority0Resolver()
{
	unsigned char colors[E_TARGET_MAX + 1]{};
	colors[E_COLOR0] = 0x22;
	colors[E_COLOR1] = 0x44;
	colors[E_COLOR2] = 0x86;
	colors[E_COLOR3] = 0xA0;
	colors[E_COLBAK] = 0x08;
	colors[E_COLPM0] = 0x10;
	colors[E_COLPM1] = 0x20;
	colors[E_COLPM2] = 0x40;
	colors[E_COLPM3] = 0x80;

	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR0, 0) == 0x11,
		"PRIOR 0 must show an unobstructed PF0 color");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR0, 1) == 0x19,
		"PRIOR 0 must OR P0 with PF0");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR1, 2) == 0x32,
		"PRIOR 0 must OR P1 with PF1");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR1, 3) == 0x2A,
		"P0 must suppress P1 before mixing with PF1");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR2, 1) == 0x08,
		"P0 must cover PF2 in PRIOR 0");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR0, 4) == 0x11,
		"PF0 must cover P2 in PRIOR 0");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR2, 4) == 0x63,
		"PRIOR 0 must OR P2 with PF2");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLOR3, 12) == 0x70,
		"P2 must suppress P3 before mixing with PF3");
	Require(ResolveGtiaPriority0ColorIndex(colors, E_COLBAK, 8) == 0x40,
		"an active P3 must replace background");
	Require(ResolveGtiaPriorityFColorIndex(colors, E_COLBAK, 1) == 0x08,
		"PRIOR F must show P0 over background");
	Require(ResolveGtiaPriorityFColorIndex(colors, E_COLOR0, 1) == 0x00,
		"conflicting PRIOR bits must blank a PF0/P0 overlap");
	Require(ResolveGtiaPriorityFColorIndex(colors, E_COLOR2, 1) == 0x43,
		"PF2 must suppress P0 with PRIOR F");
	Require(ResolveGtiaPriorityFColorIndex(colors, E_COLOR2, 4) == 0x00,
		"conflicting PRIOR bits must blank a PF2/P2 overlap");
	Require(ResolveGtiaPriority4ColorIndex(colors, E_COLBAK, 4) == 0x20,
		"PRIOR 4 must show P2 over background");
	Require(ResolveGtiaPriority4ColorIndex(colors, E_COLOR1, 4) == 0x22,
		"PRIOR 4 must keep PF1 above P2");
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
	TestGtiaPriority0Resolver();

	std::cout << "Timing model tests passed\n";
	return 0;
}

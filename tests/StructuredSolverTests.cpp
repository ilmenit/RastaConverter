#include "StructuredSolver.h"
#include "VisualObjective.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

void create_cycles_table();

namespace
{
void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

void TestPlayfieldScheduleCarriesCpuState()
{
	raster_line firstLine;
	StructuredLineBuilder first(firstLine, {});
	Require(first.TrySchedulePlayfieldWrite(E_COLOR0, 42, StructuredCpuRegister::A),
		"first color write must fit");
	Require(first.Cycles() == 6, "load and store must consume six cycles");
	Require(first.OutgoingState().a == 42, "outgoing A must retain loaded color");
	Require(ValidateRasterLine(firstLine) == E_RASTER_VALID,
		"scheduled first line must use the shared legality oracle");

	raster_line secondLine;
	StructuredLineBuilder second(secondLine, first.OutgoingState());
	Require(second.TrySchedulePlayfieldWrite(E_COLBAK, 42, StructuredCpuRegister::A),
		"next line must reuse carried A");
	Require(second.Cycles() == 4, "reused register value must need only a store");
	Require(secondLine.instructions.size() == 1
		&& secondLine.instructions[0].loose.instruction == E_RASTER_STA,
		"reused value must not emit a redundant load");
}

void TestScheduleRejectsInvalidAndOverBudgetTransitions()
{
	raster_line line;
	StructuredLineBuilder builder(line, {});
	Require(!builder.TrySchedulePlayfieldWrite(E_COLPM0, 12, StructuredCpuRegister::X),
		"playfield-only builder must reject PMG targets");
	Require(builder.Cycles() == 0 && line.instructions.empty(),
		"rejected target must not partially mutate the line");
	for (int index = 0; index < raster_program_cycle_limit / 2; ++index)
		Require(builder.TryAppendNop(), "NOP must fit through the exact body limit");
	Require(builder.Cycles() == raster_program_cycle_limit,
		"fixture must reach the exact body limit");
	Require(!builder.TrySchedulePlayfieldWrite(E_COLOR1, 9, StructuredCpuRegister::Y),
		"over-budget write must be rejected");
	Require(builder.Cycles() == raster_program_cycle_limit,
		"over-budget rejection must be atomic");
	Require(ValidateRasterLine(line) == E_RASTER_VALID,
		"line at the exact body limit must remain legal");
}

void TestInstructionOffsetsUseExactAnticTable()
{
	raster_line line;
	StructuredLineBuilder builder(line, {});
	const int initialOffset = builder.NextInstructionOffset();
	Require(builder.TrySchedulePlayfieldWrite(E_COLOR2, 7, StructuredCpuRegister::Y),
		"timed write must fit");
	Require(builder.NextInstructionOffset() == screen_cycles[6].offset,
		"builder must expose the shared ANTIC timing table");
	Require(builder.NextInstructionOffset() > initialOffset,
		"six consumed cycles must advance the screen position");
}

void TestPositionedWriteUsesExactOrNearestEarlierSlot()
{
	raster_line exactLine;
	StructuredLineBuilder exact(exactLine, {});
	const int exactPixel = StructuredInstructionEffectPixel(2);
	const StructuredWriteResult exactResult = exact.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLOR0, 17, exactPixel});
	Require(exactResult.feasible && !exactResult.repaired,
		"write at a reachable effect pixel must schedule exactly");
	Require(exactResult.actual_pixel == exactPixel,
		"exact schedule must report its shared-timing effect pixel");
	Require(exactLine.instructions.size() == 2
		&& exactLine.instructions[0].loose.instruction == E_RASTER_LDA
		&& exactLine.instructions[1].loose.instruction == E_RASTER_STA,
		"deterministic tie-breaking must select A load/store");

	int repairedPixel = exactPixel + 1;
	while (repairedPixel == StructuredInstructionEffectPixel(4))
		++repairedPixel;
	raster_line repairedLine;
	StructuredLineBuilder repaired(repairedLine, {});
	const StructuredWriteResult repairedResult = repaired.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLOR1, 23, repairedPixel});
	Require(repairedResult.feasible && repairedResult.repaired,
		"unreachable requested pixel must repair to an earlier legal slot");
	Require(repairedResult.actual_pixel <= repairedPixel,
		"repair must never make a color transition later than requested");
	for (int cycle = 2; cycle <= raster_program_cycle_limit - 4; cycle += 2)
	{
		const int candidatePixel = StructuredInstructionEffectPixel(cycle);
		Require(candidatePixel <= repairedResult.actual_pixel || candidatePixel > repairedPixel,
			"repair must choose the nearest legal earlier effect pixel");
	}
}

void TestPositionedWriteSelectsCarriedRegisterAndFailsAtomically()
{
	StructuredCpuState incoming;
	incoming.x = 61;
	raster_line line;
	StructuredLineBuilder builder(line, incoming);
	const int immediateStorePixel = StructuredInstructionEffectPixel(0);
	const StructuredWriteResult carried = builder.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLOR2, 61, immediateStorePixel});
	Require(carried.feasible && carried.source == StructuredCpuRegister::X,
		"deadline must select the carried register that avoids a load");
	Require(line.instructions.size() == 1
		&& line.instructions[0].loose.instruction == E_RASTER_STX,
		"carried-register schedule must emit only its store");

	const size_t instructionCount = line.instructions.size();
	const int cycles = line.cycles;
	const StructuredWriteResult impossible = builder.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLBAK, 99, StructuredInstructionEffectPixel(cycles) - 1});
	Require(!impossible.feasible, "missed deadline must be reported infeasible");
	Require(line.instructions.size() == instructionCount && line.cycles == cycles,
		"infeasible positioned transition must not partially mutate the line");
}

void TestBeamSelectsFixedReferenceMinimumAndMaterializesLegalLine()
{
	const int firstPixel = StructuredInstructionEffectPixel(2);
	const int secondPixel = StructuredInstructionEffectPixel(8);
	std::vector<StructuredSegmentCandidate> segments = {
		{E_COLOR0, firstPixel, {{10, 5.0}, {20, 1.0}}},
		{E_COLBAK, secondPixel, {{30, 0.5}, {40, 7.0}}},
	};
	StructuredBeamOptions options;
	options.width = 4;
	options.repair_cost_per_pixel = 0.25;
	const StructuredBeamResult result =
		SearchStructuredLineBeam({}, segments, options);
	Require(result.feasible, "beam must find a feasible segment sequence");
	Require(result.transitions.size() == 2
		&& result.transitions[0].value == 20
		&& result.transitions[1].value == 30,
		"beam must select the deterministic fixed-reference minimum");
	Require(result.cost == 1.5, "exact schedules must retain additive reference cost");
	Require(ValidateRasterLine(result.line) == E_RASTER_VALID,
		"beam output must pass the shared line oracle");
}

void TestBeamRejectsUnrepairablePartialStates()
{
	std::vector<StructuredSegmentCandidate> segments;
	for (int index = 0; index < 20; ++index)
		segments.push_back({E_COLOR0, StructuredInstructionEffectPixel(0),
			{{static_cast<unsigned char>(index + 1), 0.0}}});
	StructuredBeamOptions options;
	options.width = 2;
	const StructuredBeamResult result = SearchStructuredLineBeam({}, segments, options);
	Require(!result.feasible,
		"beam must reject a branch once an exact deadline cannot be repaired earlier");
}

void TestWindowBeamCarriesDiverseCpuStatesBetweenLines()
{
	const int loadedStorePixel = StructuredInstructionEffectPixel(2);
	const int immediateStorePixel = StructuredInstructionEffectPixel(0);
	std::vector<std::vector<StructuredSegmentCandidate>> lines = {
		{{E_COLOR0, loadedStorePixel, {{10, 0.0}, {20, 0.5}}}},
		{{E_COLBAK, immediateStorePixel, {{20, 0.0}}}},
	};
	StructuredBeamOptions options;
	options.width = 2;
	const StructuredWindowResult result =
		SearchStructuredWindowBeam({}, lines, options);
	Require(result.feasible && result.lines.size() == 2,
		"window beam must retain the state that makes the next line feasible");
	Require(result.transitions[0][0].value == 20,
		"window beam must prefer a slightly costlier predecessor when required later");
	Require(result.lines[1].instructions.size() == 1
		&& result.lines[1].instructions[0].loose.instruction == E_RASTER_STA,
		"next line must reuse the carried A value without a load");
	Require(result.cost == 0.5,
		"window cost must add each fixed-reference line cost exactly once");
	for (const raster_line& line : result.lines)
		Require(ValidateRasterLine(line) == E_RASTER_VALID,
			"every materialized window line must pass the shared oracle");
}

void TestSourceSegmentsMatchExhaustiveFixedReferenceMinimum()
{
	constexpr int width = 6;
	distance_t errorStorage[128][width];
	const distance_t* errorRows[128];
	for (int color = 0; color < 128; ++color)
	{
		errorRows[color] = errorStorage[color];
		for (int pixel = 0; pixel < width; ++pixel)
			errorStorage[color][pixel] = 1000;
	}
	// Raw GTIA values 2 and 4 map to palette rows 1 and 2.
	const distance_t row1[width] = {1, 2, 3, 20, 20, 20};
	const distance_t row2[width] = {9, 9, 9, 1, 2, 3};
	for (int pixel = 0; pixel < width; ++pixel)
	{
		errorStorage[1][pixel] = row1[pixel];
		errorStorage[2][pixel] = row2[pixel];
	}

	StructuredSegmentCandidate first;
	StructuredSegmentCandidate second;
	Require(BuildStructuredSourceSegment(E_COLOR0,
		StructuredInstructionEffectPixel(2), 0, 3, {2, 4}, errorRows,
		width, first), "first immutable source span must build");
	Require(BuildStructuredSourceSegment(E_COLBAK,
		StructuredInstructionEffectPixel(8), 3, 6, {2, 4}, errorRows,
		width, second), "second immutable source span must build");
	Require(first.values[0].reference_cost == 6.0
		&& first.values[1].reference_cost == 27.0
		&& second.values[0].reference_cost == 60.0
		&& second.values[1].reference_cost == 6.0,
		"segment costs must exactly sum their fixed source spans");

	double exhaustive = std::numeric_limits<double>::max();
	for (const auto& a : first.values)
		for (const auto& b : second.values)
			exhaustive = std::min(exhaustive,
				a.reference_cost + b.reference_cost);
	StructuredBeamOptions options;
	options.width = 4;
	options.repair_cost_per_pixel = 0.0;
	const StructuredBeamResult result =
		SearchStructuredLineBeam({}, {first, second}, options);
	Require(result.feasible && result.cost == exhaustive,
		"beam must match exhaustive choice on the source-referenced fixture");
	Require(result.transitions[0].value == 2
		&& result.transitions[1].value == 4,
		"beam must select each source span's lowest-error palette value");
}

void TestComparisonCandidatePatchesOnlyRequestedWindow()
{
	raster_picture baseline(3);
	for (int index = 0; index < E_TARGET_MAX; ++index)
		baseline.mem_regs_init[index] = static_cast<unsigned char>(index * 2);
	for (raster_line& line : baseline.raster_lines)
	{
		StructuredLineBuilder builder(line, {});
		Require(builder.TryAppendNop(), "baseline fixture NOP must fit");
	}
	StructuredWindowResult window;
	window.feasible = true;
	window.lines.resize(1);
	StructuredLineBuilder replacement(window.lines[0], {});
	Require(replacement.TrySchedulePlayfieldWrite(
		E_COLOR0, 22, StructuredCpuRegister::A),
		"replacement fixture write must fit");

	raster_picture candidate;
	Require(BuildStructuredWindowComparisonCandidate(
		baseline, 1, window, candidate),
		"comparison candidate must accept a legal bounded window");
	Require(candidate.raster_lines[0].instructions
		== baseline.raster_lines[0].instructions
		&& candidate.raster_lines[2].instructions
		== baseline.raster_lines[2].instructions,
		"comparison candidate must preserve surrounding stochastic lines");
	Require(candidate.raster_lines[1].instructions
		== window.lines[0].instructions,
		"comparison candidate must patch the requested structured line");
	Require(candidate.mem_regs_init[E_COLOR2]
		== baseline.mem_regs_init[E_COLOR2],
		"comparison candidate must preserve identical initial registers");
	Require(ValidateRasterPicture(candidate) == E_RASTER_VALID,
		"comparison candidate must pass the complete structural oracle");

	window.feasible = false;
	Require(!BuildStructuredWindowComparisonCandidate(
		baseline, 1, window, candidate),
		"comparison must reject a window the search marked infeasible");
}

void TestReplayWindowExtractsStochasticCpuStateAndPlayfieldStores()
{
	raster_picture baseline(2);
	StructuredLineBuilder first(baseline.raster_lines[0], {});
	Require(first.TrySchedulePlayfieldWrite(E_COLOR0, 18,
		StructuredCpuRegister::X), "first stochastic fixture write must fit");
	Require(first.TryAppendNop(), "stochastic fixture NOP must fit");
	StructuredLineBuilder second(baseline.raster_lines[1], first.OutgoingState());
	Require(second.TrySchedulePlayfieldWrite(E_COLBAK, 18,
		StructuredCpuRegister::X), "second stochastic fixture must reuse X");
	SRasterInstruction closingLoad{};
	closingLoad.loose.instruction = E_RASTER_LDA;
	closingLoad.loose.target = E_TARGET_MAX;
	closingLoad.loose.value = 77;
	baseline.raster_lines[1].instructions.push_back(closingLoad);
	baseline.raster_lines[1].cycles += GetInstructionCycles(closingLoad);
	SRasterInstruction fixedPmg{};
	fixedPmg.loose.instruction = E_RASTER_STA;
	fixedPmg.loose.target = E_HPOSP0;
	baseline.raster_lines[1].instructions.push_back(fixedPmg);
	const int fixedPmgCycle = baseline.raster_lines[1].cycles;
	baseline.raster_lines[1].cycles += GetInstructionCycles(fixedPmg);
	baseline.raster_lines[1].rehash();

	StructuredBeamOptions options;
	options.width = 4;
	StructuredWindowResult replay;
	Require(ExtractStructuredReplayWindow(
		baseline, 1, 1, options, replay),
		"replay extraction must recover a legal selected window");
	Require(replay.transitions.size() == 1
		&& replay.transitions[0].size() == 2
		&& replay.transitions[0][0].target == E_COLBAK
		&& replay.transitions[0][0].value == 18
		&& replay.transitions[0][1].target == E_HPOSP0
		&& replay.transitions[0][1].value == 77
		&& replay.transitions[0][1].exact_store_cycle == fixedPmgCycle,
		"replay extraction must retain playfield and exact PMG stores");
	Require(replay.lines[0].instructions.size() == 3
		&& replay.lines[0].instructions[0].loose.instruction == E_RASTER_STX,
		"replay must recover carried X and avoid a redundant store load");
	Require(replay.lines[0].instructions[1].loose.instruction == E_RASTER_LDA
		&& replay.lines[0].instructions[1].loose.value == 77
		&& replay.lines[0].instructions[2].loose.instruction == E_RASTER_STA
		&& replay.lines[0].instructions[2].loose.target == E_HPOSP0
		&& replay.outgoing.a == 77,
		"replay must restore CPU state while preserving fixed PMG timing");
	Require(!ExtractStructuredReplayWindow(
		baseline, 2, 1, options, replay),
		"replay extraction must reject an out-of-range window");
}

void TestSourceWindowAddsRankedAlternatesAndPreservesClosure()
{
	constexpr int width = 160;
	distance_t errors[128][width];
	const distance_t* rows[128];
	for (int color = 0; color < 128; ++color)
	{
		rows[color] = errors[color];
		std::fill(errors[color], errors[color] + width, 100);
	}
	raster_picture baseline(1);
	StructuredLineBuilder builder(baseline.raster_lines[0], {});
	for (int index = 0; index < 8; ++index)
		Require(builder.TryAppendNop(), "source-window positioning NOP must fit");
	const int firstPixel = StructuredInstructionEffectPixel(18);
	const int secondPixel = StructuredInstructionEffectPixel(24);
	Require(builder.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLOR0, 18, firstPixel}).feasible,
		"first source-window baseline transition must fit");
	Require(builder.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLBAK, 18, secondPixel}).feasible,
		"closing source-window baseline transition must fit");
	for (int pixel = firstPixel; pixel < secondPixel; ++pixel)
	{
		errors[9][pixel] = 10;
		errors[3][pixel] = 1;
	}
	for (int pixel = secondPixel; pixel < width; ++pixel)
		errors[9][pixel] = 1;
	StructuredBeamOptions options;
	options.width = 16;
	options.repair_cost_per_pixel = 0.0;
	StructuredWindowResult result;
	Require(ExtractStructuredSourceWindow(baseline, 0, 1, rows, width, 2,
		options, result), "source-window extraction must remain state-closed");
	Require(result.transitions[0][0].value == 6
		&& result.transitions[0][1].value == 18,
		"source ranking must improve an interior span and restore final state");
	Require(result.outgoing.a == 18,
		"alternate choices must preserve stochastic outgoing state closure");
	Require(result.cost == static_cast<double>(secondPixel - firstPixel)
		+ static_cast<double>(width - secondPixel),
		"selected choices must carry their exact source span costs");

	Require(!ExtractStructuredSourceWindow(baseline, 0, 1, rows, width, 0,
		options, result), "source-window extraction must reject no alternatives");
}

void TestSourceWindowRanksAcrossInterveningTargetWrites()
{
	constexpr int width = 160;
	distance_t errors[128][width];
	const distance_t* rows[128];
	for (int color = 0; color < 128; ++color)
	{
		rows[color] = errors[color];
		std::fill(errors[color], errors[color] + width, 100);
	}
	raster_picture baseline(1);
	StructuredLineBuilder builder(baseline.raster_lines[0], {});
	for (int index = 0; index < 6; ++index)
		Require(builder.TryAppendNop(), "target-lifetime positioning NOP must fit");
	const int firstPixel = StructuredInstructionEffectPixel(14);
	const int interveningPixel = StructuredInstructionEffectPixel(20);
	const int closingPixel = StructuredInstructionEffectPixel(26);
	Require(builder.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLOR0, 18, firstPixel}).feasible,
		"target-lifetime first write must fit");
	Require(builder.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLBAK, 20, interveningPixel}).feasible,
		"target-lifetime intervening target write must fit");
	Require(builder.TrySchedulePlayfieldWriteAtOrBefore(
		{E_COLOR0, 18, closingPixel}).feasible,
		"target-lifetime closing same-target write must fit");

	unsigned char ownershipStorage[width];
	const unsigned char* ownership[1] = {ownershipStorage};
	std::fill(ownershipStorage, ownershipStorage + width,
		static_cast<unsigned char>(E_COLBAK));
	for (int pixel = std::max(0, firstPixel); pixel < width; ++pixel)
		errors[10][pixel] = 1;
	for (int pixel = interveningPixel; pixel < closingPixel; ++pixel)
	{
		ownershipStorage[pixel] = static_cast<unsigned char>(E_COLOR0);
		errors[9][pixel] = 10;
		errors[3][pixel] = 1;
	}
	for (int pixel = closingPixel; pixel < width; ++pixel)
		errors[9][pixel] = 1;

	StructuredBeamOptions options;
	options.width = 16;
	options.repair_cost_per_pixel = 0.0;
	options.target_lifetime_spans = true;
	StructuredWindowResult result;
	Require(ExtractStructuredSourceWindow(baseline, 0, 1, rows, width, 1,
		options, result, ownership),
		"target-lifetime source window must remain state-closed");
	Require(result.transitions[0][0].value == 6,
		"source ranking must include owned pixels after another target is written");
	Require(result.transitions[0][1].target == E_COLBAK
		&& result.transitions[0][2].target == E_COLOR0
		&& result.outgoing.a == 18,
		"target-lifetime ranking must preserve the schedule and outgoing closure");
}

void TestIndependentDualProgramsRemainLegal()
{
	const int pixel = StructuredInstructionEffectPixel(2);
	StructuredBeamOptions options;
	const StructuredBeamResult frameA = SearchStructuredLineBeam({},
		{{E_COLOR0, pixel, {{18, 0.0}}}}, options);
	const StructuredBeamResult frameB = SearchStructuredLineBeam({},
		{{E_COLBAK, pixel, {{42, 0.0}}}}, options);
	Require(frameA.feasible && frameB.feasible,
		"independent dual-frame structured programs must both materialize");
	Require(ValidateRasterLine(frameA.line) == E_RASTER_VALID
		&& ValidateRasterLine(frameB.line) == E_RASTER_VALID,
		"both independent dual-frame programs must pass the shared legality oracle");
	Require(frameA.transitions[0].target != frameB.transitions[0].target,
		"dual legality must not require identical A/B programs");
}

void TestPairedProblemExtractionPreservesRetainedControlAndClosure()
{
	raster_picture baselineA(3);
	raster_picture baselineB(3);
	StructuredCpuState stateA;
	StructuredCpuState stateB;
	for (size_t line = 0; line < 3; ++line)
	{
		StructuredLineBuilder a(baselineA.raster_lines[line], stateA);
		StructuredLineBuilder b(baselineB.raster_lines[line], stateB);
		Require(a.TrySchedulePlayfieldWrite(E_COLOR0,
			static_cast<unsigned char>(18 + line * 2), StructuredCpuRegister::X)
			&& b.TrySchedulePlayfieldWrite(E_COLBAK,
				static_cast<unsigned char>(42 + line * 2), StructuredCpuRegister::Y),
			"paired retained fixture writes must fit");
		stateA = a.OutgoingState();
		stateB = b.OutgoingState();
	}
	StructuredBeamOptions options;
	options.width = 32;
	options.repair_cost_per_pixel = 0.0;
	StructuredPairedWindowProblem problem;
	Require(ExtractStructuredPairedWindowProblem(
		baselineA, baselineB, 1, 2, 1, options, problem),
		"matching retained A/B schedules must extract a paired problem");
	Require(problem.lines.size() == 2 && problem.lines[0].size() == 1
		&& problem.lines[0][0].values.size() == 9,
		"radius-one extraction must form the retained/neighbor Cartesian choices");
	const auto& first = problem.lines[0][0];
	Require(first.target_a == E_COLOR0 && first.target_b == E_COLBAK
		&& first.values[0].value_a == 20 && first.values[0].value_b == 44,
		"the retained A/B pair must remain the explicit first control choice");
	Require(problem.incoming_a.x == 18 && problem.incoming_b.y == 42
		&& problem.required_outgoing_a.x == 22
		&& problem.required_outgoing_b.y == 46,
		"paired extraction must recover independent incoming and outgoing closure");

	StructuredPairedWindowResult retained = SearchStructuredPairedWindowBeam(
		problem.incoming_a, problem.incoming_b, problem.lines, options,
		&problem.required_outgoing_a, &problem.required_outgoing_b);
	Require(retained.feasible,
		"an extracted paired problem must retain at least one state-closed solution");

	StructuredLineBuilder extra(baselineB.raster_lines[1], stateB);
	Require(extra.TrySchedulePlayfieldWrite(E_COLOR2, 60, StructuredCpuRegister::A),
		"mismatched paired schedule fixture must fit");
	Require(extra.TrySchedulePlayfieldWrite(E_COLOR1, 62, StructuredCpuRegister::A),
		"second mismatched paired schedule fixture must fit");
	Require(ExtractStructuredPairedWindowProblem(
		baselineA, baselineB, 1, 1, 1, options, problem),
		"mismatched retained A/B slot counts must align through one-sided writes");
	Require(problem.lines[0].size() == 2
		&& problem.lines[0][1].write_a == false
		&& problem.lines[0][1].write_b == true,
		"the unmatched B transition must remain an explicit one-sided slot");
}

void TestPairedBeamMeasuredAgainstAlternatingBaseline()
{
	const rgb colors[12] = {
		{130, 32, 68, 0}, {230, 253, 60, 0}, {107, 194, 241, 0},
		{14, 249, 48, 0}, {1, 221, 199, 0}, {117, 136, 228, 0},
		{15, 162, 52, 0}, {4, 13, 11, 0}, {216, 110, 195, 0},
		{224, 113, 14, 0}, {176, 119, 253, 0}, {235, 112, 118, 0},
	};
	rgb palette[128]{};
	for (int index = 0; index < 12; ++index)
		palette[index] = colors[index];
	unsigned wins = 0;
	double alternatingTotal = 0.0;
	double jointTotal = 0.0;
	unsigned state = 1;
	for (int fixture = 0; fixture < 256; ++fixture)
	{
		auto nextByte = [&state]() {
			state = state * 1664525u + 1013904223u;
			return static_cast<unsigned char>(state >> 24);
		};
		std::vector<screen_line> reference(1);
		reference[0].Resize(1);
		reference[0][0] = {nextByte(), nextByte(), nextByte(), 0};
		DualFrameObjective objective;
		objective.Init(1, 1, reference.data(), palette, 0.2, 0.1);
		auto pairCost = [&objective](int a, int b) {
			unsigned char av = static_cast<unsigned char>(a);
			unsigned char bv = static_cast<unsigned char>(b);
			const unsigned char* ar[1] = {&av};
			const unsigned char* br[1] = {&bv};
			return objective.Score(ar, br);
		};
		int a = 0;
		for (int value = 1; value < 12; ++value)
			if (pairCost(value, value).total < pairCost(a, a).total)
				a = value;
		int b = a;
		for (int iteration = 0; iteration < 12; ++iteration)
		{
			int bestA = a;
			for (int value = 0; value < 12; ++value)
				if (pairCost(value, b).total < pairCost(bestA, b).total)
					bestA = value;
			a = bestA;
			int bestB = b;
			for (int value = 0; value < 12; ++value)
				if (pairCost(a, value).total < pairCost(a, bestB).total)
					bestB = value;
			b = bestB;
		}
		const double alternating = pairCost(a, b).total;
		StructuredPairedSegmentCandidate segment;
		segment.target_a = E_COLOR0;
		segment.target_b = E_COLBAK;
		segment.desired_pixel_a = StructuredInstructionEffectPixel(2);
		segment.desired_pixel_b = StructuredInstructionEffectPixel(2);
		for (int valueA = 0; valueA < 12; ++valueA)
			for (int valueB = 0; valueB < 12; ++valueB)
			{
				const DualFrameScore score = pairCost(valueA, valueB);
				segment.values.push_back({static_cast<unsigned char>(valueA * 2),
					static_cast<unsigned char>(valueB * 2), score.visual, score.flicker});
			}
		StructuredBeamOptions options;
		options.width = segment.values.size();
		options.repair_cost_per_pixel = 0.0;
		const StructuredPairedBeamResult joint = SearchStructuredPairedLineBeam(
			{}, {}, {segment}, options);
		Require(joint.feasible
			&& ValidateRasterLine(joint.frame_a.line) == E_RASTER_VALID
			&& ValidateRasterLine(joint.frame_b.line) == E_RASTER_VALID,
			"joint fixture finalists must be independently legal");
		Require(joint.total_cost <= alternating + 1e-7,
			"joint beam must not lose to alternating coordinate descent");
		if (joint.total_cost + 1e-7 < alternating)
			++wins;
		alternatingTotal += alternating;
		jointTotal += joint.total_cost;
	}
	Require(wins > 0, "fixture matrix must expose an alternating local minimum");
	std::cout << "Phase7 paired fixtures: wins=" << wins << "/256"
		<< " alternating_total=" << alternatingTotal
		<< " joint_total=" << jointTotal << '\n';
}

void TestPairedWindowCarriesAndClosesIndependentCpuStates()
{
	const int loadedPixel = StructuredInstructionEffectPixel(2);
	const int carriedPixel = StructuredInstructionEffectPixel(0);
	StructuredPairedSegmentCandidate first;
	first.target_a = E_COLOR0;
	first.target_b = E_COLBAK;
	first.desired_pixel_a = loadedPixel;
	first.desired_pixel_b = loadedPixel;
	first.values = {{18, 42, 3.0, 2.0}};
	StructuredPairedSegmentCandidate second;
	second.target_a = E_COLOR1;
	second.target_b = E_COLOR2;
	second.desired_pixel_a = carriedPixel;
	second.desired_pixel_b = carriedPixel;
	second.values = {{18, 42, 5.0, 7.0}};
	StructuredCpuState closedA{9, 8, 7};
	StructuredCpuState closedB{6, 5, 4};
	StructuredBeamOptions options;
	options.width = 16;
	options.repair_cost_per_pixel = 0.0;
	const StructuredPairedWindowResult result = SearchStructuredPairedWindowBeam(
		{}, {}, {{first}, {second}}, options, &closedA, &closedB);
	Require(result.feasible && result.frame_a.lines.size() == 2
		&& result.frame_b.lines.size() == 2,
		"paired window must materialize both lines in both frames");
	Require(result.visual_cost == 8.0 && result.flicker_cost == 9.0
		&& result.total_cost == 17.0,
		"paired window must preserve separate additive objective components");
	Require(result.frame_a.transitions[1][0].value == 18
		&& result.frame_b.transitions[1][0].value == 42,
		"paired window must carry independent A/B values between lines");
	Require(result.frame_a.outgoing.a == closedA.a
		&& result.frame_a.outgoing.x == closedA.x
		&& result.frame_a.outgoing.y == closedA.y
		&& result.frame_b.outgoing.a == closedB.a
		&& result.frame_b.outgoing.x == closedB.x
		&& result.frame_b.outgoing.y == closedB.y,
		"paired window must restore independent required outgoing CPU states");
	for (const raster_line& line : result.frame_a.lines)
		Require(ValidateRasterLine(line) == E_RASTER_VALID,
			"every frame A window line must remain legal");
	for (const raster_line& line : result.frame_b.lines)
		Require(ValidateRasterLine(line) == E_RASTER_VALID,
			"every frame B window line must remain legal");
	raster_picture baselineA;
	raster_picture baselineB;
	baselineA.raster_lines.resize(3);
	baselineB.raster_lines.resize(3);
	for (size_t line = 0; line < 3; ++line)
	{
		StructuredLineBuilder builderA(baselineA.raster_lines[line], {});
		StructuredLineBuilder builderB(baselineB.raster_lines[line], {});
	}
	raster_picture candidateA;
	raster_picture candidateB;
	Require(BuildStructuredPairedWindowComparisonCandidates(baselineA, baselineB,
			1, result, candidateA, candidateB),
		"paired comparison must patch both retained programs transactionally");
	Require(candidateA.raster_lines[0].instructions.empty()
		&& candidateB.raster_lines[0].instructions.empty()
		&& candidateA.raster_lines[1].instructions
			== result.frame_a.lines[0].instructions
		&& candidateB.raster_lines[2].instructions
			== result.frame_b.lines[1].instructions,
		"paired comparison must replace only the requested A/B window");
}
}

int main()
{
	create_cycles_table();
	TestPlayfieldScheduleCarriesCpuState();
	TestScheduleRejectsInvalidAndOverBudgetTransitions();
	TestInstructionOffsetsUseExactAnticTable();
	TestPositionedWriteUsesExactOrNearestEarlierSlot();
	TestPositionedWriteSelectsCarriedRegisterAndFailsAtomically();
	TestBeamSelectsFixedReferenceMinimumAndMaterializesLegalLine();
	TestBeamRejectsUnrepairablePartialStates();
	TestWindowBeamCarriesDiverseCpuStatesBetweenLines();
	TestSourceSegmentsMatchExhaustiveFixedReferenceMinimum();
	TestComparisonCandidatePatchesOnlyRequestedWindow();
	TestReplayWindowExtractsStochasticCpuStateAndPlayfieldStores();
	TestSourceWindowAddsRankedAlternatesAndPreservesClosure();
	TestSourceWindowRanksAcrossInterveningTargetWrites();
	TestIndependentDualProgramsRemainLegal();
	TestPairedProblemExtractionPreservesRetainedControlAndClosure();
	TestPairedBeamMeasuredAgainstAlternatingBaseline();
	TestPairedWindowCarriesAndClosesIndependentCpuStates();
	std::cout << "Structured solver tests passed\n";
	return 0;
}

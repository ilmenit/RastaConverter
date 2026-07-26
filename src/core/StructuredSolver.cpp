#include "StructuredSolver.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <tuple>

namespace
{
e_raster_instruction LoadOpcode(StructuredCpuRegister source)
{
	switch (source)
	{
	case StructuredCpuRegister::A: return E_RASTER_LDA;
	case StructuredCpuRegister::X: return E_RASTER_LDX;
	case StructuredCpuRegister::Y: return E_RASTER_LDY;
	}
	assert(false);
	return E_RASTER_LDA;
}

e_raster_instruction StoreOpcode(StructuredCpuRegister source)
{
	switch (source)
	{
	case StructuredCpuRegister::A: return E_RASTER_STA;
	case StructuredCpuRegister::X: return E_RASTER_STX;
	case StructuredCpuRegister::Y: return E_RASTER_STY;
	}
	assert(false);
	return E_RASTER_STA;
}

bool AppendCpuStateRestoration(raster_line& line,
	StructuredCpuState& state, const StructuredCpuState& required)
{
	const std::pair<StructuredCpuRegister, unsigned char> restores[] = {
		{StructuredCpuRegister::A, required.a},
		{StructuredCpuRegister::X, required.x},
		{StructuredCpuRegister::Y, required.y},
	};
	for (const auto& restore : restores)
	{
		unsigned char* current = restore.first == StructuredCpuRegister::A
			? &state.a : (restore.first == StructuredCpuRegister::X ? &state.x : &state.y);
		if (*current == restore.second)
			continue;
		SRasterInstruction instruction{};
		instruction.loose.instruction = LoadOpcode(restore.first);
		instruction.loose.target = E_TARGET_MAX;
		instruction.loose.value = restore.second;
		const int cycles = GetInstructionCycles(instruction);
		if (line.cycles + cycles > raster_program_cycle_limit)
			return false;
		line.instructions.push_back(instruction);
		line.cycles += cycles;
		*current = restore.second;
	}
	line.rehash();
	line.cache_key = nullptr;
	return ValidateRasterLine(line) == E_RASTER_VALID;
}
}

bool IsPlayfieldTarget(e_target target)
{
	return target >= E_COLOR0 && target <= E_COLBAK;
}

namespace
{
bool IsPmgTarget(e_target target)
{
	return target >= E_COLPM0 && target <= E_HPOSP3;
}
}

int StructuredInstructionEffectPixel(int startCycle)
{
	assert(startCycle >= 0 && startCycle <= raster_program_cycle_limit);
	// Evaluator instructions execute while offset < x, immediately before x is
	// rendered. Therefore an instruction at offset N first affects pixel N+1.
	return screen_cycles[startCycle].offset + 1;
}

bool BuildStructuredSourceSegment(
	e_target target,
	int desiredPixel,
	int spanBegin,
	int spanEnd,
	const std::vector<unsigned char>& values,
	const distance_t* const* paletteErrors,
	int sourceWidth,
	StructuredSegmentCandidate& candidate)
{
	if (!IsPlayfieldTarget(target) || values.empty() || paletteErrors == nullptr
		|| sourceWidth <= 0 || spanBegin < 0 || spanEnd <= spanBegin
		|| spanEnd > sourceWidth)
		return false;
	StructuredSegmentCandidate built;
	built.target = target;
	built.desired_pixel = desiredPixel;
	built.values.reserve(values.size());
	for (unsigned char value : values)
	{
		const distance_t* errors = paletteErrors[value >> 1];
		if (errors == nullptr)
			return false;
		distance_accum_t cost = 0;
		for (int pixel = spanBegin; pixel < spanEnd; ++pixel)
			cost += errors[pixel];
		built.values.push_back({value, static_cast<double>(cost)});
	}
	candidate = std::move(built);
	return true;
}

StructuredLineBuilder::StructuredLineBuilder(
	raster_line& line, const StructuredCpuState& incoming)
	: m_line(line)
	, m_state(incoming)
{
	m_line.instructions.clear();
	m_line.cycles = 0;
	m_line.hash = 0;
	m_line.cache_key = nullptr;
}

bool StructuredLineBuilder::CanAppend(int cycles) const
{
	return cycles >= 0 && m_line.cycles + cycles <= raster_program_cycle_limit;
}

unsigned char& StructuredLineBuilder::RegisterValue(StructuredCpuRegister source)
{
	switch (source)
	{
	case StructuredCpuRegister::A: return m_state.a;
	case StructuredCpuRegister::X: return m_state.x;
	case StructuredCpuRegister::Y: return m_state.y;
	}
	assert(false);
	return m_state.a;
}

void StructuredLineBuilder::FinalizeAppend()
{
	m_line.rehash();
	m_line.cache_key = nullptr;
}

void StructuredLineBuilder::AppendLoad(
	StructuredCpuRegister source, unsigned char value)
{
	SRasterInstruction instruction{};
	instruction.loose.instruction = LoadOpcode(source);
	instruction.loose.target = E_TARGET_MAX;
	instruction.loose.value = value;
	m_line.instructions.push_back(instruction);
	m_line.cycles += GetInstructionCycles(instruction);
	RegisterValue(source) = value;
}

void StructuredLineBuilder::AppendStore(
	StructuredCpuRegister source, e_target target)
{
	SRasterInstruction instruction{};
	instruction.loose.instruction = StoreOpcode(source);
	instruction.loose.target = target;
	m_line.instructions.push_back(instruction);
	m_line.cycles += GetInstructionCycles(instruction);
}

bool StructuredLineBuilder::TrySchedulePlayfieldWrite(
	e_target target, unsigned char value, StructuredCpuRegister source)
{
	if (!IsPlayfieldTarget(target))
		return false;
	const bool loadRequired = RegisterValue(source) != value;
	const int requiredCycles = loadRequired ? 6 : 4;
	if (!CanAppend(requiredCycles))
		return false;
	if (loadRequired)
		AppendLoad(source, value);
	AppendStore(source, target);
	FinalizeAppend();
	return true;
}

StructuredWriteResult StructuredLineBuilder::TrySchedulePlayfieldWriteAtOrBefore(
	const StructuredWriteRequest& request)
{
	StructuredWriteResult result;
	if (!IsPlayfieldTarget(request.target))
		return result;

	struct Candidate
	{
		StructuredCpuRegister source;
		bool loadRequired;
		int nopCount;
		int actualPixel;
		int addedCycles;
	};
	Candidate best{};
	bool found = false;
	const StructuredCpuRegister sources[] = {
		StructuredCpuRegister::A,
		StructuredCpuRegister::X,
		StructuredCpuRegister::Y,
	};
	for (StructuredCpuRegister source : sources)
	{
		const bool loadRequired = RegisterValue(source) != request.value;
		const int loadCycles = loadRequired ? 2 : 0;
		for (int nopCount = 0; ; ++nopCount)
		{
			const int addedCycles = nopCount * 2 + loadCycles + 4;
			if (!CanAppend(addedCycles))
				break;
			const int storeStartCycle = m_line.cycles + nopCount * 2 + loadCycles;
			const int actualPixel = StructuredInstructionEffectPixel(storeStartCycle);
			if (actualPixel > request.desired_pixel)
				continue;
			if (!found || actualPixel > best.actualPixel
				|| (actualPixel == best.actualPixel && addedCycles < best.addedCycles))
			{
				best = {source, loadRequired, nopCount, actualPixel, addedCycles};
				found = true;
			}
		}
	}
	if (!found)
		return result;

	for (int index = 0; index < best.nopCount; ++index)
	{
		const bool appended = TryAppendNop();
		assert(appended);
	}
	if (best.loadRequired)
		AppendLoad(best.source, request.value);
	AppendStore(best.source, request.target);
	FinalizeAppend();

	result.feasible = true;
	result.repaired = best.actualPixel != request.desired_pixel;
	result.actual_pixel = best.actualPixel;
	result.source = best.source;
	return result;
}

StructuredWriteResult StructuredLineBuilder::TryScheduleFixedWriteAtCycle(
	const StructuredWriteRequest& request)
{
	StructuredWriteResult result;
	if (!IsPmgTarget(request.target) || request.exact_store_cycle < m_line.cycles
		|| request.exact_store_cycle > raster_program_cycle_limit - 4)
		return result;
	const StructuredCpuRegister sources[] = {
		StructuredCpuRegister::A,
		StructuredCpuRegister::X,
		StructuredCpuRegister::Y,
	};
	for (StructuredCpuRegister source : sources)
	{
		const bool loadRequired = RegisterValue(source) != request.value;
		const int loadCycles = loadRequired ? 2 : 0;
		const int padding = request.exact_store_cycle - m_line.cycles - loadCycles;
		if (padding < 0 || padding % 2 != 0)
			continue;
		for (int cycle = 0; cycle < padding; cycle += 2)
		{
			const bool appended = TryAppendNop();
			assert(appended);
		}
		if (loadRequired)
			AppendLoad(source, request.value);
		AppendStore(source, request.target);
		FinalizeAppend();
		result.feasible = true;
		result.actual_pixel = StructuredInstructionEffectPixel(
			request.exact_store_cycle);
		result.source = source;
		return result;
	}
	return result;
}

bool StructuredLineBuilder::TryAppendNop()
{
	if (!CanAppend(2))
		return false;
	SRasterInstruction instruction{};
	instruction.loose.instruction = E_RASTER_NOP;
	instruction.loose.target = E_TARGET_MAX;
	m_line.instructions.push_back(instruction);
	m_line.cycles += GetInstructionCycles(instruction);
	FinalizeAppend();
	return true;
}

int StructuredLineBuilder::NextInstructionOffset() const
{
	assert(m_line.cycles >= 0 && m_line.cycles <= raster_program_cycle_limit);
	return screen_cycles[m_line.cycles].offset;
}

namespace
{
struct BeamState
{
	double cost = 0.0;
	double referenceCost = 0.0;
	raster_line line;
	StructuredCpuState outgoing;
	std::vector<StructuredWriteRequest> transitions;
};

bool BeamStateLess(const BeamState& left, const BeamState& right)
{
	if (left.cost != right.cost)
		return left.cost < right.cost;
	if (left.transitions.size() != right.transitions.size())
		return left.transitions.size() < right.transitions.size();
	for (std::size_t index = 0; index < left.transitions.size(); ++index)
	{
		const auto& a = left.transitions[index];
		const auto& b = right.transitions[index];
		const auto aKey = std::make_tuple(a.desired_pixel, a.exact_store_cycle,
			a.target, a.value);
		const auto bKey = std::make_tuple(b.desired_pixel, b.exact_store_cycle,
			b.target, b.value);
		if (aKey != bKey)
			return aKey < bKey;
	}
	return std::tie(left.outgoing.a, left.outgoing.x, left.outgoing.y)
		< std::tie(right.outgoing.a, right.outgoing.x, right.outgoing.y);
}

bool MaterializeBeamState(
	const StructuredCpuState& incoming,
	const std::vector<StructuredWriteRequest>& transitions,
	double referenceCost,
	double repairCostPerPixel,
	BeamState& state)
{
	raster_line line;
	StructuredLineBuilder builder(line, incoming);
	double cost = referenceCost;
	for (const auto& transition : transitions)
	{
		const StructuredWriteResult result = transition.exact_store_cycle >= 0
			? builder.TryScheduleFixedWriteAtCycle(transition)
			: builder.TrySchedulePlayfieldWriteAtOrBefore(transition);
		if (!result.feasible)
			return false;
		if (transition.exact_store_cycle < 0)
			cost += repairCostPerPixel
				* static_cast<double>(transition.desired_pixel - result.actual_pixel);
	}
	if (ValidateRasterLine(line) != E_RASTER_VALID)
		return false;
	state.cost = cost;
	state.referenceCost = referenceCost;
	state.line = std::move(line);
	state.outgoing = builder.OutgoingState();
	state.transitions = transitions;
	return true;
}

std::vector<BeamState> SearchLineBeamStates(
	const StructuredCpuState& incoming,
	const std::vector<StructuredSegmentCandidate>& segments,
	const StructuredBeamOptions& options)
{
	if (options.width == 0 || options.repair_cost_per_pixel < 0.0)
		return {};
	std::vector<BeamState> beam(1);
	beam[0].outgoing = incoming;
	for (const auto& segment : segments)
	{
		if ((!IsPlayfieldTarget(segment.target)
			&& !(segment.exact_store_cycle >= 0 && IsPmgTarget(segment.target)))
			|| segment.values.empty())
			return {};
		std::vector<BeamState> expanded;
		for (const BeamState& parent : beam)
		{
			for (const auto& value : segment.values)
			{
				std::vector<StructuredWriteRequest> transitions = parent.transitions;
				transitions.push_back({segment.target, value.value,
					segment.desired_pixel, segment.exact_store_cycle});
				BeamState child;
				if (MaterializeBeamState(incoming, transitions,
					parent.referenceCost + value.reference_cost,
					options.repair_cost_per_pixel, child))
					expanded.push_back(std::move(child));
			}
		}
		if (expanded.empty())
			return {};
		std::sort(expanded.begin(), expanded.end(), BeamStateLess);

		std::vector<BeamState> next;
		std::vector<bool> selected(expanded.size(), false);
		std::map<std::tuple<unsigned char, unsigned char, unsigned char>, std::size_t>
			stateCounts;
		for (std::size_t index = 0;
			index < expanded.size() && next.size() < options.width; ++index)
		{
			const auto key = std::make_tuple(expanded[index].outgoing.a,
				expanded[index].outgoing.x, expanded[index].outgoing.y);
			if (stateCounts[key] >= options.diversity_per_state)
				continue;
			++stateCounts[key];
			selected[index] = true;
			next.push_back(expanded[index]);
		}
		for (std::size_t index = 0;
			index < expanded.size() && next.size() < options.width; ++index)
		{
			if (!selected[index])
				next.push_back(expanded[index]);
		}
		std::sort(next.begin(), next.end(), BeamStateLess);
		beam = std::move(next);
	}
	return beam;
}
}

StructuredBeamResult SearchStructuredLineBeam(
	const StructuredCpuState& incoming,
	const std::vector<StructuredSegmentCandidate>& segments,
	const StructuredBeamOptions& options)
{
	StructuredBeamResult result;
	const std::vector<BeamState> beam =
		SearchLineBeamStates(incoming, segments, options);
	if (beam.empty())
		return result;
	const BeamState& best = beam.front();
	result.feasible = true;
	result.cost = best.cost;
	result.line = best.line;
	result.outgoing = best.outgoing;
	result.transitions = best.transitions;
	return result;
}

StructuredPairedBeamResult SearchStructuredPairedLineBeam(
	const StructuredCpuState& incomingA,
	const StructuredCpuState& incomingB,
	const std::vector<StructuredPairedSegmentCandidate>& segments,
	const StructuredBeamOptions& options)
{
	StructuredPairedBeamResult result;
	if (options.width == 0 || options.repair_cost_per_pixel < 0.0)
		return result;
	struct PairedState
	{
		double visual = 0.0;
		double flicker = 0.0;
		BeamState a;
		BeamState b;
	};
	std::vector<PairedState> beam(1);
	for (const auto& segment : segments)
	{
		if ((!segment.write_a && !segment.write_b) || segment.values.empty()
			|| (segment.write_a && !IsPlayfieldTarget(segment.target_a))
			|| (segment.write_b && !IsPlayfieldTarget(segment.target_b)))
			return result;
		std::vector<PairedState> expanded;
		for (const PairedState& parent : beam)
			for (const auto& value : segment.values)
			{
				PairedState child;
				std::vector<StructuredWriteRequest> transitionsA = parent.a.transitions;
				std::vector<StructuredWriteRequest> transitionsB = parent.b.transitions;
				if (segment.write_a) transitionsA.push_back({segment.target_a,
					value.value_a, segment.desired_pixel_a, segment.exact_store_cycle_a});
				if (segment.write_b) transitionsB.push_back({segment.target_b,
					value.value_b, segment.desired_pixel_b, segment.exact_store_cycle_b});
				child.visual = parent.visual + value.visual_cost;
				child.flicker = parent.flicker + value.flicker_cost;
				if (MaterializeBeamState(incomingA, transitionsA,
					child.visual + child.flicker,
					options.repair_cost_per_pixel, child.a)
					&& MaterializeBeamState(incomingB, transitionsB, 0.0,
						options.repair_cost_per_pixel, child.b))
					expanded.push_back(std::move(child));
			}
		if (expanded.empty())
			return result;
		auto less = [](const PairedState& left, const PairedState& right) {
			const double leftCost = left.a.cost + left.b.cost;
			const double rightCost = right.a.cost + right.b.cost;
			if (leftCost != rightCost)
				return leftCost < rightCost;
			return std::tie(left.a.outgoing.a, left.a.outgoing.x, left.a.outgoing.y,
				left.b.outgoing.a, left.b.outgoing.x, left.b.outgoing.y)
				< std::tie(right.a.outgoing.a, right.a.outgoing.x, right.a.outgoing.y,
					right.b.outgoing.a, right.b.outgoing.x, right.b.outgoing.y);
		};
		std::sort(expanded.begin(), expanded.end(), less);
		if (expanded.size() > options.width)
			expanded.resize(options.width);
		beam = std::move(expanded);
	}
	if (beam.empty())
		return result;
	const PairedState& best = beam.front();
	result.feasible = true;
	result.visual_cost = best.visual;
	result.flicker_cost = best.flicker;
	result.total_cost = best.a.cost + best.b.cost;
	result.frame_a = {true, best.a.cost, best.a.line, best.a.outgoing,
		best.a.transitions};
	result.frame_b = {true, best.b.cost, best.b.line, best.b.outgoing,
		best.b.transitions};
	return result;
}

StructuredPairedWindowResult SearchStructuredPairedWindowBeam(
	const StructuredCpuState& incomingA,
	const StructuredCpuState& incomingB,
	const std::vector<std::vector<StructuredPairedSegmentCandidate>>& lines,
	const StructuredBeamOptions& options,
	const StructuredCpuState* requiredOutgoingA,
	const StructuredCpuState* requiredOutgoingB)
{
	StructuredPairedWindowResult result;
	if (options.width == 0 || options.repair_cost_per_pixel < 0.0 || lines.empty())
		return result;
	struct WindowState
	{
		double visual = 0.0;
		double flicker = 0.0;
		StructuredWindowResult a;
		StructuredWindowResult b;
	};
	std::vector<WindowState> beam(1);
	beam[0].a.feasible = beam[0].b.feasible = true;
	beam[0].a.outgoing = incomingA;
	beam[0].b.outgoing = incomingB;
	for (const auto& line : lines)
	{
		std::vector<WindowState> expanded;
		for (const WindowState& parent : beam)
		{
			// Expand the bounded paired-line beam here so the window search can
			// retain multiple outgoing A/B states, rather than only the public
			// single-line API's best finalist.
			struct LineState
			{
				double visual = 0.0;
				double flicker = 0.0;
				BeamState a;
				BeamState b;
			};
			std::vector<LineState> lineBeam(1);
			for (const auto& segment : line)
			{
				if ((!segment.write_a && !segment.write_b) || segment.values.empty()
					|| (segment.write_a && !IsPlayfieldTarget(segment.target_a)
						&& !IsPmgTarget(segment.target_a))
					|| (segment.write_b && !IsPlayfieldTarget(segment.target_b)
						&& !IsPmgTarget(segment.target_b)))
					return result;
				std::vector<LineState> lineExpanded;
				for (const LineState& lineParent : lineBeam)
					for (const auto& value : segment.values)
					{
						LineState child;
						auto transitionsA = lineParent.a.transitions;
						auto transitionsB = lineParent.b.transitions;
						if (segment.write_a) transitionsA.push_back({segment.target_a,
							value.value_a, segment.desired_pixel_a,
							segment.exact_store_cycle_a});
						if (segment.write_b) transitionsB.push_back({segment.target_b,
							value.value_b, segment.desired_pixel_b,
							segment.exact_store_cycle_b});
						child.visual = lineParent.visual + value.visual_cost;
						child.flicker = lineParent.flicker + value.flicker_cost;
						if (MaterializeBeamState(parent.a.outgoing, transitionsA,
							child.visual + child.flicker,
							options.repair_cost_per_pixel, child.a)
							&& MaterializeBeamState(parent.b.outgoing, transitionsB, 0.0,
								options.repair_cost_per_pixel, child.b))
							lineExpanded.push_back(std::move(child));
					}
				if (lineExpanded.empty())
					return result;
				auto lineLess = [](const LineState& left, const LineState& right) {
					const double lc = left.a.cost + left.b.cost;
					const double rc = right.a.cost + right.b.cost;
					if (lc != rc) return lc < rc;
					return std::tie(left.a.outgoing.a, left.a.outgoing.x, left.a.outgoing.y,
						left.b.outgoing.a, left.b.outgoing.x, left.b.outgoing.y)
						< std::tie(right.a.outgoing.a, right.a.outgoing.x, right.a.outgoing.y,
							right.b.outgoing.a, right.b.outgoing.x, right.b.outgoing.y);
				};
				std::sort(lineExpanded.begin(), lineExpanded.end(), lineLess);
				if (lineExpanded.size() > options.width) lineExpanded.resize(options.width);
				lineBeam = std::move(lineExpanded);
			}
			for (const LineState& finalist : lineBeam)
			{
				WindowState child = parent;
				child.visual += finalist.visual;
				child.flicker += finalist.flicker;
				child.a.cost += finalist.a.cost;
				child.b.cost += finalist.b.cost;
				child.a.lines.push_back(finalist.a.line);
				child.b.lines.push_back(finalist.b.line);
				child.a.transitions.push_back(finalist.a.transitions);
				child.b.transitions.push_back(finalist.b.transitions);
				child.a.outgoing = finalist.a.outgoing;
				child.b.outgoing = finalist.b.outgoing;
				expanded.push_back(std::move(child));
			}
		}
		if (expanded.empty()) return result;
		auto less = [](const WindowState& left, const WindowState& right) {
			const double lc = left.a.cost + left.b.cost;
			const double rc = right.a.cost + right.b.cost;
			if (lc != rc) return lc < rc;
			return std::tie(left.a.outgoing.a, left.a.outgoing.x, left.a.outgoing.y,
				left.b.outgoing.a, left.b.outgoing.x, left.b.outgoing.y)
				< std::tie(right.a.outgoing.a, right.a.outgoing.x, right.a.outgoing.y,
					right.b.outgoing.a, right.b.outgoing.x, right.b.outgoing.y);
		};
		std::sort(expanded.begin(), expanded.end(), less);
		std::vector<WindowState> next;
		std::vector<bool> selected(expanded.size(), false);
		std::map<std::tuple<unsigned char, unsigned char, unsigned char,
			unsigned char, unsigned char, unsigned char>, std::size_t> stateCounts;
		for (std::size_t index = 0;
			index < expanded.size() && next.size() < options.width; ++index)
		{
			const auto key = std::make_tuple(expanded[index].a.outgoing.a,
				expanded[index].a.outgoing.x, expanded[index].a.outgoing.y,
				expanded[index].b.outgoing.a, expanded[index].b.outgoing.x,
				expanded[index].b.outgoing.y);
			if (stateCounts[key] >= options.diversity_per_state) continue;
			++stateCounts[key];
			selected[index] = true;
			next.push_back(expanded[index]);
		}
		for (std::size_t index = 0;
			index < expanded.size() && next.size() < options.width; ++index)
			if (!selected[index]) next.push_back(expanded[index]);
		std::sort(next.begin(), next.end(), less);
		beam = std::move(next);
	}
	beam.erase(std::remove_if(beam.begin(), beam.end(), [&](WindowState& state) {
		if (requiredOutgoingA != nullptr
			&& !AppendCpuStateRestoration(state.a.lines.back(), state.a.outgoing,
				*requiredOutgoingA)) return true;
		if (requiredOutgoingB != nullptr
			&& !AppendCpuStateRestoration(state.b.lines.back(), state.b.outgoing,
				*requiredOutgoingB)) return true;
		return false;
	}), beam.end());
	if (beam.empty()) return result;
	const WindowState& best = beam.front();
	result.feasible = true;
	result.visual_cost = best.visual;
	result.flicker_cost = best.flicker;
	result.total_cost = best.a.cost + best.b.cost;
	result.frame_a = best.a;
	result.frame_b = best.b;
	return result;
}

StructuredWindowResult SearchStructuredWindowBeam(
	const StructuredCpuState& incoming,
	const std::vector<std::vector<StructuredSegmentCandidate>>& lines,
	const StructuredBeamOptions& options,
	const StructuredCpuState* requiredOutgoing)
{
	StructuredWindowResult result;
	if (options.width == 0 || options.repair_cost_per_pixel < 0.0)
		return result;
	struct WindowState
	{
		double cost = 0.0;
		std::vector<raster_line> lines;
		StructuredCpuState outgoing;
		std::vector<std::vector<StructuredWriteRequest>> transitions;
	};
	std::vector<WindowState> beam(1);
	beam[0].outgoing = incoming;
	for (const auto& lineSegments : lines)
	{
		std::vector<WindowState> expanded;
		for (const auto& parent : beam)
		{
			const std::vector<BeamState> lineBeam =
				SearchLineBeamStates(parent.outgoing, lineSegments, options);
			for (const auto& line : lineBeam)
			{
				WindowState child = parent;
				child.cost += line.cost;
				child.lines.push_back(line.line);
				child.outgoing = line.outgoing;
				child.transitions.push_back(line.transitions);
				expanded.push_back(std::move(child));
			}
		}
		if (expanded.empty())
			return result;
		auto less = [](const WindowState& left, const WindowState& right) {
			if (left.cost != right.cost)
				return left.cost < right.cost;
			return std::tie(left.outgoing.a, left.outgoing.x, left.outgoing.y)
				< std::tie(right.outgoing.a, right.outgoing.x, right.outgoing.y);
		};
		std::sort(expanded.begin(), expanded.end(), less);
		std::vector<WindowState> next;
		std::vector<bool> selected(expanded.size(), false);
		std::map<std::tuple<unsigned char, unsigned char, unsigned char>, std::size_t>
			stateCounts;
		for (std::size_t index = 0;
			index < expanded.size() && next.size() < options.width; ++index)
		{
			const auto key = std::make_tuple(expanded[index].outgoing.a,
				expanded[index].outgoing.x, expanded[index].outgoing.y);
			if (stateCounts[key] >= options.diversity_per_state)
				continue;
			++stateCounts[key];
			selected[index] = true;
			next.push_back(expanded[index]);
		}
		for (std::size_t index = 0;
			index < expanded.size() && next.size() < options.width; ++index)
			if (!selected[index])
				next.push_back(expanded[index]);
		std::sort(next.begin(), next.end(), less);
		beam = std::move(next);
	}

	if (requiredOutgoing != nullptr)
	{
		beam.erase(std::remove_if(beam.begin(), beam.end(),
			[requiredOutgoing](WindowState& state) {
				return state.lines.empty() || !AppendCpuStateRestoration(
					state.lines.back(), state.outgoing, *requiredOutgoing);
			}), beam.end());
		if (beam.empty())
			return result;
	}
	const WindowState& best = beam.front();
	result.feasible = true;
	result.cost = best.cost;
	result.lines = best.lines;
	result.outgoing = best.outgoing;
	result.transitions = best.transitions;
	return result;
}

bool BuildStructuredWindowComparisonCandidate(
	const raster_picture& baseline,
	std::size_t firstLine,
	const StructuredWindowResult& window,
	raster_picture& candidate)
{
	if (!window.feasible || window.lines.empty()
		|| firstLine > baseline.raster_lines.size()
		|| window.lines.size() > baseline.raster_lines.size() - firstLine
		|| ValidateRasterPicture(baseline) != E_RASTER_VALID)
		return false;
	for (const raster_line& line : window.lines)
		if (ValidateRasterLine(line) != E_RASTER_VALID)
			return false;
	raster_picture built = baseline;
	for (std::size_t index = 0; index < window.lines.size(); ++index)
	{
		built.raster_lines[firstLine + index] = window.lines[index];
		built.raster_lines[firstLine + index].cache_key = nullptr;
	}
	if (ValidateRasterPicture(built) != E_RASTER_VALID)
		return false;
	candidate = std::move(built);
	return true;
}

bool BuildStructuredPairedWindowComparisonCandidates(
	const raster_picture& baselineA,
	const raster_picture& baselineB,
	std::size_t firstLine,
	const StructuredPairedWindowResult& window,
	raster_picture& candidateA,
	raster_picture& candidateB)
{
	if (!window.feasible || !window.frame_a.feasible || !window.frame_b.feasible
		|| window.frame_a.lines.size() != window.frame_b.lines.size())
		return false;
	raster_picture builtA;
	raster_picture builtB;
	if (!BuildStructuredWindowComparisonCandidate(
			baselineA, firstLine, window.frame_a, builtA)
		|| !BuildStructuredWindowComparisonCandidate(
			baselineB, firstLine, window.frame_b, builtB)
		|| ValidateRasterPicture(builtA) != E_RASTER_VALID
		|| ValidateRasterPicture(builtB) != E_RASTER_VALID)
		return false;
	candidateA = std::move(builtA);
	candidateB = std::move(builtB);
	return true;
}

namespace
{
unsigned char& ExtractedRegister(
	StructuredCpuState& state, e_raster_instruction instruction)
{
	switch (instruction)
	{
	case E_RASTER_LDX:
	case E_RASTER_STX: return state.x;
	case E_RASTER_LDY:
	case E_RASTER_STY: return state.y;
	default: return state.a;
	}
}

void ReplayCpuLoads(const raster_line& line, StructuredCpuState& state)
{
	for (const SRasterInstruction& instruction : line.instructions)
	{
		if (instruction.loose.instruction == E_RASTER_LDA
			|| instruction.loose.instruction == E_RASTER_LDX
			|| instruction.loose.instruction == E_RASTER_LDY)
		{
			ExtractedRegister(state, static_cast<e_raster_instruction>(
				instruction.loose.instruction)) =
				instruction.loose.value;
		}
	}
}
}

bool ExtractStructuredReplayWindow(
	const raster_picture& baseline,
	std::size_t firstLine,
	std::size_t lineCount,
	const StructuredBeamOptions& options,
	StructuredWindowResult& window)
{
	if (lineCount == 0 || firstLine > baseline.raster_lines.size()
		|| lineCount > baseline.raster_lines.size() - firstLine
		|| ValidateRasterPicture(baseline) != E_RASTER_VALID)
		return false;
	StructuredCpuState incoming;
	for (std::size_t line = 0; line < firstLine; ++line)
		ReplayCpuLoads(baseline.raster_lines[line], incoming);

	std::vector<std::vector<StructuredSegmentCandidate>> segments(lineCount);
	StructuredCpuState state = incoming;
	for (std::size_t relative = 0; relative < lineCount; ++relative)
	{
		const raster_line& line = baseline.raster_lines[firstLine + relative];
		int cycle = 0;
		for (const SRasterInstruction& instruction : line.instructions)
		{
			const e_raster_instruction opcode =
				static_cast<e_raster_instruction>(instruction.loose.instruction);
			if (opcode == E_RASTER_LDA || opcode == E_RASTER_LDX
				|| opcode == E_RASTER_LDY)
			{
				ExtractedRegister(state, opcode) = instruction.loose.value;
			}
			else if (opcode == E_RASTER_STA || opcode == E_RASTER_STX
				|| opcode == E_RASTER_STY)
			{
				const e_target target =
					static_cast<e_target>(instruction.loose.target);
				if (IsPlayfieldTarget(target) || IsPmgTarget(target))
				{
					StructuredSegmentCandidate segment;
					segment.target = target;
					segment.desired_pixel = StructuredInstructionEffectPixel(cycle);
					segment.values.push_back({ExtractedRegister(state, opcode), 0.0});
					if (IsPmgTarget(target))
						segment.exact_store_cycle = cycle;
					segments[relative].push_back(std::move(segment));
				}
			}
			cycle += GetInstructionCycles(instruction);
		}
	}
	StructuredWindowResult built =
		SearchStructuredWindowBeam(incoming, segments, options, &state);
	if (!built.feasible)
		return false;
	window = std::move(built);
	return true;
}

bool ExtractStructuredPairedWindowProblem(
	const raster_picture& baselineA,
	const raster_picture& baselineB,
	std::size_t firstLine,
	std::size_t lineCount,
	std::size_t alternateRadius,
	const StructuredBeamOptions& options,
	StructuredPairedWindowProblem& problem)
{
	if (alternateRadius == 0)
		return false;
	StructuredWindowResult replayA;
	StructuredWindowResult replayB;
	if (!ExtractStructuredReplayWindow(
			baselineA, firstLine, lineCount, options, replayA)
		|| !ExtractStructuredReplayWindow(
			baselineB, firstLine, lineCount, options, replayB)
		|| replayA.transitions.size() != replayB.transitions.size())
		return false;

	StructuredPairedWindowProblem built;
	for (std::size_t line = 0; line < firstLine; ++line)
	{
		ReplayCpuLoads(baselineA.raster_lines[line], built.incoming_a);
		ReplayCpuLoads(baselineB.raster_lines[line], built.incoming_b);
	}
	built.required_outgoing_a = replayA.outgoing;
	built.required_outgoing_b = replayB.outgoing;
	built.lines.resize(lineCount);
	auto valuesAround = [alternateRadius](unsigned char retained) {
		std::vector<unsigned char> values{retained};
		for (std::size_t step = 1; step <= alternateRadius; ++step)
		{
			const int delta = static_cast<int>(step * 2);
			if (static_cast<int>(retained) - delta >= 0)
				values.push_back(static_cast<unsigned char>(retained - delta));
			if (static_cast<int>(retained) + delta <= 254)
				values.push_back(static_cast<unsigned char>(retained + delta));
		}
		return values;
	};
	for (std::size_t line = 0; line < lineCount; ++line)
	{
		const auto& transitionsA = replayA.transitions[line];
		const auto& transitionsB = replayB.transitions[line];
		if (transitionsA.empty() && transitionsB.empty())
			return false;
		const std::size_t slotCount = std::max(transitionsA.size(), transitionsB.size());
		for (std::size_t slot = 0; slot < slotCount; ++slot)
		{
			const bool writeA = slot < transitionsA.size();
			const bool writeB = slot < transitionsB.size();
			const StructuredWriteRequest a = writeA
				? transitionsA[slot] : StructuredWriteRequest{};
			const StructuredWriteRequest b = writeB
				? transitionsB[slot] : StructuredWriteRequest{};
			StructuredPairedSegmentCandidate segment;
			segment.write_a = writeA;
			segment.write_b = writeB;
			segment.target_a = a.target;
			segment.target_b = b.target;
			segment.desired_pixel_a = a.desired_pixel;
			segment.desired_pixel_b = b.desired_pixel;
			segment.exact_store_cycle_a = a.exact_store_cycle;
			segment.exact_store_cycle_b = b.exact_store_cycle;
			const auto valuesA = !writeA || IsPmgTarget(a.target)
				? std::vector<unsigned char>{a.value} : valuesAround(a.value);
			const auto valuesB = !writeB || IsPmgTarget(b.target)
				? std::vector<unsigned char>{b.value} : valuesAround(b.value);
			for (unsigned char valueA : valuesA)
				for (unsigned char valueB : valuesB)
					segment.values.push_back({valueA, valueB, 0.0, 0.0});
			built.lines[line].push_back(std::move(segment));
		}
	}
	problem = std::move(built);
	return true;
}

bool ExtractStructuredSourceWindow(
	const raster_picture& baseline,
	std::size_t firstLine,
	std::size_t lineCount,
	const distance_t* const* paletteErrors,
	int sourceWidth,
	std::size_t alternateCount,
	const StructuredBeamOptions& options,
	StructuredWindowResult& window,
	const unsigned char* const* targetRows)
{
	if (alternateCount == 0 || paletteErrors == nullptr || sourceWidth <= 0)
		return false;
	StructuredWindowResult replay;
	if (!ExtractStructuredReplayWindow(
		baseline, firstLine, lineCount, options, replay))
		return false;

	std::vector<std::vector<StructuredSegmentCandidate>> segments(lineCount);
	for (std::size_t line = 0; line < lineCount; ++line)
	{
		const auto& transitions = replay.transitions[line];
		for (std::size_t index = 0; index < transitions.size(); ++index)
		{
			const StructuredWriteRequest& transition = transitions[index];
			if (transition.exact_store_cycle >= 0)
			{
				StructuredSegmentCandidate fixed;
				fixed.target = transition.target;
				fixed.desired_pixel = transition.desired_pixel;
				fixed.values.push_back({transition.value, 0.0});
				fixed.exact_store_cycle = transition.exact_store_cycle;
				segments[line].push_back(std::move(fixed));
				continue;
			}
			const int spanBegin = std::max(0, std::min(sourceWidth,
				transition.desired_pixel));
			int nextPixel = sourceWidth;
			for (std::size_t next = index + 1; next < transitions.size(); ++next)
			{
				if (transitions[next].exact_store_cycle < 0
					&& (targetRows == nullptr || !options.target_lifetime_spans
						|| transitions[next].target == transition.target))
				{
					nextPixel = transitions[next].desired_pixel;
					break;
				}
			}
			const int spanEnd = std::max(spanBegin + 1,
				std::min(sourceWidth, nextPixel));
			if (spanBegin >= sourceWidth || spanEnd > sourceWidth)
				return false;

			struct RankedValue
			{
				unsigned char value;
				distance_accum_t cost;
			};
			std::vector<RankedValue> ranked;
			ranked.reserve(128);
			const std::size_t rowOffset = (firstLine + line)
				* static_cast<std::size_t>(sourceWidth);
			std::size_t ownedPixels = 0;
			if (targetRows != nullptr)
			{
				for (int pixel = spanBegin; pixel < spanEnd; ++pixel)
					if (targetRows[firstLine + line][pixel] == transition.target)
						++ownedPixels;
			}
			for (int palette = 0; palette < 128; ++palette)
			{
				if (paletteErrors[palette] == nullptr)
					return false;
				distance_accum_t cost = 0;
				for (int pixel = spanBegin; pixel < spanEnd; ++pixel)
					if (targetRows == nullptr
						|| targetRows[firstLine + line][pixel] == transition.target)
						cost += paletteErrors[palette][rowOffset + pixel];
				ranked.push_back({static_cast<unsigned char>(palette * 2), cost});
			}
			std::sort(ranked.begin(), ranked.end(), [](const RankedValue& left,
				const RankedValue& right) {
				if (left.cost != right.cost)
					return left.cost < right.cost;
				return left.value < right.value;
			});

			std::vector<unsigned char> values;
			values.push_back(transition.value);
			if (targetRows == nullptr || ownedPixels != 0)
			{
				for (const RankedValue& choice : ranked)
				{
					if (choice.value == transition.value)
						continue;
					values.push_back(choice.value);
					if (values.size() == alternateCount + 1)
						break;
				}
			}
			StructuredSegmentCandidate segment;
			segment.target = transition.target;
			segment.desired_pixel = transition.desired_pixel;
			for (unsigned char value : values)
			{
				const auto rankedValue = std::find_if(ranked.begin(), ranked.end(),
					[value](const RankedValue& choice) { return choice.value == value; });
				assert(rankedValue != ranked.end());
				segment.values.push_back(
					{value, static_cast<double>(rankedValue->cost)});
			}
			segments[line].push_back(std::move(segment));
		}
	}

	StructuredCpuState incoming;
	for (std::size_t line = 0; line < firstLine; ++line)
		ReplayCpuLoads(baseline.raster_lines[line], incoming);
	StructuredCpuState expected = incoming;
	for (std::size_t line = 0; line < lineCount; ++line)
		ReplayCpuLoads(baseline.raster_lines[firstLine + line], expected);
	StructuredWindowResult built =
		SearchStructuredWindowBeam(incoming, segments, options, &expected);
	if (!built.feasible)
		return false;
	window = std::move(built);
	return true;
}

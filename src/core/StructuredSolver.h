#ifndef STRUCTUREDSOLVER_H
#define STRUCTUREDSOLVER_H

#include "Program.h"
#include "Distance.h"

#include <cstddef>
#include <vector>

enum class StructuredCpuRegister
{
	A,
	X,
	Y,
};

struct StructuredCpuState
{
	unsigned char a = 0;
	unsigned char x = 0;
	unsigned char y = 0;
};

struct StructuredWriteRequest
{
	e_target target = E_COLBAK;
	unsigned char value = 0;
	// First rendered pixel that should observe the new register value.
	int desired_pixel = 0;
	// Nonnegative only for a retained fixed store that must start on this
	// exact CPU cycle (currently PMG color/position writes).
	int exact_store_cycle = -1;
};

struct StructuredWriteResult
{
	bool feasible = false;
	bool repaired = false;
	int actual_pixel = 0;
	StructuredCpuRegister source = StructuredCpuRegister::A;
};

// Deterministic legality boundary for the Phase 6 playfield-only solver.
// Search policies build ordinary raster_line objects through this class, then
// use the existing evaluator and program emitter without a second timing model.
class StructuredLineBuilder
{
public:
	StructuredLineBuilder(raster_line& line, const StructuredCpuState& incoming);

	bool TrySchedulePlayfieldWrite(
		e_target target, unsigned char value, StructuredCpuRegister source);
	StructuredWriteResult TrySchedulePlayfieldWriteAtOrBefore(
		const StructuredWriteRequest& request);
	StructuredWriteResult TryScheduleFixedWriteAtCycle(
		const StructuredWriteRequest& request);
	bool TryAppendNop();

	int Cycles() const { return m_line.cycles; }
	int RemainingCycles() const { return raster_program_cycle_limit - m_line.cycles; }
	int NextInstructionOffset() const;
	const StructuredCpuState& OutgoingState() const { return m_state; }

private:
	bool CanAppend(int cycles) const;
	unsigned char& RegisterValue(StructuredCpuRegister source);
	void AppendLoad(StructuredCpuRegister source, unsigned char value);
	void AppendStore(StructuredCpuRegister source, e_target target);
	void FinalizeAppend();

	raster_line& m_line;
	StructuredCpuState m_state;
};

bool IsPlayfieldTarget(e_target target);
int StructuredInstructionEffectPixel(int startCycle);

// A compact, fixed-reference transition choice.  The caller computes the
// visual cost against its immutable source/reference line; the structured
// search adds only the exact timing-repair displacement.
struct StructuredSegmentValue
{
	unsigned char value = 0;
	double reference_cost = 0.0;
};

struct StructuredSegmentCandidate
{
	e_target target = E_COLBAK;
	int desired_pixel = 0;
	std::vector<StructuredSegmentValue> values;
	int exact_store_cycle = -1;
};

// Converts a source-referenced palette error table into one segment's bounded
// value choices.  Register values are GTIA bytes; palette rows use value / 2,
// matching the evaluator's 128-color mode-15 convention.
bool BuildStructuredSourceSegment(
	e_target target,
	int desiredPixel,
	int spanBegin,
	int spanEnd,
	const std::vector<unsigned char>& values,
	const distance_t* const* paletteErrors,
	int sourceWidth,
	StructuredSegmentCandidate& candidate);

struct StructuredBeamOptions
{
	std::size_t width = 16;
	// Preserve this many candidates for each distinct outgoing A/X/Y state
	// before filling the remaining beam by global cost.
	std::size_t diversity_per_state = 1;
	double repair_cost_per_pixel = 1.0;
	// Experimental PAL ranking policy: keep a value's source span active until
	// the next write to the same target when evaluator ownership is available.
	bool target_lifetime_spans = false;
};

struct StructuredBeamResult
{
	bool feasible = false;
	double cost = 0.0;
	raster_line line;
	StructuredCpuState outgoing;
	std::vector<StructuredWriteRequest> transitions;
};

struct StructuredWindowResult
{
	bool feasible = false;
	double cost = 0.0;
	std::vector<raster_line> lines;
	StructuredCpuState outgoing;
	std::vector<std::vector<StructuredWriteRequest>> transitions;
};

struct StructuredPairedSegmentValue
{
	unsigned char value_a = 0;
	unsigned char value_b = 0;
	double visual_cost = 0.0;
	double flicker_cost = 0.0;
};

struct StructuredPairedSegmentCandidate
{
	bool write_a = true;
	bool write_b = true;
	e_target target_a = E_COLBAK;
	e_target target_b = E_COLBAK;
	int desired_pixel_a = 0;
	int desired_pixel_b = 0;
	int exact_store_cycle_a = -1;
	int exact_store_cycle_b = -1;
	std::vector<StructuredPairedSegmentValue> values;
};

struct StructuredPairedBeamResult
{
	bool feasible = false;
	double visual_cost = 0.0;
	double flicker_cost = 0.0;
	double total_cost = 0.0;
	StructuredBeamResult frame_a;
	StructuredBeamResult frame_b;
};

struct StructuredPairedWindowResult
{
	bool feasible = false;
	double visual_cost = 0.0;
	double flicker_cost = 0.0;
	double total_cost = 0.0;
	StructuredWindowResult frame_a;
	StructuredWindowResult frame_b;
};

struct StructuredPairedWindowProblem
{
	StructuredCpuState incoming_a;
	StructuredCpuState incoming_b;
	StructuredCpuState required_outgoing_a;
	StructuredCpuState required_outgoing_b;
	std::vector<std::vector<StructuredPairedSegmentCandidate>> lines;
};

// Joint bounded A/B line search. Pair costs are computed by the caller from
// complete evaluator-oracle renders; this layer only searches the paired
// choices and independently materializes both legal programs.
StructuredPairedBeamResult SearchStructuredPairedLineBeam(
	const StructuredCpuState& incomingA,
	const StructuredCpuState& incomingB,
	const std::vector<StructuredPairedSegmentCandidate>& segments,
	const StructuredBeamOptions& options = {});

// Extends the joint A/B search across a bounded scanline window. Both CPU
// states flow independently between lines, and optional outgoing states close
// the window back onto the retained programs with legal trailing loads.
StructuredPairedWindowResult SearchStructuredPairedWindowBeam(
	const StructuredCpuState& incomingA,
	const StructuredCpuState& incomingB,
	const std::vector<std::vector<StructuredPairedSegmentCandidate>>& lines,
	const StructuredBeamOptions& options = {},
	const StructuredCpuState* requiredOutgoingA = nullptr,
	const StructuredCpuState* requiredOutgoingB = nullptr);

// Bounded deterministic beam prototype for one scanline.  Every partial state
// is rematerialized through StructuredLineBuilder, so infeasible transitions
// are rejected by the shared timing boundary rather than approximated.
StructuredBeamResult SearchStructuredLineBeam(
	const StructuredCpuState& incoming,
	const std::vector<StructuredSegmentCandidate>& segments,
	const StructuredBeamOptions& options = {});

// Composes the same bounded beam across a short scanline window. Outgoing CPU
// states from every retained line candidate become the incoming states for the
// next line; cost remains additive against the caller's fixed references. A
// required outgoing state is restored with legal trailing loads when supplied.
StructuredWindowResult SearchStructuredWindowBeam(
	const StructuredCpuState& incoming,
	const std::vector<std::vector<StructuredSegmentCandidate>>& lines,
	const StructuredBeamOptions& options = {},
	const StructuredCpuState* requiredOutgoing = nullptr);

// Patches a feasible structured window into a complete stochastic baseline so
// both programs can be scored from identical initial registers and surrounding
// lines by the evaluator oracle.
bool BuildStructuredWindowComparisonCandidate(
	const raster_picture& baseline,
	std::size_t firstLine,
	const StructuredWindowResult& window,
	raster_picture& candidate);

// Transactionally patches both members of a paired result into independently
// retained programs. Each complete candidate passes the shared legality oracle.
bool BuildStructuredPairedWindowComparisonCandidates(
	const raster_picture& baselineA,
	const raster_picture& baselineB,
	std::size_t firstLine,
	const StructuredPairedWindowResult& window,
	raster_picture& candidateA,
	raster_picture& candidateB);

// Extracts a matching playfield-only window from an existing stochastic
// program. Loads are replayed to recover the exact CPU state; PMG stores and
// non-store instructions are omitted from the structured transition set.
bool ExtractStructuredReplayWindow(
	const raster_picture& baseline,
	std::size_t firstLine,
	std::size_t lineCount,
	const StructuredBeamOptions& options,
	StructuredWindowResult& window);

// Extracts matching playfield transition slots from retained A/B programs.
// Each slot includes the retained pair and deterministic neighboring GTIA
// values. Costs remain zero for the caller to populate from complete oracle
// renders. Unmatched slots become one-sided writes; PMG slots retain only their
// original value and exact store cycle.
bool ExtractStructuredPairedWindowProblem(
	const raster_picture& baselineA,
	const raster_picture& baselineB,
	std::size_t firstLine,
	std::size_t lineCount,
	std::size_t alternateRadius,
	const StructuredBeamOptions& options,
	StructuredPairedWindowProblem& problem);

// Source-referenced replay variant used by controlled fixtures. Each extracted
// stochastic transition keeps its retained value and adds the cheapest bounded
// GTIA alternatives for its target lifetime. With evaluator target ownership,
// that lifetime ends at the next write to the same target (or sourceWidth), and
// only pixels owned by that target contribute. Callers without ownership retain
// the older non-overlapping next-transition spans for controlled unit fixtures.
bool ExtractStructuredSourceWindow(
	const raster_picture& baseline,
	std::size_t firstLine,
	std::size_t lineCount,
	const distance_t* const* paletteErrors,
	int sourceWidth,
	std::size_t alternateCount,
	const StructuredBeamOptions& options,
	StructuredWindowResult& window,
	const unsigned char* const* targetRows = nullptr);

#endif

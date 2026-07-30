#include "Program.h"
#include <assert.h>
#include <algorithm>

// Cycle where WSYNC starts - 105?
#define WSYNC_START 104

namespace
{
std::array<DmaTimingProfile, 5> profiles;
bool profiles_initialized = false;

// One entry per CPU slot the line actually offers. The slot an instruction is
// scheduled on is where its opcode fetch happens; a store only reaches GTIA on
// its last cycle, which RasterInstructionCompletionOffset() resolves by looking
// the completion slot up in this same table. AltirraBridge confirms the result:
// for a store starting on slot c the first color clock showing the new value is
// exactly (slots[c + 3] - displayStart) * 2 + 1, on badlines and continuation
// lines alike. The evaluator supplies the trailing +1 through its offset < x
// test.
void BuildProfile(DmaTimingProfile& profile, DmaTimingKind kind,
	const int* slots, int slotCount, int displayStart)
{
	profile.kind = kind;
	profile.cpu_slots = slotCount;
	for (int index = 0; index < slotCount; ++index)
	{
		// The playfield's first visible color clock is selected by displayStart.
		profile.cycles[index].offset = (slots[index] - displayStart) * 2;
		const int next = index + 1 < slotCount ? slots[index + 1] : 115;
		profile.cycles[index].length = (next - slots[index]) * 2;
	}
}

void InitializeProfiles()
{
	if (profiles_initialized)
		return;

	// Canonical logical ANTIC slots for the wide playfield, derived from
	// Altirra's UpdateDMAPattern()/ATAnticSetRefreshCycles() and confirmed on
	// AltirraBridge by a 200-line program built to exactly these counts, which
	// runs a whole frame without drifting a single cycle.
	//
	// Wide mode-4 DMA: character names on even cycles 10..104 (badlines only),
	// character data on odd cycles 13..105 every line, missile 0, players 2-5,
	// display-list opcode 1 and LMS address 6-7 on character-row starts, and
	// nine refresh cycles from 25 every 4 pushed onto the next free cycle.
	// Negative values execute at the end of the preceding scanline after WSYNC
	// release.
	//
	// Data at 105 and names at 12 are DMA cycles, not free ones.
	static const int antic4Lms[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 8,9,11
	};
	static const int antic4Badline[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 6,7,8,9,11
	};
	static const int antic4Continuation[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 1, 6,7,8,9,10,11,12,
		14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
		72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104
	};
	// A wide badline is contended solidly from cycle 10 to 105, so its last
	// refresh cannot be placed until cycle 106 - the slot the following logical
	// line would otherwise open with. Every line after a badline is one CPU
	// slot shorter than an ordinary continuation line.
	static const int antic4ContinuationAfterBadline[] = {
		-7,-6,-5,-4,-3,-2,-1, 1, 6,7,8,9,10,11,12,
		14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
		72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104
	};

	// Preserve the proven mode-E map byte-for-byte.
	char antic_cycles[cycle_map_size] = "IPPPPAA             G G GRG GRG GRG GRG GRG GRG GRG GRG GRG G G G G G G G G G G G G G G G G G G G G              M";
	DmaTimingProfile& modeE = profiles[static_cast<int>(DmaTimingKind::AnticE)];
	modeE.kind = DmaTimingKind::AnticE;
	int antic_xpos, last_antic_xpos = 0;
	int cpu_xpos = 0;
	int scanline_cpu_slots = 0;
	for (antic_xpos = 0; antic_xpos < cycle_map_size; antic_xpos++)
	{
		char c = antic_cycles[antic_xpos];
		// we have set normal width, graphics mode, PMG and LMS in each line
		if (c != 'G' && c != 'R' && c != 'P' && c != 'M' && c != 'I' && c != 'A')
		{
			/*Not a stolen cycle*/
			assert(cpu_xpos < CYCLES_MAX);
			modeE.cycles[cpu_xpos].offset = (antic_xpos - 24) * 2;
			if (cpu_xpos > 0)
			{
				modeE.cycles[cpu_xpos - 1].length = (antic_xpos - last_antic_xpos) * 2;
			}
			last_antic_xpos = antic_xpos;
			cpu_xpos++;
			if (antic_xpos < antic_scanline_cycles)
				++scanline_cpu_slots;
		}
	}

	modeE.cycles[cpu_xpos - 1].length = (antic_xpos - 24) * 2;
	modeE.cpu_slots = scanline_cpu_slots;

	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4LmsBadline)],
		DmaTimingKind::Antic4LmsBadline, antic4Lms,
		static_cast<int>(std::size(antic4Lms)), 22);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4Badline)],
		DmaTimingKind::Antic4Badline, antic4Badline,
		static_cast<int>(std::size(antic4Badline)), 22);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4Continuation)],
		DmaTimingKind::Antic4Continuation, antic4Continuation,
		static_cast<int>(std::size(antic4Continuation)), 22);
	BuildProfile(
		profiles[static_cast<int>(DmaTimingKind::Antic4ContinuationAfterBadline)],
		DmaTimingKind::Antic4ContinuationAfterBadline,
		antic4ContinuationAfterBadline,
		static_cast<int>(std::size(antic4ContinuationAfterBadline)), 22);
	profiles_initialized = true;
}
}

const DmaTimingProfile& GetDmaTimingProfile(DmaTimingKind kind)
{
	InitializeProfiles();
	return profiles[static_cast<int>(kind)];
}

bool IsAntic4Badline(int y)
{
	return y >= 0 && y % 8 == 0;
}

bool IsAntic4ChbaseTransitionLine(int y, int pictureHeight)
{
	return y >= 0 && y + 1 < pictureHeight && y % 24 == 23;
}

RasterLineSchedule GetRasterLineSchedule(GraphicsMode mode, int y,
	int pictureHeight)
{
	if (mode == GraphicsMode::AnticE)
		return {&GetDmaTimingProfile(DmaTimingKind::AnticE), 54, 3, false};
	// Every raster instruction costs 2 or 4 cycles, so an optimizer limit has to
	// be even; the odd slot left over on each line goes to the fixed suffix.
	// limit + suffix must equal the profile's cpu_slots exactly, or the
	// WSYNC-free kernel drifts against ANTIC for the rest of the frame.
	if (y == 0)
		return {&GetDmaTimingProfile(DmaTimingKind::Antic4LmsBadline), 6, 5, false};
	if (IsAntic4Badline(y))
		return {&GetDmaTimingProfile(DmaTimingKind::Antic4Badline), 8, 5, false};
	if (IsAntic4Badline(y - 1))
		return {&GetDmaTimingProfile(
			DmaTimingKind::Antic4ContinuationAfterBadline), 48, 4, false};
	// CHBASE transitions land on y % 24 == 23, which is always y % 8 == 7 and
	// therefore always a full-length continuation line.
	const bool transition = IsAntic4ChbaseTransitionLine(y, pictureHeight);
	return {&GetDmaTimingProfile(DmaTimingKind::Antic4Continuation),
		transition ? 44 : 48, transition ? 9 : 5, transition};
}

void create_cycles_table()
{
	const DmaTimingProfile& modeE = GetDmaTimingProfile(DmaTimingKind::AnticE);
	std::copy(modeE.cycles.begin(), modeE.cycles.end(), screen_cycles);
	screen_cpu_slots = modeE.cpu_slots;
	assert(screen_cpu_slots == raster_cpu_slots);
}

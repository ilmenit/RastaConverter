#include "Program.h"
#include <assert.h>
#include <algorithm>

// Cycle where WSYNC starts - 105?
#define WSYNC_START 104

namespace
{
std::array<DmaTimingProfile,
	static_cast<size_t>(DmaTimingKind::Count)> profiles;
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

	// Mode E uses one LMS bitmap line per scanline. These are the exact free
	// CPU slots after display-list, player/missile, playfield and refresh DMA.
	// The normal table preserves the historically Altirra-calibrated phase.
	static const int anticENormal[] = {
		7,8,9,10,11,12,13,14,15,16,17,18,19,21,23,27,31,35,39,43,47,
		51,55,59,61,63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,
		95,97,99,100,101,102,103,104,105,106,107,108,109,110,111,112
	};
	static const int anticEWide[] = {
		7,8,9,10,11,13,15,17,19,21,23,27,31,35,39,43,47,51,55,59,61,
		63,65,67,69,71,73,75,77,79,81,83,85,87,89,91,93,95,97,99,101,
		103,105,106,107,108,109,110,111,112
	};

	// Normal mode-4 DMA starts eight ANTIC cycles later than wide DMA:
	// character names occupy 18..96 and character data 21..99. Unlike a wide
	// badline, it leaves enough room for the pending refresh before cycle 106,
	// so the following continuation line does not lose its first slot.
	static const int antic4NormalLms[] = {
		-8,-7,-6,-5,-4,-3,-2,-1,8,9,10,11,12,13,14,15,16,17,19,
		100,101,102,103,104,105
	};
	static const int antic4NormalBadline[] = {
		-8,-7,-6,-5,-4,-3,-2,-1,6,7,8,9,10,11,12,13,14,15,16,17,19,
		100,101,102,103,104,105
	};
	static const int antic4NormalContinuation[] = {
		-8,-7,-6,-5,-4,-3,-2,-1,1,6,7,8,9,10,11,12,13,14,15,16,17,
		18,19,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,72,
		74,76,78,80,82,84,86,88,90,92,94,96,98,100,101,102,103,104,105
	};

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
	static const int antic4WideLms[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 8,9,11
	};
	static const int antic4WideBadline[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 6,7,8,9,11
	};
	static const int antic4WideContinuation[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 1, 6,7,8,9,10,11,12,
		14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
		72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104
	};
	// A wide badline is contended solidly from cycle 10 to 105, so its last
	// refresh cannot be placed until cycle 106 - the slot the following logical
	// line would otherwise open with. Every line after a badline is one CPU
	// slot shorter than an ordinary continuation line.
	static const int antic4WideContinuationAfterBadline[] = {
		-7,-6,-5,-4,-3,-2,-1, 1, 6,7,8,9,10,11,12,
		14,16,18,20,22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,
		72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,102,104
	};

	BuildProfile(profiles[static_cast<int>(DmaTimingKind::AnticENormal)],
		DmaTimingKind::AnticENormal, anticENormal,
		static_cast<int>(std::size(anticENormal)), 24);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::AnticEWide)],
		DmaTimingKind::AnticEWide, anticEWide,
		static_cast<int>(std::size(anticEWide)), 22);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4NormalLmsBadline)],
		DmaTimingKind::Antic4NormalLmsBadline, antic4NormalLms,
		static_cast<int>(std::size(antic4NormalLms)), 24);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4NormalBadline)],
		DmaTimingKind::Antic4NormalBadline, antic4NormalBadline,
		static_cast<int>(std::size(antic4NormalBadline)), 24);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4NormalContinuation)],
		DmaTimingKind::Antic4NormalContinuation, antic4NormalContinuation,
		static_cast<int>(std::size(antic4NormalContinuation)), 24);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4WideLmsBadline)],
		DmaTimingKind::Antic4WideLmsBadline, antic4WideLms,
		static_cast<int>(std::size(antic4WideLms)), 22);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4WideBadline)],
		DmaTimingKind::Antic4WideBadline, antic4WideBadline,
		static_cast<int>(std::size(antic4WideBadline)), 22);
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4WideContinuation)],
		DmaTimingKind::Antic4WideContinuation, antic4WideContinuation,
		static_cast<int>(std::size(antic4WideContinuation)), 22);
	BuildProfile(
		profiles[static_cast<int>(
			DmaTimingKind::Antic4WideContinuationAfterBadline)],
		DmaTimingKind::Antic4WideContinuationAfterBadline,
		antic4WideContinuationAfterBadline,
		static_cast<int>(std::size(antic4WideContinuationAfterBadline)), 22);
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
	int pictureHeight, PlayfieldWidth width)
{
	if (mode == GraphicsMode::AnticE)
	{
		if (width == PlayfieldWidth::Wide)
			return {&GetDmaTimingProfile(DmaTimingKind::AnticEWide),
				46, 4, false};
		return {&GetDmaTimingProfile(DmaTimingKind::AnticENormal),
			54, 3, false};
	}
	const bool normal = width == PlayfieldWidth::Normal;
	// Every raster instruction costs 2 or 4 cycles, so an optimizer limit has to
	// be even; the odd slot left over on each line goes to the fixed suffix.
	// limit + suffix must equal the profile's cpu_slots exactly, or the
	// WSYNC-free kernel drifts against ANTIC for the rest of the frame.
	if (y == 0)
		return normal
			? RasterLineSchedule{
				&GetDmaTimingProfile(DmaTimingKind::Antic4NormalLmsBadline),
				20, 5, false}
			: RasterLineSchedule{
				&GetDmaTimingProfile(DmaTimingKind::Antic4WideLmsBadline),
				6, 5, false};
	if (IsAntic4Badline(y))
		return normal
			? RasterLineSchedule{
				&GetDmaTimingProfile(DmaTimingKind::Antic4NormalBadline),
				22, 5, false}
			: RasterLineSchedule{
				&GetDmaTimingProfile(DmaTimingKind::Antic4WideBadline),
				8, 5, false};
	if (!normal && IsAntic4Badline(y - 1))
		return {&GetDmaTimingProfile(
			DmaTimingKind::Antic4WideContinuationAfterBadline), 48, 4, false};
	// CHBASE transitions land on y % 24 == 23, which is always y % 8 == 7 and
	// therefore always a full-length continuation line.
	const bool transition = IsAntic4ChbaseTransitionLine(y, pictureHeight);
	if (normal)
		return {&GetDmaTimingProfile(DmaTimingKind::Antic4NormalContinuation),
			transition ? 50 : 56, transition ? 10 : 4, transition};
	return {&GetDmaTimingProfile(DmaTimingKind::Antic4WideContinuation),
		transition ? 44 : 48, transition ? 9 : 5, transition};
}

void create_cycles_table()
{
	const DmaTimingProfile& modeE =
		GetDmaTimingProfile(DmaTimingKind::AnticENormal);
	std::copy(modeE.cycles.begin(), modeE.cycles.end(), screen_cycles);
	screen_cpu_slots = modeE.cpu_slots;
	assert(screen_cpu_slots == raster_cpu_slots);
}

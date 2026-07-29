#include "Program.h"
#include <assert.h>
#include <algorithm>

// Cycle where WSYNC starts - 105?
#define WSYNC_START 104

namespace
{
std::array<DmaTimingProfile, 4> profiles;
bool profiles_initialized = false;

void BuildProfile(DmaTimingProfile& profile, DmaTimingKind kind,
	const int* slots, int slotCount)
{
	profile.kind = kind;
	profile.cpu_slots = slotCount;
	for (int index = 0; index < slotCount; ++index)
	{
		// Normal-width ANTIC playfield pixel zero begins at cycle 24.
		// GTIA register writes become visible one color clock after their CPU
		// write slot, which the evaluator adds through its offset < x test.
		profile.cycles[index].offset = (slots[index] - 24) * 2;
		const int next = index + 1 < slotCount ? slots[index + 1] : 115;
		profile.cycles[index].length = (next - slots[index]) * 2;
	}
}

void InitializeProfiles()
{
	if (profiles_initialized)
		return;

	// Canonical logical ANTIC slots. Negative values execute at the end of the
	// preceding scanline after WSYNC release.
	static const int antic4Lms[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 8,9,10,11,12,13,14,15,16,17,19,
		100,101,102,103,104,105
	};
	static const int antic4Badline[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 6,7,8,9,10,11,12,13,14,15,16,17,19,
		100,101,102,103,104,105
	};
	static const int antic4Continuation[] = {
		-8,-7,-6,-5,-4,-3,-2,-1, 1, 6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
		22,24,28,32,36,40,44,48,52,56,60,62,64,66,68,70,72,74,76,78,80,82,
		84,86,88,90,92,94,96,98,100,101,102,103,104,105
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
		static_cast<int>(std::size(antic4Lms)));
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4Badline)],
		DmaTimingKind::Antic4Badline, antic4Badline,
		static_cast<int>(std::size(antic4Badline)));
	BuildProfile(profiles[static_cast<int>(DmaTimingKind::Antic4Continuation)],
		DmaTimingKind::Antic4Continuation, antic4Continuation,
		static_cast<int>(std::size(antic4Continuation)));
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
	if (y == 0)
		return {&GetDmaTimingProfile(DmaTimingKind::Antic4LmsBadline), 22, 3, false};
	if (IsAntic4Badline(y))
		return {&GetDmaTimingProfile(DmaTimingKind::Antic4Badline), 24, 3, false};
	const bool transition = IsAntic4ChbaseTransitionLine(y, pictureHeight);
	return {&GetDmaTimingProfile(DmaTimingKind::Antic4Continuation),
		transition ? 48 : 54, transition ? 12 : 6, transition};
}

void create_cycles_table()
{
	const DmaTimingProfile& modeE = GetDmaTimingProfile(DmaTimingKind::AnticE);
	std::copy(modeE.cycles.begin(), modeE.cycles.end(), screen_cycles);
	screen_cpu_slots = modeE.cpu_slots;
	assert(screen_cpu_slots == raster_cpu_slots);
}

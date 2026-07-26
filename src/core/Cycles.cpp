#include "Program.h"
#include <assert.h>

// Cycle where WSYNC starts - 105?
#define WSYNC_START 104

void create_cycles_table()
{
	char antic_cycles[cycle_map_size] = "IPPPPAA             G G GRG GRG GRG GRG GRG GRG GRG GRG GRG G G G G G G G G G G G G G G G G G G G G              M";
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
			screen_cycles[cpu_xpos].offset = (antic_xpos - 24) * 2;
			if (cpu_xpos > 0)
			{
				screen_cycles[cpu_xpos - 1].length = (antic_xpos - last_antic_xpos) * 2;
			}
			last_antic_xpos = antic_xpos;
			cpu_xpos++;
			if (antic_xpos < antic_scanline_cycles)
				++scanline_cpu_slots;
		}
	}

	screen_cycles[cpu_xpos - 1].length = (antic_xpos - 24) * 2;
	screen_cpu_slots = scanline_cpu_slots;
	assert(screen_cpu_slots == raster_cpu_slots);
}

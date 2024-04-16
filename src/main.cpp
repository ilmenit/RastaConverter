#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef int64_t
#undef uint64_t
#include <stdint.h>
#include "rasta.h"
#include <iostream>

extern bool quiet;

bool LoadAtariPalette(string filename);
void create_cycles_table();

RastaConverter rasta;

int main(int argc, char *argv[])
{	
	//////////////////////////////////////////////////////////////////////////
	FreeImage_Initialise(TRUE);

	create_cycles_table();

	Configuration cfg;
	cfg.Process(argc, argv);

	if (cfg.continue_processing)
	{
		quiet=true;
		rasta.Resume();
		rasta.cfg.continue_processing=true;
		quiet=false;
	}
	else
		rasta.SetConfig(cfg);

	if (!rasta.cfg.preprocess_only)
	{
	}
	else
		quiet=true;

	if (rasta.ProcessInit())
	{
		rasta.MainLoop();
		rasta.SaveBestSolution();
	}
#ifndef NO_GUI
	SDL_Quit();
#endif
	return 0; // Exit with no errors
}

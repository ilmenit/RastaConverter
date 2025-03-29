#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef int64_t
#undef uint64_t
#include <stdint.h>
#include "RastaConverter.h"
#include <iostream>

bool quiet = false;

bool LoadAtariPalette(string filename);
void create_cycles_table();

int main(int argc, char *argv[])
{	
    // Initialize FreeImage
    FreeImage_Initialise(TRUE);

    // Initialize cycle tables
    create_cycles_table();

    // Create configuration
    Configuration cfg;
    cfg.Process(argc, argv);

    // Create RastaConverter
    RastaConverter rasta;

    // Resume if requested
    if (cfg.continue_processing)
    {
        quiet = true;
        rasta.Resume();
        rasta.cfg.continue_processing = true;
        quiet = false;
    }
    else
    {
        // Set new configuration
        rasta.SetConfig(cfg);
    }

    // Set quiet mode for preprocess only
    if (rasta.cfg.preprocess_only)
        quiet = true;

    // Initialize and run
    if (rasta.ProcessInit())
    {
        rasta.MainLoop();
        rasta.SaveBestSolution();
    }

    // Clean up SDL
#ifndef NO_GUI
    SDL_Quit();
#endif

    return 0; // Exit with no errors
}
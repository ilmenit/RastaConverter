#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef int64_t
#undef uint64_t
#include <stdint.h>
#include "platform/ConsoleCtrlWin.h"
#include "RastaConverter.h"
#include <iostream>
#include <fstream>
#include <mutex>

bool quiet = false;

bool LoadAtariPalette(string filename);
void create_cycles_table();

int main(int argc, char *argv[])
{	
    // Simple atexit and terminate diagnostics
    std::atexit([](){ std::cout << "[EXIT] atexit called" << std::endl; });
    std::set_terminate([](){ std::cerr << "[TERM] std::terminate called" << std::endl; std::abort(); });

    // Log console control events (CTRL+C, console close, etc.) and crashes
    RegisterConsoleCtrlLogger();
    RegisterUnhandledExceptionLogger();
    RegisterSignalHandlers();
    // Initialize FreeImage
    FreeImage_Initialise(TRUE);

    // Initialize cycle tables
    create_cycles_table();

    // Create configuration
    Configuration cfg;
    cfg.Process(argc, argv);

    // Create RastaConverter
    RastaConverter rasta;

    // Respect quiet mode (headless)
    quiet = cfg.quiet;

    // Resume if requested
    if (cfg.continue_processing)
    {
        quiet = true;
        rasta.Resume();
        rasta.cfg.continue_processing = true;
        quiet = cfg.quiet;
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
    else {
        std::cout << "[MAIN] ProcessInit returned false (preprocess-only or error)" << std::endl;
    }

    // Clean up SDL
#ifndef NO_GUI
    SDL_Quit();
#endif

    return 0; // Exit with no errors
}
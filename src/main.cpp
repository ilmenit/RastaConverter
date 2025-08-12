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

void create_cycles_table();

int main(int argc, char *argv[])
{	
    // Simple atexit and terminate diagnostics (debug builds only)
    #if (defined(THREAD_DEBUG) || defined(UI_DEBUG)) && !defined(NDEBUG)
    std::atexit([](){ std::cout << "[EXIT] atexit called" << std::endl; });
    std::set_terminate([](){ std::cerr << "[TERM] std::terminate called" << std::endl; std::abort(); });
    #endif

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

    // CLI help / usage handling
    auto print_help = []() {
        const char* help = R"(Usage:
  rasta <InputFile> [options]

Input:
  <InputFile>                 Source image path (positional) or use -i/--input

General options:
  -i, --input <FILE>          Input image path (alternative to positional)
  -o, --output <FILE>         Output base filename (default: output.png)
  -pal, --palette <FILE>      Atari palette .act file (default: Palettes/laoo.act)
  --optimizer <dlas|lahc>   Optimizer algorithm (default: dlas)
  --mutation_strategy <global|regional>
                               Mutation strategy (default: global)
  -s <N>                      DLAS/LAHC history length (default: 1)
  --threads <N>               Number of worker threads (default: 1)
  --max_evals <N>             Stop after N evaluations (0 = unlimited)
  --save <auto|N>             Auto-save period in evals or 'auto' (default: auto)
  --continue                  Resume from last saved state
  --preprocess                Only preprocess, save -src/-dst images and exit
  -q, --quiet                 Headless/quiet mode (no GUI)

Image processing:
  --distance <yuv|euclid|ciede|cie94>       Distance function (default: yuv)
  --predistance <yuv|euclid|ciede|cie94>    Preprocess distance (default: ciede)
  --dither <none|floyd|rfloyd|line|line2|chess|2d|jarvis|simple|knoll>
                                            Dithering (default: none)
  --dither_val <FLOAT>        Dither strength (default: 1)
  --dither_rand <FLOAT>       Dither randomness (default: 0)
  --filter <box|bilinear|bicubic|bspline|catmullrom|lanczos3>
                                            Rescale filter (default: box)
  --init <random|empty|less|smart>
                                            Initial program (default: random)
  -h <HEIGHT>                 Target height (max 240; default: auto)
  --cache <MB>                Line cache size in MB (default: 16)
  --seed <random|N>           RNG seed (default: random)
  --details <FILE>            Save detailed stats to file
  --details_val <FLOAT>       Details strength (default: 0.5)
  --onoff <FILE>              Per-line register on/off map
  --brightness <INT>          Brightness [-100..100] (default: 0)
  --contrast <INT>            Contrast   [-100..100] (default: 0)
  --gamma <FLOAT>             Gamma [0..8] (default: 1.0)

Dual-frame mode (A/B frame optimization):
  --dual [on|off]             Enable dual-frame mode (default: off). Bare --dual enables it.
  --flicker_luma_weight <F>   Luma flicker weight (default: 1.0)
  --flicker_luma_thresh <F>   Luma flicker threshold (default: 3)
  --flicker_exp_luma <INT>    Luma exponent (default: 2)
  --flicker_chroma_weight <F> Chroma flicker weight (default: 0.2)
  --flicker_chroma_thresh <F> Chroma flicker threshold (default: 8)
  --flicker_exp_chroma <INT>  Chroma exponent (default: 2)
  --dual_mutate_ratio <0..1>
                              Chance to mutate B vs A (default: 0.5)

Notes:
  - Options accept "/" or "-"/"--" prefixes. Examples: /threads=4, -threads 4, --threads 4
  - Input file can be positional or provided via -i/--input
  - Use --quiet to run without GUI and print errors to stderr
  - Internal dual-frame evaluation uses a YUV fast path
)";
        std::cout << help;
    };

    if (cfg.show_help || argc <= 1) {
        print_help();
        return (argc <= 1) ? 1 : 0; // no args -> treat as usage error
    }

    if (cfg.bad_arguments) {
        std::cerr << "Error: missing or invalid arguments.\n\n";
        print_help();
        return 2;
    }

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
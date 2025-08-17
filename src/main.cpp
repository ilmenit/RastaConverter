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
#include "app/RastaConverter.h"
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
  --mutation_base <best|current>
                                Mutation baseline (default: best)
  -s <N>                      DLAS/LAHC history length (default: 1)
  --threads <N>               Number of worker threads (default: 1)
  --max_evals <N>             Stop after N evaluations (0 = unlimited)
  --save <auto|N>             Auto-save period in evals or 'auto' (default: auto)
  --continue                  Resume from last saved state
  --preprocess                Only preprocess, save -src/-dst images and exit
  -q, --quiet                 Headless/quiet mode (no GUI)
  --legacy-mutations          Use only original 9 mutation types (max performance)

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
  --dual_init <dup|random|anti>
                              Initial relation of frames (default: dup)
  --flicker_luma <0..1>       How much luma flicker you accept (0=no, 1=full) (default: 1)
  --flicker_chroma <0..1>     How much chroma flicker you accept (0=no, 1=full) (default: 1)
  --dual_mutate_ratio <0..1>
                              Chance to mutate B vs A (default: 0.5)
  --dual_strategy <alternate|staged>
                              Mutation scheduling between A/B (default: staged)
  --dual_stage_evals <N>      If staged, iterations per focus block (default: 100000)
  --dual_stage_start <A|B>    If staged, which frame to focus first (default: A)
  --dual_cross_share_prob <F> Probability of occasional line copy/swap between frames (default: 0.05)
  --dual_both_frames_prob <F> Reserved small pair tweak probability (default: 0.0)

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
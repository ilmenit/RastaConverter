#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef int64_t
#undef uint64_t
#include <stdint.h>
#include "platform/win/ConsoleCtrlWin.h" 
#include "rasta.h"
#include "debug_log.h"
#include <iostream>

extern bool quiet;

bool LoadAtariPalette(string filename);
void create_cycles_table();

RastaConverter rasta;

int main(int argc, char *argv[])
{	
	//////////////////////////////////////////////////////////////////////////
    // Log console control events (CTRL+C, console close, etc.) and crashes
    RegisterConsoleCtrlLogger();
    RegisterUnhandledExceptionLogger();
    RegisterSignalHandlers(); 

	FreeImage_Initialise(TRUE);

	create_cycles_table();

	Configuration cfg;
	cfg.Process(argc, argv);
	DBG_PRINT("[MAIN] Args parsed. input='%s' dual=%d threads=%d quiet=%d", cfg.input_file.c_str(), (int)cfg.dual_mode, cfg.threads, (int)cfg.quiet);

	// CLI help / usage handling
	if (cfg.show_help || argc <= 1) {
		// Print any diagnostics first
		for (const auto &e : cfg.error_messages) std::cerr << "Error: " << e << "\n";
		for (const auto &w : cfg.warning_messages) std::cerr << "Warning: " << w << "\n";
		std::cout << cfg.parser.formatHelp("rasta");
		return (argc <= 1) ? 1 : 0; // no args -> treat as usage error
	}

	if (cfg.bad_arguments || !cfg.error_messages.empty()) {
		for (const auto &e : cfg.error_messages) std::cerr << "Error: " << e << "\n";
		for (const auto &w : cfg.warning_messages) std::cerr << "Warning: " << w << "\n";
		std::cout << cfg.parser.formatHelp("rasta");
		return 2;
	}

	// Respect quiet mode (headless)
	quiet = cfg.quiet;

	if (cfg.continue_processing)
	{
		quiet=true;
		// Respect current CLI (e.g., /output) during resume
		// Populate output_file from parsed args even though Process returned early
		cfg.output_file = cfg.parser.getValue("output", "output.png");
		rasta.SetConfig(cfg);
		rasta.Resume();
		rasta.cfg.continue_processing=true;
		quiet = cfg.quiet;
	}
	else
		rasta.SetConfig(cfg);

	if (!rasta.cfg.preprocess_only)
	{
	}
	else
		quiet=true;

	DBG_PRINT("[MAIN] Calling ProcessInit() ...");
	if (rasta.ProcessInit())
	{
		DBG_PRINT("[MAIN] ProcessInit OK. Entering MainLoop (dual=%d)", (int)rasta.cfg.dual_mode);
		rasta.MainLoop();
		rasta.SaveBestSolution();
	}
	else {
		std::cout << "[MAIN] ProcessInit returned false (preprocess-only or error)" << std::endl;
		DBG_PRINT("[MAIN] ProcessInit returned false");
	}

#ifndef NO_GUI
	SDL_Quit();
#endif
	return 0; // Exit with no errors
}



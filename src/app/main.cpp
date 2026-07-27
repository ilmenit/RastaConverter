#if defined(_WIN32)
#define NOMINMAX
#undef small // In case of macro conflict
#endif

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
#include "version.h"
#include "Interrupt.h"
#include <iostream>
#include <memory>

#ifndef NO_GUI
#include <SDL3/SDL_main.h>
#if defined(RASTA_ENABLE_LIVE_UI)
#include "frontend/gui/live_ui/LiveUI.h"
#endif
#endif

#if defined(_WIN32)
// Forward declare the function to avoid including the entire <shobjidl.h> header,
// which causes numerous compilation conflicts with other libraries.
extern "C" long __stdcall SetCurrentProcessExplicitAppUserModelID(const wchar_t *AppID);
#endif

extern bool quiet;

bool LoadAtariPalette(string filename);
void create_cycles_table();

// One conversion's worth of state. Recreated per run so nothing carries over
// between conversions started from the live UI.
std::unique_ptr<RastaConverter> rasta;

// Runs a single conversion to completion. Takes the configuration by value:
// the run mutates it, and the setup screen keeps its own copy for the next one.
static void RunConversion(Configuration cfg)
{
	ResetProcessGlobalsForNewRun();
	rasta = std::make_unique<RastaConverter>();

	// Respect quiet mode (headless)
	quiet = cfg.quiet;

	if (cfg.continue_processing)
	{
		quiet=true;
		// Respect current CLI (e.g., /output) during resume.
		// Populate output_file from parsed args even though Process returned early.
		// The live UI sets output_file directly when resuming a run picked from
		// its history, and that choice never reached the parser, so re-reading
		// it here would send the resume to "output.png" instead.
		if (!cfg.live_gui)
			cfg.output_file = cfg.parser.getValue("output", "output.png");
		rasta->SetConfig(cfg);
		rasta->Resume();
		rasta->cfg.continue_processing=true;
		quiet = cfg.quiet;
	}
	else
		rasta->SetConfig(cfg);

	if (rasta->cfg.preprocess_only)
		quiet=true;

	DBG_PRINT("[MAIN] Calling ProcessInit() ...");
	if (rasta->ProcessInit())
	{
		const char* structuredFixtureCsv = std::getenv("RASTA_STRUCTURED_FIXTURE_CSV");
		if (structuredFixtureCsv != nullptr && structuredFixtureCsv[0] != '\0')
		{
			const char* profile = std::getenv("RASTA_STRUCTURED_FIXTURE_PROFILE");
			if (!rasta->RunStructuredFixtureScreen(structuredFixtureCsv,
				profile != nullptr ? profile : "unspecified"))
			{
				std::cerr << "Structured fixture screen failed\n";
				return;
			}
		}
		else
		{
			DBG_PRINT("[MAIN] ProcessInit OK. Entering MainLoop (dual=%d)", (int)rasta->cfg.dual_mode);
			rasta->MainLoop();
			const char* phase7Csv = std::getenv("RASTA_PHASE7_RETAINED_CSV");
			if (phase7Csv != nullptr && phase7Csv[0] != '\0')
			{
				const char* profile = std::getenv("RASTA_STRUCTURED_FIXTURE_PROFILE");
				if (!rasta->RunPhase7RetainedWindowScreen(phase7Csv,
					profile != nullptr ? profile : "unspecified"))
				{
					std::cerr << "Phase 7 retained-window screen failed\n";
					return;
				}
			}
			if (rasta->AbortedWithoutSave())
			{
				std::cout << "Aborted; nothing new was written." << std::endl;
			}
			else
			{
				rasta->ApplyInternalStructuredFinalizer();
				rasta->SaveBestSolution();
			}
		}
	}
	else {
		std::cout << "[MAIN] ProcessInit returned false (preprocess-only or error)" << std::endl;
		DBG_PRINT("[MAIN] ProcessInit returned false");
	}

	// Releases the conversion window before the setup screen opens again.
	rasta.reset();
}

int main(int argc, char *argv[])
{	
#if defined(_WIN32)
    // Set a unique AppUserModelID to ensure the taskbar icon is handled correctly on Windows 11/10/7.
    // This allows Windows to group the app correctly and use the runtime-set icon.
    SetCurrentProcessExplicitAppUserModelID(L"RastaConverter.MainUI.1");
#endif

	//////////////////////////////////////////////////////////////////////////
    // Log console control events (CTRL+C, console close, etc.) and crashes
    RegisterConsoleCtrlLogger();
    RegisterUnhandledExceptionLogger();
    RegisterSignalHandlers();
    // Before anything creates a window: SDL installs its own SIGINT/SIGTERM
    // handlers otherwise, and turns them into a quit event that only the run
    // loop was reading. Ours makes an interrupt mean "stop and save".
    interrupts::InstallInterruptHandlers();

	FreeImage_Initialise(TRUE);

	create_cycles_table();

	Configuration cfg;
	cfg.Process(argc, argv);
	DBG_PRINT("[MAIN] Args parsed. input='%s' dual=%d threads=%d quiet=%d", cfg.input_file.c_str(), (int)cfg.dual_mode, cfg.threads, (int)cfg.quiet);

	// CLI help / usage handling
	if (cfg.show_help || (argc <= 1 && !cfg.live_gui)) {
		// Print any diagnostics first
		for (const auto &e : cfg.error_messages) std::cerr << "Error: " << e << "\n";
		for (const auto &w : cfg.warning_messages) std::cerr << "Warning: " << w << "\n";
		std::cout << cfg.parser.formatHelp("rasta");
		return (argc <= 1) ? 1 : 0; // non-live build with no args is a usage error
	}

	// Version display
	if (cfg.show_version) {
		std::cout << "RastaConverter " << RASTA_CONVERTER_VERSION << std::endl;
		return 0;
	}

	if (cfg.bad_arguments || !cfg.error_messages.empty()) {
		for (const auto &e : cfg.error_messages) std::cerr << "Error: " << e << "\n";
		for (const auto &w : cfg.warning_messages) std::cerr << "Warning: " << w << "\n";
		std::cout << cfg.parser.formatHelp("rasta");
		return 2;
	}
#if defined(RASTA_ENABLE_LIVE_UI) && !defined(NO_GUI)
	if (cfg.live_gui) {
		// The live UI is a session, not a one-shot: finishing a conversion
		// returns to the setup screen with the settings still in place, so the
		// next one is a tweak away rather than a relaunch.
		Configuration session = cfg;
		bool just_finished = false;
		for (;;) {
			// Each pass starts a fresh run unless the user picks Continue in
			// the history, and a fresh run wants its own output folder.
			session.output_file = "output.png";
			session.continue_processing = false;

			if (!rc_live_ui::RunSetup(session, /*show_recent*/ just_finished))
				break;
			if (session.input_file.empty()) {
				std::cerr << "Error: Choose an input file.\n";
				break;
			}
			for (const auto &w : session.warning_messages)
				std::cerr << "Warning: " << w << "\n";

			RunConversion(session);
			just_finished = true;
		}
		return 0;
	}
#endif
	for (const auto &w : cfg.warning_messages)
		std::cerr << "Warning: " << w << "\n";

	RunConversion(cfg);

	return 0; // Exit with no errors
}

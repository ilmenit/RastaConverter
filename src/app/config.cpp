#include <time.h>
#include <list>
#include "config.h"
#include "string_conv.h"
#include "prng_xoroshiro.h"
#include "version.h"

#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <cctype>
#include <cmath>
#include <thread>

using namespace std;

extern int solutions;

void Configuration::ProcessCmdLine(const std::vector<std::string>& extraTokens)
{
	auto tokenize = [](const std::string& line) {
		std::vector<std::string> result;
		std::string current;
		bool in_quotes = false;
		char quote_char = '\0';
		bool escape = false;
		auto flush = [&]() {
			if (!current.empty()) {
				result.push_back(current);
				current.clear();
			}
		};
		for (char ch : line) {
			if (escape) {
				current.push_back(ch);
				escape = false;
				continue;
			}
			if (in_quotes) {
				if (ch == '\\') {
					escape = true;
					continue;
				}
				if (ch == quote_char) {
					in_quotes = false;
					quote_char = '\0';
					continue;
				}
				current.push_back(ch);
				continue;
			}
			if (std::isspace(static_cast<unsigned char>(ch))) {
				flush();
				continue;
			}
			if (ch == '"' || ch == '\'') {
				in_quotes = true;
				quote_char = ch;
				continue;
			}
			current.push_back(ch);
		}
		if (escape) {
			current.push_back('\\');
			escape = false;
		}
		flush();
		return result;
	};

	std::vector<std::string> baseTokens = tokenize(command_line);

	auto parseTokens = [&](const std::vector<std::string>& tokens) {
		if (tokens.empty()) return;
		std::vector<char*> argv_storage;
		argv_storage.reserve(tokens.size() + 1);
		argv_storage.push_back(const_cast<char*>("/"));
		for (const std::string& tok : tokens) {
			argv_storage.push_back(const_cast<char*>(tok.c_str()));
		}
		Process(static_cast<int>(argv_storage.size()), argv_storage.data(), /*captureOverrides*/ false);
	};

	bool parsedBaselineThisCall = false;
	if (!resume_have_baseline) {
		if (!baseTokens.empty()) {
			parseTokens(baseTokens);
			parsedBaselineThisCall = true;
		}
		if (!resume_have_baseline) {
			resume_saved_optimizer = optimizer;
			resume_saved_solutions = solutions;
			resume_saved_distance = dstf;
			resume_saved_predistance = pre_dstf;
			resume_saved_dither = dither;
			resume_saved_objective = visual_objective;
			resume_saved_details_file = details_file;
			resume_saved_details_mode = details_mode;
			resume_saved_details_strength = details_strength;
			resume_saved_details_floor = details_floor;
			resume_saved_details_feather = details_feather;
			resume_saved_details_refine_mix = details_refine_mix;
			resume_saved_details_score = details_score;
			resume_saved_details_allocate = details_allocate;
			resume_saved_details_global_period = details_global_period;
			resume_have_baseline = true;
		}
	}

	const bool hasOverrides = !extraTokens.empty();
	if (hasOverrides) {
		std::vector<std::string> combinedTokens = baseTokens;
		combinedTokens.insert(combinedTokens.end(), extraTokens.begin(), extraTokens.end());
		parseTokens(combinedTokens);
	} else if (!parsedBaselineThisCall && !baseTokens.empty()) {
		// Ensure current config reflects the saved command line when baseline already existed
		parseTokens(baseTokens);
	}

	int normalized_saved_solutions = std::max(1, resume_saved_solutions);
	int normalized_current_solutions = std::max(1, solutions);
	resume_optimizer_changed = (resume_saved_optimizer != optimizer);
	resume_solutions_changed = (normalized_saved_solutions != normalized_current_solutions);
	resume_distance_changed = (resume_saved_distance != dstf);
	resume_predistance_changed = (resume_saved_predistance != pre_dstf);
	resume_dither_changed = (resume_saved_dither != dither);
	resume_objective_changed = (resume_saved_objective != visual_objective);
	const bool detailsMetricChanged = resume_saved_details_score != details_score
		|| ((resume_saved_details_score || details_score)
			&& (resume_saved_details_file != details_file
				|| resume_saved_details_mode != details_mode
				|| resume_saved_details_strength != details_strength
				|| resume_saved_details_floor != details_floor
				|| resume_saved_details_feather != details_feather
				|| resume_saved_details_refine_mix != details_refine_mix));
	resume_objective_changed = resume_objective_changed || detailsMetricChanged;
}

void Configuration::Process(int argc, char *argv[], bool captureOverrides)
{
    // Reset parser and diagnostics to allow multiple invocations (e.g., resume path)
    parser = CommandLineParser();
    error_messages.clear();
    warning_messages.clear();
	show_help = false;
	bad_arguments = false;

	// Define CLI specification (names are case-insensitive)
	// Groups: Input, General options, Image processing, Dual-frame mode
	parser.addFlag("help", {"?"},
		"Show this help and exit.",
		"General options");
	parser.addFlag("version", {"v"},
		"Show version information and exit.",
		"General options");
	parser.addFlag("continue", {},
		"Resume previously stopped process (uses existing output*.* files).",
		"General options");
	parser.addFlag("preprocess", {},
		"Only preprocess and save -src/-dst images, then exit.",
		"General options");
	parser.addFlag("quiet", {"q"},
		"Headless/quiet mode (no GUI).",
		"General options");
#if defined(RASTA_ENABLE_LIVE_UI)
	parser.addFlag("livegui", {},
		"Open the interactive setup screen and live conversion dashboard.",
		"General options");
#endif

	// Input/output
	parser.addOption("input", {"i"}, "FILE", "",
		"Input image path. May also be provided positionally as the first non-option argument.",
		"Input");
	parser.addOption("output", {"o"}, "FILE", "output.png",
		"Output base filename.",
		"Input");
	// Palette: allow '/palette' with no value to imply default
	parser.addOption("palette", {"pal"}, "FILE", "Palettes/laoo.act",
		"Atari palette .act file. If used without a value, defaults to Palettes/laoo.act.",
		"Input", /*optionalValue*/ true, /*implicitValue*/ "Palettes/laoo.act");

	parser.addOption("threads", {"t"}, "N", "1",
		"Number of worker threads, clamped to the machine's reported hardware-thread count when available.",
		"General options");
	parser.addOption("max_evals", {"me"}, "N", "1000000000000000000",
		"Stop after N evaluations (0 = unlimited).",
		"General options");
	parser.addOption("save", {}, "auto|N", "auto",
		"Auto-save period in evaluations or 'auto' to save ~every 30 seconds.",
		"General options");
	parser.addOption("seed", {}, "random|N", "random",
		"RNG seed.",
		"General options");

	// Image processing
parser.addOption("distance", {}, "yuv|euclid|ciede|cie94|oklab|rasta", "rasta",
		"Distance function used during optimization.",
		"Image processing");
parser.addOption("predistance", {}, "yuv|euclid|ciede|cie94|oklab|rasta", "ciede",
		"Distance function used during preprocess (destination picture).",
		"Image processing");
	parser.addOption("subfolder", {}, "on|off", "on",
		"Write a run's files into their own rc-<image>-NNN folder beside the "
		"source image, or straight into that folder.",
		"Output");
	parser.addOption("objective", {}, "target|source", "target",
		"Which picture a candidate is scored against.",
		"Image processing");
	// Accepted and ignored. These tuned the structural objectives that were
	// removed, and one of them appeared in the preset this program used to
	// recommend. Rejecting the option would strand those recipes at the command
	// line rather than at the objective, which is where the change happened.
	parser.addOption("spatial_weight", {}, "FLOAT", "0.1",
		"Ignored; the objective it tuned was removed.", "Image processing");
	parser.addOption("edge_weight", {}, "FLOAT", "0.1",
		"Ignored; the objective it tuned was removed.", "Image processing");
	parser.addOption("region_weight", {}, "FLOAT", "0.1",
		"Ignored; the objective it tuned was removed.", "Image processing");
	parser.addOption("dither", {}, "none|floyd|rfloyd|line|line2|chess|2d|jarvis|simple|knoll", "none",
		"Dithering algorithm.",
		"Image processing");
	parser.addOption("dither_val", {}, "FLOAT", "1",
		"Dither strength.",
		"Image processing");
	parser.addOption("dither_rand", {}, "FLOAT", "0",
		"Dither randomness.",
		"Image processing");
	parser.addOption("filter", {}, "box|bilinear|bicubic|bspline|catmullrom|lanczos3", "box",
		"Rescale filter.",
		"Image processing");
	parser.addOption("init", {}, "random|empty|less|smart", "random",
		"Initial program state.",
		"Image processing");
	parser.addOption("h", {}, "HEIGHT", "-1",
		"Target height (max 240; default: auto).",
		"Image processing");
	parser.addOption("cache", {}, "MB", "64",
		"Line cache size per thread in MB.",
		"Image processing");
	parser.addOption("details", {}, "FILE", "",
		"Details-priority mask image (legacy arithmetic-sRGB mode).",
		"Image processing");
	parser.addOption("target", {}, "FILE", "",
		"Use a prebuilt destination image instead of target preprocessing.",
		"Image processing");
	parser.addOption("details_val", {}, "FLOAT", "0.5",
		"Details influence strength.",
		"Image processing");
	parser.addOption("details_mode", {}, "legacy|normalized|refined", "legacy",
		"Details-mask interpretation mode.",
		"Image processing");
	parser.addOption("details_floor", {}, "FLOAT", "0.25",
		"Normalized-mode background priority floor [0.01..1].",
		"Image processing");
	parser.addOption("details_feather", {}, "PIXELS", "1",
		"Normalized-mode target-space feather radius [0..8].",
		"Image processing");
	parser.addOption("details_refine_mix", {}, "FLOAT", "0.5",
		"Refined-mode source-importance blend [0..1].",
		"Image processing");
	parser.addOption("details_score", {}, "on|off", "on",
		"Apply the loaded details map to direct pixel scoring.",
		"Image processing");
	parser.addOption("details_layer", {}, "on|off", "off",
		"Treat /details as a persisted target-space editable layer.",
		"Image processing");
	parser.addOption("details_global_period", {}, "N", "5",
		"With /details_allocate, force uniform global selection every N mutations.",
		"Image processing");
	parser.addFlag("details_allocate", {},
		"Bias scanline mutation selection using the loaded details map.",
		"Image processing");
	parser.addOption("brightness", {}, "INT", "0",
		"Brightness [-100..100].",
		"Image processing");
	parser.addOption("contrast", {}, "INT", "0",
		"Contrast [-100..100].",
		"Image processing");
	parser.addOption("gamma", {}, "FLOAT", "1.0",
		"Gamma [0..8].",
		"Image processing");
	parser.addOption("saturation", {}, "INT", "0",
		"Uniform colour saturation adjustment [-100..100].",
		"Image processing");
	parser.addOption("vibrance", {}, "INT", "0",
		"Selective saturation adjustment that protects saturated colours [-100..100].",
		"Image processing");
	parser.addOption("onoff", {}, "FILE", "",
		"OnOff file describing register enable/disable ranges.",
		"Image processing");

	// Solutions count (alias support)
	parser.addOption("solutions", {"s"}, "N", "1",
		"History length for optimizer (DLAS/LAHC) (>=1).",
		"General options");

	// Optimizer selection
	parser.addOption("optimizer", {"opt"}, "lahc|dlas|legacy", "lahc",
		"Select optimization algorithm: lahc (late acceptance, default), dlas (delayed acceptance), or legacy (legacy LAHC behavior).",
		"General options");
	parser.addOption("graphics_mode", {}, "e|antic4", "e",
		"Output graphics mode: ANTIC E bitmap (default) or ANTIC 4 character mode.",
		"General options");
	parser.addOption("playfield", {}, "normal|wide", "normal",
		"ANTIC playfield width. Normal is the default for both graphics modes.",
		"General options");

    // Aggressive search threshold (0 = never escalate)
    parser.addOption("unstuck_after", {"ua"}, "N", "0",
		"Escalate exploration after this many evaluations without improvement (0=never).",
		"General options");
    // Drift: support both --unstuck_drift (primary) and --unstuck_drift_norm (alias)
    parser.addOption("unstuck_drift", {"ud"}, "FLOAT", "0",
        "When stuck, add this normalized drift per evaluation to acceptance thresholds (0=off).",
        "General options");
    parser.addOption("unstuck_drift_norm", {}, "FLOAT", "0",
        "Alias for --unstuck_drift.",
        "General options");

	// Dual mode
	parser.addOption("dual", {}, "on|off", "off",
		"Enable dual-frame mode. If specified without value, behaves as 'on'.",
		"Dual-frame mode", /*optionalValue*/ true, /*implicitValue*/ "on");
	parser.addOption("first_dual_steps", {"fds"}, "N", "100000",
		"Bootstrap evaluations for frame A.",
		"Dual-frame mode");
	parser.addOption("after_dual_steps", {"ads"}, "copy|generate", "copy",
		"After A bootstrap: copy A to B (copy) or generate fresh B then bootstrap (generate).",
		"Dual-frame mode");
	parser.addOption("altering_dual_steps", {"alts"}, "N", "50000",
		"Evaluations per alternation block during dual Stage 3.",
		"Dual-frame mode");
	parser.addOption("dual_blending", {"db"}, "yuv|rgb", "yuv",
		"Blending color space for preview/export in dual mode.",
		"Dual-frame mode");
	parser.addOption("dual_luma", {"dl"}, "FLOAT", "0.2",
		"Temporal luma penalty weight (higher reduces flicker).",
		"Dual-frame mode");
	parser.addOption("dual_chroma", {"dc"}, "FLOAT", "0.1",
		"Temporal chroma penalty weight (higher reduces flicker).",
		"Dual-frame mode");
	parser.addOption("dual_dither", {"dd"}, "knoll|random|chess|line|line2", "none",
		"Input dithering type for dual mode (adds noise to input image before optimization).",
		"Dual-frame mode");
	parser.addOption("dual_dither_val", {"ddv"}, "FLOAT", "0.125",
		"Input dithering strength (0.0-2.0, default 0.125).",
		"Dual-frame mode");
	parser.addOption("dual_dither_rand", {"ddr"}, "FLOAT", "0.0",
		"Input dithering randomness (0.0-1.0, blends pattern with random noise).",
		"Dual-frame mode");

	// Parse now
	parser.parse(argc, argv);

	// Diagnose unrecognized options and missing values
	for (const auto &tok : parser.getUnrecognized()) {
		error_messages.push_back(std::string("Unrecognized option: ") + tok);
	}
	for (const auto &name : parser.getMissingValueOptions()) {
		error_messages.push_back(std::string("Missing value for option: ") + name);
	}

	// unified help switches (do not treat "-h" as help to avoid conflict with height)
	if (parser.switchExists("help") || parser.switchExists("?")) {
		show_help = true;
	}
	
	// version flag
	if (parser.switchExists("version") || parser.switchExists("v")) {
		show_version = true;
	}

	// If there are any fatal CLI issues, stop early so user sees errors and help immediately
	if (!parser.getMissingValueOptions().empty() || !parser.getUnrecognized().empty()) {
		bad_arguments = true;
	}

	// Set continue flag and capture overrides if requested
	continue_processing = parser.switchExists("continue");
	if (captureOverrides && continue_processing) {
		resume_override_tokens = parser.getNormalizedTokens();
		// Force baseline to be captured from resume payload later
		resume_have_baseline = false;
	}

	command_line = parser.rebuildCommandLine();

	input_file = parser.getValue("i","");
	if (input_file.empty()) input_file = parser.getValue("input","");
	output_file = parser.getValue("o","output.png");
	{
		std::string out2 = parser.getValue("output", "");
		if (!out2.empty()) output_file = out2;
	}
	palette_file = parser.getValue("pal","Palettes/laoo.act");
	{
		std::string p2 = parser.getValue("palette", "");
		if (!p2.empty()) palette_file = p2;
	}

	string dst_name = parser.getValue("distance","rasta");
	if (dst_name=="euclid")
		dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="ciede" || dst_name=="ciede2000")
		dstf=E_DISTANCE_CIEDE;
	else if (dst_name=="cie94")
		dstf=E_DISTANCE_CIE94;
	else if (dst_name=="oklab")
		dstf=E_DISTANCE_OKLAB;
	else if (dst_name=="yuv")
		dstf=E_DISTANCE_YUV;
	else 
	{
		if (dst_name != "rasta") warning_messages.push_back("Unknown distance='" + dst_name + "', using 'rasta'.");
		dstf=E_DISTANCE_RASTA;
	}

	{
		const string subfolder_value = parser.getValue("subfolder", "on");
		run_subfolder = subfolder_value != "off" && subfolder_value != "0"
			&& subfolder_value != "false";
	}
	{
		string objective_name = parser.getValue("objective", "target");
		// "target" is the current name; "legacy" is what it was called when the
		// name described the option's history rather than what it scores
		// against. Recipes written by older versions still carry it.
		if (objective_name == "target" || objective_name == "legacy")
			visual_objective = E_OBJECTIVE_LEGACY_TARGET;
		else if (objective_name == "source")
			visual_objective = E_OBJECTIVE_SOURCE;
		else if (objective_name == "source-spatial"
			|| objective_name == "source-composite"
			|| objective_name == "source-edge"
			|| objective_name == "source-region")
		{
			// Retired. Each of these scored the source plus a full-frame
			// structural term; the term cost about sixty times the throughput
			// and never earned it back. They map to plain source, which is what
			// they were built on and what beat them, rather than falling through
			// to target - a recipe that named one of these asked to score the
			// source, and that intent survives.
			warning_messages.push_back("Objective '" + objective_name
				+ "' was removed; using 'source'.");
			visual_objective = E_OBJECTIVE_SOURCE;
		}
		else
		{
			warning_messages.push_back("Unknown objective='" + objective_name
				+ "', using 'target'.");
			visual_objective = E_OBJECTIVE_LEGACY_TARGET;
		}
		for (const char* retired : {"spatial_weight", "edge_weight", "region_weight"})
		{
			if (parser.switchExists(retired))
				warning_messages.push_back(string("/") + retired
					+ " is ignored; the objective it tuned was removed.");
		}
	}

	dst_name = parser.getValue("predistance","ciede");
	if (dst_name=="euclid")
		pre_dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="rasta")
		pre_dstf=E_DISTANCE_RASTA;
	else if (dst_name=="cie94")
		pre_dstf=E_DISTANCE_CIE94;
else if (dst_name=="oklab")
	pre_dstf=E_DISTANCE_OKLAB;
else if (dst_name=="yuv")
	pre_dstf=E_DISTANCE_YUV;
	else 
	{
		if (dst_name != "ciede") warning_messages.push_back("Unknown predistance='" + dst_name + "', using 'ciede'.");
		pre_dstf=E_DISTANCE_CIEDE;
	}


	string dither_value = parser.getValue("dither","none");
	if (dither_value=="floyd")
		dither=E_DITHER_FLOYD;
	else if (dither_value=="rfloyd")
		dither=E_DITHER_RFLOYD;
	else if (dither_value=="line")
		dither=E_DITHER_LINE;
	else if (dither_value=="line2")
		dither=E_DITHER_LINE2;
	else if (dither_value=="chess" || dither_value=="cdither")
		dither=E_DITHER_CHESS;
	else if (dither_value=="2d")
		dither=E_DITHER_2D;
	else if (dither_value=="jarvis")
		dither=E_DITHER_JARVIS;
	else if (dither_value=="simple")
		dither=E_DITHER_SIMPLE;
	else if (dither_value=="knoll")
		dither=E_DITHER_KNOLL;
	else
	{
		if (dither_value != "none") warning_messages.push_back("Unknown dither='" + dither_value + "', using 'none'.");
		dither=E_DITHER_NONE;
	}

	string dither_val2;
	dither_val2 = parser.getValue("dither_val","1");
	dither_strength=String2Value<double>(dither_val2);

	dither_val2 = parser.getValue("dither_rand","0");
	dither_randomness=String2Value<double>(dither_val2);
	if (dither_randomness < 0.0) dither_randomness = 0.0;
	if (dither_randomness > 1.0) dither_randomness = 1.0;

	string cache_string = parser.getValue("cache", "64");
	cache_size = 1024*1024*String2Value<double>(cache_string);

	string seed_val;
	seed_val = parser.getValue("seed","random");

	if (seed_val=="random")
		initial_seed = (unsigned long) time( NULL );
	else
	{
		initial_seed = String2Value<unsigned long>(seed_val);
	}

	init_genrand( initial_seed );

	details_file = parser.getValue("details","");
	target_file = parser.getValue("target","");
	on_off_file = parser.getValue("onoff","");

	threads = String2Value<int>(parser.getValue("threads", "1"));
	if (threads < 1)
		threads = 1;
	const unsigned hardware_threads = std::thread::hardware_concurrency();
	if (hardware_threads > 0 && static_cast<unsigned>(threads) > hardware_threads)
	{
		warning_messages.push_back("/threads exceeds the "
			+ std::to_string(hardware_threads)
			+ " hardware threads reported by this machine; clamping to that limit.");
		threads = static_cast<int>(hardware_threads);
	}

	// auto-save is on by default
	string save_val = parser.getValue("save","auto");
	if (save_val == "auto" || save_val == "'auto'" || save_val == "\"auto\"")
		save_period = -1;
	else
		save_period=String2Value<int>(save_val);

	string details_val2 = parser.getValue("details_val","0.5");
	details_strength=String2Value<double>(details_val2);
	details_mode = parser.getValue("details_mode", "legacy");
	std::transform(details_mode.begin(), details_mode.end(), details_mode.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (details_mode != "legacy" && details_mode != "normalized"
		&& details_mode != "refined")
	{
		error_messages.push_back("/details_mode must be legacy, normalized, or refined.");
		details_mode = "legacy";
	}
	details_floor = String2Value<double>(parser.getValue("details_floor", "0.25"));
	if (details_floor < 0.01) details_floor = 0.01;
	if (details_floor > 1.0) details_floor = 1.0;
	int feather = String2Value<int>(parser.getValue("details_feather", "1"));
	if (feather < 0) feather = 0;
	if (feather > 8) feather = 8;
	details_feather = static_cast<unsigned>(feather);
	details_refine_mix = String2Value<double>(parser.getValue("details_refine_mix", "0.5"));
	if (details_refine_mix < 0.0) details_refine_mix = 0.0;
	if (details_refine_mix > 1.0) details_refine_mix = 1.0;
	std::string detailsScore = parser.getValue("details_score", "on");
	std::transform(detailsScore.begin(), detailsScore.end(), detailsScore.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (detailsScore != "on" && detailsScore != "off")
		error_messages.push_back("/details_score must be on or off.");
	details_score = detailsScore != "off";
	std::string detailsLayer = parser.getValue("details_layer", "off");
	std::transform(detailsLayer.begin(), detailsLayer.end(), detailsLayer.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (detailsLayer != "on" && detailsLayer != "off")
		error_messages.push_back("/details_layer must be on or off.");
	details_layer = detailsLayer == "on";
	details_allocate = parser.switchExists("details_allocate");
	int globalPeriod = String2Value<int>(parser.getValue("details_global_period", "5"));
	if (globalPeriod < 2) globalPeriod = 2;
	if (globalPeriod > 100) globalPeriod = 100;
	details_global_period = static_cast<unsigned>(globalPeriod);

	string solutions_value = parser.getValue("s","1");
	{
		std::string s2 = parser.getValue("solutions", "");
		if (!s2.empty()) solutions_value = s2;
	}
	solutions=String2Value<int>(solutions_value);
	if (solutions<1)
		solutions=1;

	// Optimizer parsing (default LAHC)
	{
		std::string opt = parser.getValue("optimizer", "lahc");
		for (auto &c : opt) c = (char)tolower(c);
		if (opt == "lahc") optimizer = E_OPT_LAHC;
		else if (opt == "dlas") optimizer = E_OPT_DLAS;
		else if (opt == "legacy") optimizer = E_OPT_LEGACY;
		else {
			warning_messages.push_back("Unknown optimizer='" + opt + "', using 'lahc'.");
			optimizer = E_OPT_LAHC;
		}
	}

	// Parse aggressive search threshold
	{
		std::string ua = parser.getValue("unstuck_after", "0");
		std::string ua2 = parser.getValue("ua", "");
		if (!ua2.empty()) ua = ua2;
		unstuck_after = String2Value<unsigned long long>(ua);
	}

    // Parse normalized drift per evaluation when stuck (prefer primary name, accept alias)
    {
        std::string ud = parser.getValue("unstuck_drift", "0");
        std::string ud2 = parser.getValue("ud", "");
        if (!ud2.empty()) ud = ud2;
        std::string ud_alt = parser.getValue("unstuck_drift_norm", "");
        if (!ud_alt.empty()) ud = ud_alt;
        unstuck_drift_norm = String2Value<double>(ud);
        if (unstuck_drift_norm < 0) unstuck_drift_norm = 0;
    }

	if (parser.switchExists("preprocess"))
		preprocess_only=true;
	else
		preprocess_only=false;

	// Quiet mode
	quiet = parser.switchExists("quiet") || parser.switchExists("q");
#if defined(RASTA_ENABLE_LIVE_UI)
	// A live-UI build is directly launchable from a desktop shortcut: no
	// command line means "open setup". CLI conversions still opt in explicitly.
	live_gui = parser.switchExists("livegui") || argc <= 1;
#else
	live_gui = false;
#endif

	string brightness_value = parser.getValue("brightness","0");
	brightness=String2Value<int>(brightness_value);
	if (brightness<-100)
		brightness=-100;
	if (brightness>100)
		brightness=100;

	string contrast_value = parser.getValue("contrast","0");
	contrast=String2Value<int>(contrast_value);
	if (contrast<-100)
		contrast=-100;
	if (contrast>100)
		contrast=100;

	string gamma_value = parser.getValue("gamma","1.0");
	gamma=String2Value<double>(gamma_value);
	if (gamma<0)
		gamma=0;
	if (gamma>8)
		gamma=8;

	saturation=String2Value<int>(parser.getValue("saturation","0"));
	saturation=std::max(-100, std::min(100, saturation));
	vibrance=String2Value<int>(parser.getValue("vibrance","0"));
	vibrance=std::max(-100, std::min(100, vibrance));
	
	// Handle positional input file if not specified via -i or --input
	if (input_file.empty())
	{
		const auto& positional = parser.getPositionalArguments();
		// Anything the parser did not claim as an option is a candidate path.
		// It used to skip every candidate containing a slash, which rejected
		// "photos/pic.png" and every absolute path on Linux and macOS - the
		// program could only be given a file in the current directory.
		for (const std::string& candidate : positional) {
			if (candidate.empty()) continue;
			if (candidate[0] == '-') continue;
			input_file = candidate;
			break;
		}
		if (input_file.empty() && argc > 1) {
			std::string temp = argv[1];
			if (!temp.empty() && temp[0] != '-')
				input_file = temp;
		}
	}

	// Validate that we have an input file
	if (input_file.empty() && !show_help && !continue_processing && !live_gui)
	{
		bad_arguments = true;
	}

	{
		std::string mode = parser.getValue("graphics_mode", "e");
		std::transform(mode.begin(), mode.end(), mode.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (mode == "e")
			graphics_mode = GraphicsMode::AnticE;
		else if (mode == "antic4")
			graphics_mode = GraphicsMode::Antic4;
		else
		{
			error_messages.push_back("/graphics_mode must be e or antic4.");
			graphics_mode = GraphicsMode::AnticE;
		}
	}
	playfield_width = DefaultPlayfieldWidth(graphics_mode);
	if (parser.valueProvided("playfield"))
	{
		std::string playfield = parser.getValue("playfield", "");
		std::transform(playfield.begin(), playfield.end(), playfield.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (playfield == "normal")
			playfield_width = PlayfieldWidth::Normal;
		else if (playfield == "wide")
			playfield_width = PlayfieldWidth::Wide;
		else
			error_messages.push_back("/playfield must be normal or wide.");
	}
	width = PlayfieldVisibleWidth(playfield_width);

	string rescale_filter_value = parser.getValue("filter","box");
	if (rescale_filter_value=="box")
		rescale_filter=FILTER_BOX;
	else if (rescale_filter_value=="bicubic")
		rescale_filter=FILTER_BICUBIC;
	else if (rescale_filter_value=="bilinear")
		rescale_filter=FILTER_BILINEAR;
	else if (rescale_filter_value=="bspline")
		rescale_filter=FILTER_BSPLINE;
	else if (rescale_filter_value=="catmullrom")
		rescale_filter=FILTER_CATMULLROM;
	else {
		if (rescale_filter_value != "lanczos3") warning_messages.push_back("Unknown filter='" + rescale_filter_value + "', using 'lanczos3'.");
		rescale_filter=FILTER_LANCZOS3;
	}

	string init_type_string = parser.getValue("init","random");
	if (init_type_string=="random")
		init_type=E_INIT_RANDOM;
	else if (init_type_string=="empty")
		init_type=E_INIT_EMPTY;
	else if (init_type_string=="less")
		init_type=E_INIT_LESS;
	else {
		if (init_type_string != "smart") warning_messages.push_back("Unknown init='" + init_type_string + "', using 'smart'.");
		init_type=E_INIT_SMART;
	}

	string height_value = parser.getValue("h","-1");
	height=String2Value<int>(height_value);
	if (height>240)
		height=240;

	string max_evals_value = parser.getValue("max_evals","1000000000000000000");
	max_evals=String2Value<unsigned long long>(max_evals_value);

	// --- Dual mode CLI ---
	// /dual on|off (default off). Accept also --dual without value -> on
	{
		bool has_dual_switch = parser.switchExists("dual");
		std::string dual_val = parser.getValue("dual", has_dual_switch ? "on" : "off");
		for (auto &c : dual_val) c = (char)tolower(c);
		dual_mode = (dual_val == "on" || dual_val == "1" || dual_val == "true");
	}

	// Counts are evaluation-based for consistency
	{
		std::string v = parser.getValue("first_dual_steps", "100000");
		first_dual_steps = String2Value<unsigned long long>(v);
	}
	{
		std::string v = parser.getValue("after_dual_steps", "copy");
		for (auto &c : v) c = (char)tolower(c);
		if (v != "copy" && v != "generate") {
			warning_messages.push_back("Unknown after_dual_steps='" + v + "', using 'copy'.");
			v = "copy";
		}
		after_dual_steps = v;
	}
	{
		std::string v = parser.getValue("altering_dual_steps", "50000");
		altering_dual_steps = String2Value<unsigned long long>(v);
	}
	{
		std::string v = parser.getValue("dual_blending", "yuv");
		for (auto &c : v) c = (char)tolower(c);
		if (v != "yuv" && v != "rgb") {
			warning_messages.push_back("Unknown dual_blending='" + v + "', using 'yuv'.");
			v = "yuv";
		}
		dual_blending = v;
	}
	{
		std::string v = parser.getValue("dual_luma", "0.2");
		dual_luma = String2Value<double>(v);
		if (dual_luma < 0.0) dual_luma = 0.0; // clamp
	}
	{
		std::string v = parser.getValue("dual_chroma", "0.1");
		dual_chroma = String2Value<double>(v);
		if (dual_chroma < 0.0) dual_chroma = 0.0; // clamp
	}
	{
		std::string v = parser.getValue("dual_dither", "none");
		for (auto &c : v) c = (char)tolower(c);
		if (v == "knoll")
			dual_dither = E_DUAL_DITHER_KNOLL;
		else if (v == "random")
			dual_dither = E_DUAL_DITHER_RANDOM;
		else if (v == "chess")
			dual_dither = E_DUAL_DITHER_CHESS;
		else if (v == "line")
			dual_dither = E_DUAL_DITHER_LINE;
		else if (v == "line2")
			dual_dither = E_DUAL_DITHER_LINE2;
		else
		{
			if (v != "none") warning_messages.push_back("Unknown dual_dither='" + v + "', using 'none'.");
			dual_dither = E_DUAL_DITHER_NONE;
		}
	}
	{
		std::string v = parser.getValue("dual_dither_val", "0.125");
		dual_dither_val = String2Value<double>(v);
		if (dual_dither_val < 0.0) dual_dither_val = 0.0;
		if (dual_dither_val > 2.0) dual_dither_val = 2.0;
	}
	{
		std::string v = parser.getValue("dual_dither_rand", "0.0");
		dual_dither_rand = String2Value<double>(v);
		if (dual_dither_rand < 0.0) dual_dither_rand = 0.0;
		if (dual_dither_rand > 1.0) dual_dither_rand = 1.0;
	}

	if (dual_mode && visual_objective != E_OBJECTIVE_LEGACY_TARGET)
	{
		warning_messages.push_back("Source-referenced objectives are single-frame experiments; using legacy for dual mode.");
		visual_objective = E_OBJECTIVE_LEGACY_TARGET;
	}

	if (graphics_mode == GraphicsMode::Antic4)
	{
		if (dual_mode)
			error_messages.push_back("ANTIC 4 currently supports single-frame conversion only; disable /dual.");
		if (height != -1)
		{
			const int normalized = NormalizeAntic4Height(height);
			if (normalized != height)
				warning_messages.push_back("ANTIC 4 height adjusted to "
					+ std::to_string(normalized)
					+ " scanlines (complete 8-scanline character rows).");
			height = normalized;
		}
	}
	if (dual_mode && playfield_width == PlayfieldWidth::Wide)
		error_messages.push_back(
			"Wide playfield currently supports single-frame conversion only; disable /dual.");
	if (!error_messages.empty())
		bad_arguments = true;

	if (!captureOverrides) {
		if (!resume_have_baseline) {
			resume_saved_optimizer = optimizer;
			resume_saved_solutions = solutions;
			resume_saved_distance = dstf;
			resume_saved_predistance = pre_dstf;
			resume_saved_dither = dither;
			resume_saved_objective = visual_objective;
			resume_have_baseline = true;
		}
	}
}

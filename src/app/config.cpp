#include <time.h>
#include <list>
#include "config.h"
#include "string_conv.h"
#include "prng_xoroshiro.h"

using namespace std;

extern int solutions;

void Configuration::ProcessCmdLine()
{
	string cmd=command_line;
	vector <char *> args;
	list <string> args_str;
	list <string>::iterator it;

	args_str.push_back("/");
	it=args_str.begin();
	args.push_back( (char *) (it->c_str()) ); // UGLY casting
	int params=1;

	size_t pos;
	string current_cmd;
	for (pos=0;pos<=cmd.size();++pos)
	{
		if (pos<cmd.size() && cmd[pos]!=' ')
		{
			current_cmd+=cmd[pos];			
		}
		else
		{
			args_str.push_back(current_cmd);
			++it;
			args.push_back( (char *) (it->c_str()) ); // UGLY casting
			current_cmd.clear();
			++params;
		}
	}

	if (params>1)
		Process(params,&args[0]);
}

void Configuration::Process(int argc, char *argv[])
{
	// Define CLI specification (names are case-insensitive)
	// Groups: Input, General options, Image processing, Dual-frame mode
	parser.addFlag("help", {"?"},
		"Show this help and exit.",
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
		"Number of worker threads.",
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
	parser.addOption("distance", {}, "yuv|euclid|ciede|cie94", "yuv",
		"Distance function used during optimization.",
		"Image processing");
	parser.addOption("predistance", {}, "yuv|euclid|ciede|cie94", "ciede",
		"Distance function used during preprocess (destination picture).",
		"Image processing");
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
	parser.addOption("cache", {}, "MB", "16",
		"Line cache size per thread in MB.",
		"Image processing");
	parser.addOption("details", {}, "FILE", "",
		"Save detailed stats to file.",
		"Image processing");
	parser.addOption("details_val", {}, "FLOAT", "0.5",
		"Details influence strength.",
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
	parser.addOption("onoff", {}, "FILE", "",
		"OnOff file describing register enable/disable ranges.",
		"Image processing");

	// Solutions count (alias support)
	parser.addOption("solutions", {"s"}, "N", "1",
		"History length for optimizer (DLAS/LAHC) (>=1).",
		"General options");

	// Optimizer selection
	parser.addOption("optimizer", {"opt"}, "lahc|dlas", "lahc",
		"Select optimization algorithm: lahc (late acceptance, default) or dlas (delayed acceptance).",
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

	// Parse now
	parser.parse(argc, argv);

	// Diagnose unrecognized options and missing values
	for (const auto &tok : parser.getUnrecognized()) {
		warning_messages.push_back(std::string("Unrecognized option: ") + tok);
	}
	for (const auto &name : parser.getMissingValueOptions()) {
		error_messages.push_back(std::string("Missing value for option: ") + name);
	}

	// unified help switches (do not treat "-h" as help to avoid conflict with height)
	if (parser.switchExists("help") || parser.switchExists("?")) {
		show_help = true;
	}

	// If there are any fatal CLI issues, stop early so user sees errors and help immediately
	if (!parser.getMissingValueOptions().empty()) {
		bad_arguments = true;
	}

	if (parser.switchExists("continue"))
	{
		continue_processing=true;
		return;
	}
	else
		continue_processing=false;

	command_line=parser.mn_command_line;

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

	string dst_name = parser.getValue("distance","yuv");
	if (dst_name=="euclid")
		dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="ciede" || dst_name=="ciede2000")
		dstf=E_DISTANCE_CIEDE;
	else if (dst_name=="cie94")
		dstf=E_DISTANCE_CIE94;
	else 
	{
		if (dst_name != "yuv") warning_messages.push_back("Unknown distance='" + dst_name + "', using 'yuv'.");
		dstf=E_DISTANCE_YUV;
	}

	dst_name = parser.getValue("predistance","ciede");
	if (dst_name=="euclid")
		pre_dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="ciede" || dst_name=="ciede2000")
		pre_dstf=E_DISTANCE_CIEDE;
	else if (dst_name=="cie94")
		pre_dstf=E_DISTANCE_CIE94;
	else 
	{
		if (dst_name != "yuv") warning_messages.push_back("Unknown predistance='" + dst_name + "', using 'yuv'.");
		pre_dstf=E_DISTANCE_YUV;
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

	string cache_string = parser.getValue("cache", "16");
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
	on_off_file = parser.getValue("onoff","");

	threads = String2Value<int>(parser.getValue("threads", "1"));
	if (threads < 1)
		threads = 1;

	// no more threads limit

	// auto-save is on by default
	string save_val = parser.getValue("save","auto");
	if (save_val == "auto" || save_val == "'auto'" || save_val == "\"auto\"")
		save_period = -1;
	else
		save_period=String2Value<int>(save_val);

	string details_val2 = parser.getValue("details_val","0.5");
	details_strength=String2Value<double>(details_val2);

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
		else {
			warning_messages.push_back("Unknown optimizer='" + opt + "', using 'lahc'.");
			optimizer = E_OPT_LAHC;
		}
	}

	if (parser.switchExists("preprocess"))
		preprocess_only=true;
	else
		preprocess_only=false;

	// Quiet mode
	quiet = parser.switchExists("quiet") || parser.switchExists("q");

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
	
	// Handle positional input file if not specified via -i or --input
	if (input_file.empty())
	{
		string temp;
		if (argc>1)
		{
			temp=argv[1];
			if (temp.find("/")==string::npos && temp.find("-")==string::npos)
				input_file=temp;
		}
	}

	// Validate that we have an input file
	if (input_file.empty() && !show_help && !continue_processing)
	{
		bad_arguments = true;
	}

	width=160; // constant in RastaConverter!

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
}



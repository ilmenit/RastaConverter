#include <time.h>
#include <list>
#include "../config.h"
#include "../string_conv.h"
#include "../mt19937int.h"

using namespace std;

int solutions = 1;

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
	parser.parse(argc, argv);

    // unified help switches (do not treat "-h" as help to avoid conflict with height)
    if (parser.switchExists("help") || parser.switchExists("?")) {
        show_help = true;
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

	if (parser.switchExists("palette"))
		palette_file = parser.getValue("palette","Palettes/laoo.act");

    // Parse mutation strategy - global is the default for backward compatibility
    string mutation_strategy_value = parser.getValue("mutation_strategy", "global");
    if (mutation_strategy_value == "regional")
        mutation_strategy = E_MUTATION_REGIONAL;
    else
        mutation_strategy = E_MUTATION_GLOBAL;

    // Optimizer selection
    string optimizer_value = parser.getValue("optimizer", "dlas");
    if (optimizer_value == "lahc") optimizer_type = E_OPT_LAHC; else optimizer_type = E_OPT_DLAS;

	string dst_name = parser.getValue("distance","yuv");
	if (dst_name=="euclid")
		dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="ciede" || dst_name=="ciede2000")
		dstf=E_DISTANCE_CIEDE;
	else if (dst_name=="cie94")
		dstf=E_DISTANCE_CIE94;
	else 
		dstf=E_DISTANCE_YUV;

	dst_name = parser.getValue("predistance","ciede");
	if (dst_name=="euclid")
		pre_dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="ciede" || dst_name=="ciede2000")
		pre_dstf=E_DISTANCE_CIEDE;
	else if (dst_name=="cie94")
		pre_dstf=E_DISTANCE_CIE94;
	else 
		pre_dstf=E_DISTANCE_YUV;


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
		dither=E_DITHER_NONE;

	string dither_val2;
	dither_val2 = parser.getValue("dither_val","1");
	dither_strength=String2Value<double>(dither_val2);

	dither_val2 = parser.getValue("dither_rand","0");
	dither_randomness=String2Value<double>(dither_val2);

    string cache_string = parser.getValue("cache", "16");
    {
        double cache_mb = String2Value<double>(cache_string);
        if (cache_mb < 1.0) cache_mb = 1.0; // minimum 1 MB to avoid thrashing
        const double bytes = cache_mb * 1024.0 * 1024.0;
        cache_size = static_cast<size_t>(bytes + 0.5); // round to nearest
    }

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

    // LAHC/DLAS history length (was /s). Kept backward compatible.
    string solutions_value = parser.getValue("s","1");
	solutions=String2Value<int>(solutions_value);
	if (solutions<1)
		solutions=1;

    if (parser.switchExists("preprocess"))
		preprocess_only=true;
	else
		preprocess_only=false;

    // Quiet flag for headless/CLI operation
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
	
    // allow positional input file as first non-option arg when not provided via /i or -i
    if (input_file.empty())
    {
        bool stop_options = false;
        for (int ai = 1; ai < argc; ++ai)
        {
            std::string tok = argv[ai];
            if (tok.empty()) continue;
            if (tok == "--") { stop_options = true; continue; }
            char c0 = tok[0];
            if (stop_options || (c0 != '-' && c0 != '/')) { input_file = tok; break; }
        }
    }

    // Accept value next to flag: -i <file>, /i <file>, --input <file>
    if (input_file.empty())
    {
        for (int ai = 1; ai + 1 < argc; ++ai)
        {
            std::string key = argv[ai];
            if (key == "--") break; // stop option parsing
            if (key == "-i" || key == "/i" || key == "--input")
            {
                std::string val = argv[ai + 1];
                if (!(val.size() && (val[0] == '-' || val[0] == '/'))) { input_file = val; break; }
            }
        }
    }

    // Validate required arguments for fresh run (not when continuing)
    if (!continue_processing)
    {
        if (input_file.empty()) {
            bad_arguments = true;
        }
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
	else rescale_filter=FILTER_LANCZOS3;

	string init_type_string = parser.getValue("init","random");
	if (init_type_string=="random")
		init_type=E_INIT_RANDOM;
	else if (init_type_string=="empty")
		init_type=E_INIT_EMPTY;
	else if (init_type_string=="less")
		init_type=E_INIT_LESS;
	else
		init_type=E_INIT_SMART;

	string height_value = parser.getValue("h","-1");
	height=String2Value<int>(height_value);
	if (height>240)
		height=240;

	string max_evals_value = parser.getValue("max_evals","1000000000000000000");
	max_evals=String2Value<unsigned long long>(max_evals_value);

    // Dual-frame mode and parameters (non-intrusive when off)
    {
        const bool dual_switch = parser.switchExists("dual");
        std::string dual_val = parser.getValue("dual", "");
        for (auto &c : dual_val) c = (char)tolower(c);
        // Rules:
        // - If /dual is present as a bare switch, enable dual mode
        // - Else if /dual has value on/true/1, enable
        dual_mode = false;
        if (dual_switch) {
            dual_mode = true;
        } else if (!dual_val.empty() && (dual_val == "on" || dual_val == "true" || dual_val == "1")) {
            dual_mode = true;
		}

        // blend_space (only YUV is supported in current build)
        blend_space = E_BLEND_YUV;

        // blend_distance (only YUV is supported in current build)
        blend_distance = E_DISTANCE_YUV;

        // gamma for rgb-linear
        blend_gamma = String2Value<double>(parser.getValue("blend_gamma", "2.2"));
        if (blend_gamma < 1.0) {
            blend_gamma = 1.0;
        } else if (blend_gamma > 4.0) {
            blend_gamma = 4.0;
        }

        // Simplified flicker controls only: luma/chroma acceptance 0..1

        // Simplified tolerances (0..1): how much flicker is accepted.
        // 0 -> keep default penalty; 1 -> no penalty. These override weights if provided.
        {
            std::string luma_tol = parser.getValue("flicker_luma", "");
            if (luma_tol.empty()) luma_tol = parser.getValue("blink_luma", "");
            if (!luma_tol.empty()) {
                double acc = String2Value<double>(luma_tol);
                if (acc > 1.0) acc *= 0.01; // accept percentages like 60
                if (acc < 0.0) acc = 0.0; else if (acc > 1.0) acc = 1.0;
                const double default_wl = 1.0; // baseline weight
                flicker_luma_weight = (1.0 - acc) * default_wl;
            }

            std::string chroma_tol = parser.getValue("flicker_chroma", "");
            if (chroma_tol.empty()) chroma_tol = parser.getValue("blink_chroma", "");
            if (!chroma_tol.empty()) {
                double acc = String2Value<double>(chroma_tol);
                if (acc > 1.0) acc *= 0.01;
                if (acc < 0.0) acc = 0.0; else if (acc > 1.0) acc = 1.0;
                const double default_wc = 0.2; // baseline weight
                flicker_chroma_weight = (1.0 - acc) * default_wc;
            }
        }

        // strategy
        {
            std::string strat = parser.getValue("dual_strategy", "alternate");
            for (auto &c : strat) c = (char)tolower(c);
            if (strat == "alternate" || strat == "alt") dual_strategy = E_DUAL_STRAT_ALTERNATE;
            else if (strat == "joint") dual_strategy = E_DUAL_STRAT_JOINT; // reserved; behaves as alternate currently
            else if (strat == "staged" || strat == "stage") dual_strategy = E_DUAL_STRAT_STAGED;
            else dual_strategy = E_DUAL_STRAT_ALTERNATE;
        }

        // init
        {
            std::string initv = parser.getValue("dual_init", "dup");
            for (auto &c : initv) c = (char)tolower(c);
            if (initv == "dup" || initv == "copy") dual_init = E_DUAL_INIT_DUP;
            else if (initv == "random") dual_init = E_DUAL_INIT_RANDOM;
            else if (initv == "anti") dual_init = E_DUAL_INIT_ANTI;
            else dual_init = E_DUAL_INIT_DUP;
        }

        dual_mutate_ratio = String2Value<double>(parser.getValue("dual_mutate_ratio", ""));
        if (dual_mutate_ratio < 0.0) {
            dual_mutate_ratio = 0.0;
        } else if (dual_mutate_ratio > 1.0) {
            dual_mutate_ratio = 1.0;
        }

        // cross-frame ops probabilities (optional)
        std::string cross_share_val = parser.getValue("dual_cross_share_prob", "");
        if (!cross_share_val.empty()) dual_cross_share_prob = String2Value<double>(cross_share_val);
        if (dual_cross_share_prob < 0.0) {
            dual_cross_share_prob = 0.0;
        } else if (dual_cross_share_prob > 1.0) {
            dual_cross_share_prob = 1.0;
        }
        std::string both_frames_val = parser.getValue("dual_both_frames_prob", "");
        if (!both_frames_val.empty()) dual_both_frames_prob = String2Value<double>(both_frames_val);
        if (dual_both_frames_prob < 0.0) {
            dual_both_frames_prob = 0.0;
        } else if (dual_both_frames_prob > 1.0) {
            dual_both_frames_prob = 1.0;
        }

        // staged dual params
        {
            std::string stage_len = parser.getValue("dual_stage_evals", "5000");
            if (!stage_len.empty()) dual_stage_evals = String2Value<unsigned long long>(stage_len);
            if (dual_stage_evals < 1ULL) dual_stage_evals = 1ULL;
            std::string stage_start = parser.getValue("dual_stage_start", "A");
            for (auto &c : stage_start) c = (char)tolower(c);
            dual_stage_start_B = (stage_start == "b" || stage_start == "1" || stage_start == "true" || stage_start == "on");
        }

        // flicker ramp config (aliases blink_* accepted)
        std::string ramp_evals_val = parser.getValue("blink_ramp_evals", "0");
        blink_ramp_evals = String2Value<unsigned long long>(ramp_evals_val);
        std::string wl_init_val = parser.getValue("flicker_luma_weight_initial", "");
        if (wl_init_val.empty()) wl_init_val = parser.getValue("blink_luma_weight_initial", "");
        if (!wl_init_val.empty()) flicker_luma_weight_initial = String2Value<double>(wl_init_val);
    }
}

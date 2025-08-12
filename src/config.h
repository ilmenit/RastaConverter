#ifndef CONFIG_H
#define CONFIG_H

#include <vector>
#include <string>
#include "FreeImage.h"
#include "CommandLineParser.h"
#include <assert.h>
#include "color/rgb.h"

using namespace Epoch::Foundation;

enum e_init_type {
	E_INIT_RANDOM,
	E_INIT_SMART,
	E_INIT_EMPTY,
	E_INIT_LESS,
};

enum e_mutation_strategy {
    E_MUTATION_GLOBAL,    // Original approach - all threads can mutate any line
    E_MUTATION_REGIONAL,  // Region-based approach - threads focus on their specific region
};

enum e_dither_type {
	E_DITHER_NONE,
	E_DITHER_FLOYD,
	E_DITHER_RFLOYD,
	E_DITHER_LINE,
	E_DITHER_LINE2,
	E_DITHER_CHESS,
	E_DITHER_SIMPLE,
	E_DITHER_2D,
	E_DITHER_JARVIS,
	E_DITHER_KNOLL,
};

enum e_distance_function {
	E_DISTANCE_EUCLID,
	E_DISTANCE_YUV,
	E_DISTANCE_CIEDE,
	E_DISTANCE_CIE94,
};

enum e_optimizer_type {
    E_OPT_DLAS,
    E_OPT_LAHC,
};

// Dual-frame blend mode enums
enum e_blend_space {
    E_BLEND_YUV,
    E_BLEND_RGB_LINEAR,
    E_BLEND_LAB,
    E_BLEND_AUTO
};

enum e_dual_strategy {
    E_DUAL_STRAT_ALTERNATE,
    E_DUAL_STRAT_JOINT,
    E_DUAL_STRAT_STAGED
};

enum e_dual_init {
    E_DUAL_INIT_DUP,
    E_DUAL_INIT_RANDOM,
    E_DUAL_INIT_ANTI
};

struct Configuration {
	std::string input_file;
	std::string output_file;
	std::string palette_file;
	std::string details_file;
	std::string command_line;
	std::string on_off_file;

	e_distance_function dstf;
	e_distance_function pre_dstf;
	bool continue_processing;

    e_mutation_strategy mutation_strategy;
	e_dither_type dither;
	double dither_randomness; // 0-1
	double dither_strength;
	double details_strength;

	int brightness;
	int contrast;
	double gamma;
	int save_period;
	unsigned long initial_seed;
    unsigned long resume_seed; // seed extracted from saved program (for /continue)
    bool have_resume_seed = false;
	int cache_size;

	bool preprocess_only;
	int threads;
	int width;
	int height;
	unsigned long long max_evals;
	FREE_IMAGE_FILTER rescale_filter;
	e_init_type init_type;
    e_optimizer_type optimizer_type;
    bool quiet;
    // CLI handling flags
    bool show_help = false;
    bool bad_arguments = false;

	CommandLineParser parser; 

	void ProcessCmdLine();
	void Process(int argc, char *argv[]);

    // Dual-frame blend configuration
    bool dual_mode = false;                     // /dual=on|off
    e_blend_space blend_space = E_BLEND_YUV;    // /blend_space=
    e_distance_function blend_distance = E_DISTANCE_YUV; // /blend_distance=
    double blend_gamma = 2.2;                   // /blend_gamma= (rgb-linear)

    double flicker_luma_weight = 1.0;           // /flicker_luma_weight=
    double flicker_luma_thresh = 3.0;           // /flicker_luma_thresh=
    int    flicker_exp_luma = 2;                // /flicker_exp_luma=
    double flicker_chroma_weight = 0.2;         // /flicker_chroma_weight=
    double flicker_chroma_thresh = 8.0;         // /flicker_chroma_thresh=
    int    flicker_exp_chroma = 2;              // /flicker_exp_chroma=

    e_dual_strategy dual_strategy = E_DUAL_STRAT_ALTERNATE; // /dual_strategy=
    e_dual_init dual_init = E_DUAL_INIT_DUP;                // /dual_init=
    double dual_mutate_ratio = 0.5;           // probability to mutate B vs A
    // Cross-frame structural ops probabilities
    double dual_cross_share_prob = 0.05;      // probability of cross copy/swap
    double dual_both_frames_prob = 0.0;       // reserved for future
    // Flicker ramp configuration
    unsigned long long blink_ramp_evals = 0;  // number of evals to ramp WL
    double flicker_luma_weight_initial = 1.0; // starting WL if ramp enabled
};

#endif

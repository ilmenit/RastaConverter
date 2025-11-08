#ifndef CONFIG_H
#define CONFIG_H

#include <vector>
#include <string>

#if defined(__APPLE__)
#define FREEIMAGE_H_BOOL_OVERRIDE
#define BOOL FreeImageBOOL
#endif

#include "FreeImage.h"

#if defined(FREEIMAGE_H_BOOL_OVERRIDE)
#undef BOOL
#undef FREEIMAGE_H_BOOL_OVERRIDE
#endif
#include "CommandLineParser.h"
#include <assert.h>
#include "rgb.h"

enum e_init_type {
	E_INIT_RANDOM,
	E_INIT_SMART,
	E_INIT_EMPTY,
	E_INIT_LESS,
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

enum e_dual_dither_type {
	E_DUAL_DITHER_NONE,
	E_DUAL_DITHER_KNOLL,
	E_DUAL_DITHER_RANDOM,
	E_DUAL_DITHER_CHESS,
	E_DUAL_DITHER_LINE,
	E_DUAL_DITHER_LINE2,
};

enum e_distance_function {
	E_DISTANCE_EUCLID,
	E_DISTANCE_YUV,
	E_DISTANCE_CIEDE,
	E_DISTANCE_CIE94,
	E_DISTANCE_OKLAB,
	E_DISTANCE_RASTA,
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

	e_dither_type dither;
	double dither_randomness; // 0-1
	double dither_strength;
	double details_strength;

	int brightness;
	int contrast;
	double gamma;
	int save_period;
	unsigned long initial_seed;
	int cache_size;

	bool preprocess_only;
	int threads;
	int width;
	int height;
	unsigned long long max_evals;
	FREE_IMAGE_FILTER rescale_filter;
	e_init_type init_type;
	bool quiet;
	// CLI handling flags
	bool show_help = false;
	bool show_version = false;
	bool bad_arguments = false;
	std::vector<std::string> error_messages; // fatal issues to show user
	std::vector<std::string> warning_messages; // non-fatal issues

	// --- Dual mode options ---
	bool dual_mode = false; // /dual on|off (default off)
	unsigned long long first_dual_steps = 100000; // /first_dual_steps
	std::string after_dual_steps = "copy"; // /after_dual_steps=generate|copy
	unsigned long long altering_dual_steps = 50000; // /altering_dual_steps
	std::string dual_blending = "yuv"; // /dual_blending=rgb|yuv (default yuv)
	// Temporal penalty weights to control flicker perception in dual mode
	double dual_luma = 0.2;   // weight for (Ya - Yb)^2
	double dual_chroma = 0.1; // weight for (Ua - Ub)^2 + (Va - Vb)^2
	// Input dithering for dual mode (applies noise to input image before optimization)
	e_dual_dither_type dual_dither = E_DUAL_DITHER_NONE; // /dual_dither=knoll|random|chess|line|line2
	double dual_dither_val = 0.125; // /dual_dither_val (0.0-2.0, default 0.125)
	double dual_dither_rand = 0.0; // /dual_dither_rand (0.0-1.0, default 0.0)

	// --- Optimizer selection ---
	enum e_optimizer { E_OPT_DLAS, E_OPT_LAHC, E_OPT_LEGACY };
	e_optimizer optimizer = E_OPT_LAHC; // /opt dlas|lahc|legacy (default lahc)

	// Aggressive search trigger: escalate exploration after this many
	// evaluations without improvement (0 = never escalate)
	unsigned long long unstuck_after = 1000ULL;

	// When stuck, add this normalized drift to acceptance thresholds per evaluation
	// Units: normalized distance (same scale as Norm. Dist). 0 = disabled.
	double unstuck_drift_norm = 0.1;


	CommandLineParser parser; 
	std::vector<std::string> resume_override_tokens;
	bool resume_distance_changed = false;
	bool resume_predistance_changed = false;
	bool resume_dither_changed = false;
	bool resume_optimizer_changed = false;
	bool resume_solutions_changed = false;
	bool resume_have_baseline = false;
	e_optimizer resume_saved_optimizer = E_OPT_LAHC;
	int resume_saved_solutions = 1;
	e_distance_function resume_saved_distance = E_DISTANCE_RASTA;
	e_distance_function resume_saved_predistance = E_DISTANCE_CIEDE;
	e_dither_type resume_saved_dither = E_DITHER_NONE;

	void ProcessCmdLine(const std::vector<std::string>& extraTokens = {});
	void Process(int argc, char *argv[], bool captureOverrides = true);
};

#endif



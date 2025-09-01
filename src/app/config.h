#ifndef CONFIG_H
#define CONFIG_H

#include <vector>
#include <string>
#include "FreeImage.h"
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

enum e_distance_function {
	E_DISTANCE_EUCLID,
	E_DISTANCE_YUV,
	E_DISTANCE_CIEDE,
	E_DISTANCE_CIE94,
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

	// --- Optimizer selection ---
	enum e_optimizer { E_OPT_DLAS, E_OPT_LAHC };
	e_optimizer optimizer = E_OPT_LAHC; // /opt dlas|lahc (default lahc)


	CommandLineParser parser; 

	void ProcessCmdLine();
	void Process(int argc, char *argv[]);
};

#endif



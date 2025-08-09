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
    E_OPT_DLASHC,
    E_OPT_LAHC,
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

	CommandLineParser parser; 

	void ProcessCmdLine();
	void Process(int argc, char *argv[]);
};

#endif

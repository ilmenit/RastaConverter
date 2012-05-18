#ifndef CONFIG_H
#define CONFIG_H

#include <vector>
#include <string>
#include "FreeImage.h"
#include <allegro.h> 
#include "CommandLineParser.h"
#include <assert.h>
#include "rgb.h"

using namespace Epoch::Foundation;

enum e_init_type {
	E_INIT_RANDOM,
	E_INIT_SMART,
	E_INIT_EMPTY,
	E_INIT_LESS,
};

enum e_dither_type {
	E_DITHER_NONE,
	E_DITHER_FLOYD,
	E_DITHER_CHESS,
	E_DITHER_SIMPLE,
	E_DITHER_2D,
	E_DITHER_JARVIS,
};


struct Configuration {
	std::string input_file;
	std::string output_file;
	std::string palette_file;
	std::string command_line;

	bool border;
	bool euclid;
	bool continue_processing;

	e_dither_type dither;
	int width;
	int height;
	FREE_IMAGE_FILTER rescale_filter;
	e_init_type init_type;

	CommandLineParser parser; 

	void ProcessCmdLine();
	void Process(int argc, char *argv[]);
};

#endif

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
};

struct Configuration {
	std::string input_file;
	std::string output_file;
	std::string palette_file;

	bool border;
	bool dither;
	double dither_level;
	int width;
	int height;
	FREE_IMAGE_FILTER rescale_filter;
	e_init_type init_type;

	CommandLineParser parser; 

	Configuration(Configuration &a)
	{
		*this=a;
	}
	Configuration(int argc, char *argv[]);
};

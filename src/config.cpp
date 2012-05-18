#include "config.h"
#include "string_conv.h"

using namespace std;

extern int solutions;

void ShowHelp();
void error(char *e);
void error(char *e, int i);

Configuration::Configuration(int argc, char *argv[])
{
	dither_level=0.5;

	parser.parse(argc, argv);

	input_file = parser.getValue("i","NoFileName");
	output_file = parser.getValue("o","output.png");
	palette_file = parser.getValue("pal","Palettes\\altirra.act");

	if (parser.switchExists("noborder"))
		border=false;
	else
		border=true;

	if (parser.switchExists("palette"))
		palette_file = parser.getValue("palette","Palettes\\altirra.act");

	string solutions_value = parser.getValue("s","1");
	solutions=String2Value<int>(solutions_value);

	if (!parser.switchExists("i"))
	{
		string temp;
		if (argc>1)
		{
			temp=argv[1];
			if (temp.find("/")==string::npos)
				input_file=temp;
		}
		else
			ShowHelp();
	}
	if (parser.switchExists("help") || parser.nonInterpretedExists("--help") || parser.switchExists("?"))
		ShowHelp();

	width=160; // constant in RastaConverter!

	string rescale_filter_value = parser.getValue("filter","lanczos");
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

	string init_type_string = parser.getValue("init","smart");
	if (init_type_string=="random")
		init_type=E_INIT_RANDOM;
	else if (init_type_string=="empty")
		init_type=E_INIT_EMPTY;
	else
		init_type=E_INIT_SMART;

	string height_value = parser.getValue("h","240");
	height=String2Value<int>(height_value);
}

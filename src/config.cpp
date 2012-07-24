#include "config.h"
#include "string_conv.h"
#include <time.h>
#include "mt19937int.h"

#include <list>

using namespace std;

extern int solutions;

void ShowHelp();
void error(char *e);
void error(char *e, int i);

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

	if (parser.switchExists("continue"))
	{
		continue_processing=true;
		return;
	}
	else
		continue_processing=false;

	command_line=parser.mn_command_line;

	input_file = parser.getValue("i","NoFileName");
	output_file = parser.getValue("o","output.png");
	palette_file = parser.getValue("pal","Palettes/altirra.act");

	if (parser.switchExists("palette"))
		palette_file = parser.getValue("palette","Palettes/altirra.act");

	string dst_name = parser.getValue("distance","yuv");
	if (dst_name=="euclid")
		dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="ciede")
		dstf=E_DISTANCE_CIEDE;
	else 
		dstf=E_DISTANCE_YUV;

	dst_name = parser.getValue("predistance","ciede");
	if (dst_name=="euclid")
		pre_dstf=E_DISTANCE_EUCLID;
	else if (dst_name=="ciede")
		pre_dstf=E_DISTANCE_CIEDE;
	else 
		pre_dstf=E_DISTANCE_YUV;


	string dither_value = parser.getValue("dither","none");
	if (dither_value=="floyd")
		dither=E_DITHER_FLOYD;
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

	string seed_val;
	seed_val = parser.getValue("seed","random");
	if (seed_val=="random")
		init_genrand( (unsigned long) time( NULL ));
	else
	{
		unsigned long seed=String2Value<unsigned long>(seed_val);
		init_genrand( seed );
	}

	details_file = parser.getValue("details","");
	on_off_file = parser.getValue("onoff","");

	string save_val = parser.getValue("save","0");
	save_period=String2Value<int>(save_val);

	string details_val2 = parser.getValue("details_val","0.5");
	details_strength=String2Value<double>(details_val2);

	string solutions_value = parser.getValue("s","1");
	solutions=String2Value<int>(solutions_value);
	if (solutions<1)
		solutions=1;

	if (parser.switchExists("preprocess"))
		preprocess_only=true;
	else
		preprocess_only=false;

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


	if (parser.switchExists("picture_colors"))
		picture_colors_only=true;
	else
		picture_colors_only=false;
	
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

	string init_type_string = parser.getValue("init","random");
	if (init_type_string=="random")
		init_type=E_INIT_RANDOM;
	else if (init_type_string=="empty")
		init_type=E_INIT_EMPTY;
	else if (init_type_string=="less")
		init_type=E_INIT_LESS;
	else
		init_type=E_INIT_SMART;

	string height_value = parser.getValue("h","240");
	height=String2Value<int>(height_value);

	string max_evals_value = parser.getValue("max_evals","1000000000000000000");
	max_evals=String2Value<unsigned long long>(max_evals_value);
}

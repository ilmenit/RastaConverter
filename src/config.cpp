#include "config.h"
#include "string_conv.h"

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

	if (parser.switchExists("noborder"))
		border=false;
	else
		border=true;

	if (parser.switchExists("euclid"))
		euclid=true;
	else
		euclid=false;

	if (parser.switchExists("palette"))
		palette_file = parser.getValue("palette","Palettes/altirra.act");

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
	else
		dither=E_DITHER_NONE;

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

	string max_evals_value = parser.getValue("max_evals","2000000000");
	max_evals=String2Value<unsigned>(max_evals_value);
}

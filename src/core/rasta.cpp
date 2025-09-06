const char *program_version="Beta8";

#ifdef _MSC_VER
#pragma warning (disable: 4312)
#pragma warning (disable: 4996)
#endif

#include <climits>
#include <math.h>
#include <cmath>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include "FreeImage.h"

#undef int8_t
#undef uint8_t
#undef int16_t
#undef uint16_t
#undef int32_t
#undef uint32_t
#undef int64_t
#undef uint64_t
#include <stdint.h>

#include "CommandLineParser.h"
#include "string_conv.h"
#include <assert.h>
#include "config.h"
#include <float.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sstream>
#include <ctype.h>
#include <iomanip>
#include <iterator>

#include "rasta.h"
#include "prng_xoroshiro.h"
#include "LinearAllocator.h"
#include "LineCache.h"
#include "Program.h"
#include "Evaluator.h"
#include "TargetPicture.h"
#include "debug_log.h"

#ifndef _MSC_EXTENSIONS
#define __timeb64 timeb
#define _ftime ftime
#endif

unsigned char FindAtariColorIndex( const rgb& col );


// Cycle where WSYNC starts - 105?
#define WSYNC_START 104
// Normal screen CPU cycle 24-104 = 80 cycles = 160 color cycles

// global variables
int solutions=1;

bool quiet=false;

void RastaConverter::Error(std::string e)
{
	gui.Error(e);
	DBG_PRINT("[RASTA] Fatal error: %s", e.c_str());
	exit(1);
}

int random(int range)
{
	if (range==0)
		return 0;
	return genrand_int32()%range;
}


void RastaConverter::Message(std::string message)
{
	if (quiet)
		return;

	time_t t;
	t = time(NULL);	
	string current_time = ctime(&t);
	current_time = current_time.substr(0, current_time.length() - 1);
	DBG_PRINT("[RASTA] %s", message.c_str());
	gui.DisplayText(0, 450, current_time + string(": ") + message + string("                    "));
}

using namespace std;

static rgb PIXEL2RGB(RGBQUAD &q)
{
	rgb x;
	x.b = q.rgbBlue;
	x.g = q.rgbGreen;
	x.r = q.rgbRed;
	x.a = q.rgbReserved;
	return x;
}

static RGBQUAD RGB2PIXEL(rgb& val)
{
	RGBQUAD fpixel;
	fpixel.rgbRed = val.r;
	fpixel.rgbGreen = val.g;
	fpixel.rgbBlue = val.b;
	return fpixel;
}


set < unsigned char > color_indexes_on_dst_picture;

OnOffMap on_off;

Evaluator eval;

const char *mem_regs_names[E_TARGET_MAX+1]=
{
	"COLOR0",
	"COLOR1",
	"COLOR2",
	"COLBAK",
	"COLPM0",
	"COLPM1",
	"COLPM2",
	"COLPM3",
	"HPOSP0",
	"HPOSP1",
	"HPOSP2",
	"HPOSP3",
	"HITCLR",
};

// create_cycles_table now defined in src/core/Cycles.cpp

const char *mutation_names[E_MUTATION_MAX]=
{
	"PushBack2Prev ",
	"Copy2NextLine ",
	"SwapWithPrevL ",
	"Add Instr     ",
	"Remove Instr  ",
	"Swap Instr    ",
	"Change Target ",
	"Change Value  ",
	"Chg Val to Col",
	"Dual Complement",
};

void resize_rgb_picture(vector < screen_line > *picture, size_t width, size_t height)
{
	size_t y;
	picture->resize(height);
	for (y=0;y<height;++y)
	{
		(*picture)[y].Resize(width);
	}
}

void RastaConverter::LoadAtariPalette()
{
	Message("Loading palette");
	if (!::LoadAtariPalette(cfg.palette_file))
		Error("Error opening .act palette file");
}

// Function to rescale FIBITMAP to double its width
FIBITMAP* RescaleFIBitmapDoubleWidth(FIBITMAP* originalFiBitmap) {
	int originalWidth = FreeImage_GetWidth(originalFiBitmap);
	int originalHeight = FreeImage_GetHeight(originalFiBitmap);

	// Calculate the new width as double the original width
	int newWidth = originalWidth * 2;

	// Use FreeImage_Rescale to create a new bitmap with the new dimensions
	FIBITMAP* rescaledFiBitmap = FreeImage_Rescale(originalFiBitmap, newWidth, originalHeight, FILTER_BOX);

	return rescaledFiBitmap;
}

bool RastaConverter::SavePicture(const std::string& filename, FIBITMAP* to_save)
{
	// Assuming to_save is already in the correct format and size
	// No need to create a new bitmap or stretch_blit

	FIBITMAP* stretched = RescaleFIBitmapDoubleWidth(to_save);

	// Flip the image vertically
	FreeImage_FlipVertical(stretched);

	// Save the image as a PNG
	if (FreeImage_Save(FIF_PNG, stretched, filename.c_str()))
	{
		// If the image is saved successfully
		FreeImage_Unload(stretched);
		return true;
	}
	else
	{
		// If there was an error saving the image
		Error(string("Error saving picture.")+filename);
		return false;
	}
}
void RastaConverter::SaveStatistics(const char *fn)
{
	FILE *f = fopen(fn, "w");
	if (!f)
		return;

	fprintf(f, "Iterations,Seconds,Score\n");
	for(statistics_list::const_iterator it(m_eval_gstate.m_statistics.begin()), itEnd(m_eval_gstate.m_statistics.end());
		it != itEnd;
		++it)
	{
		const statistics_point& pt = *it;

		fprintf(f, "%llu,%u,%.6f\n", (unsigned long long)pt.evaluations, pt.seconds, NormalizeScore(pt.distance));
	}

	fclose(f);
}

void RastaConverter::SaveOptimizerState(const char* fn) {
	FILE* f = fopen(fn, "wt+");
	if (!f)
		return;

	// Optimizer kind
	const char* opt = (m_eval_gstate.m_optimizer == EvalGlobalState::OPT_LAHC) ? "lahc" : "dlas";
	fprintf(f, "%s\n", opt);

	// Persist evaluation counters as unsigned long long
	fprintf(f, "%llu\n", (unsigned long long)m_eval_gstate.m_evaluations);
	fprintf(f, "%llu\n", (unsigned long long)m_eval_gstate.m_last_best_evaluation);

	// DLAS/LAHC state
	fprintf(f, "%lu\n", (unsigned long)m_eval_gstate.m_previous_results.size());
	fprintf(f, "%lu\n", (unsigned long)m_eval_gstate.m_previous_results_index);
	fprintf(f, "%Lf\n", (long double)m_eval_gstate.m_cost_max);
	fprintf(f, "%d\n", m_eval_gstate.m_N);
	fprintf(f, "%Lf\n", (long double)m_eval_gstate.m_current_cost);

	for (size_t i = 0; i < m_eval_gstate.m_previous_results.size(); ++i) {
		fprintf(f, "%Lf\n", (long double)m_eval_gstate.m_previous_results[i]);
	}
	fclose(f);
}

void RastaConverter::LoadOptimizerState(string name)
{
	FILE* f = fopen(name.c_str(), "rt");
	if (!f)
		return;

	char optbuf[32] = {0};
	if (!fgets(optbuf, sizeof(optbuf), f)) { fclose(f); return; }
	for (int i = 0; optbuf[i]; ++i) { if (optbuf[i] == '\n' || optbuf[i] == '\r') { optbuf[i] = 0; break; } }
	std::string opt = optbuf;
	for (auto &c : opt) c = (char)tolower(c);
	m_eval_gstate.m_optimizer = (opt == "lahc") ? EvalGlobalState::OPT_LAHC : EvalGlobalState::OPT_DLAS;

	// Load evaluation counters as unsigned long long
	unsigned long long evals = 0ULL;
	unsigned long long lastbest = 0ULL;
	fscanf(f, "%llu\n", &evals);
	fscanf(f, "%llu\n", &lastbest);
	m_eval_gstate.m_evaluations = evals;
	m_eval_gstate.m_last_best_evaluation = lastbest;

	unsigned long no_elements = 0;
	unsigned long index = 0;
	long double cost_max = 0;
	int N = 0;
	long double current_cost = 0;

	fscanf(f, "%lu\n", &no_elements);
	fscanf(f, "%lu\n", &index);
	fscanf(f, "%Lf\n", &cost_max);
	fscanf(f, "%d\n", &N);
	fscanf(f, "%Lf\n", &current_cost);

	m_eval_gstate.m_previous_results.clear();
	m_eval_gstate.m_previous_results.reserve(no_elements);
	for (size_t i = 0; i < (size_t)no_elements; ++i) {
		long double v = 0;
		fscanf(f, "%Lf\n", &v);
		m_eval_gstate.m_previous_results.push_back((double)v);
	}

	m_eval_gstate.m_previous_results_index = index;
	m_eval_gstate.m_cost_max = (double)cost_max;
	m_eval_gstate.m_N = N;
	m_eval_gstate.m_current_cost = (double)current_cost;

	fclose(f);
}

bool RastaConverter::LoadInputBitmap()
{
	Message("Loading and initializing file");
	input_bitmap = FreeImage_Load(FreeImage_GetFileType(cfg.input_file.c_str()), cfg.input_file.c_str(), 0);
	if (!input_bitmap)
		Error(string("Error loading input file: ") + cfg.input_file);

	unsigned int input_width=FreeImage_GetWidth(input_bitmap);
	unsigned int input_height=FreeImage_GetHeight(input_bitmap);

	if (cfg.height==-1) // set height automatic to keep screen proportions
	{
		double iw= (double) input_width;
		double ih= (double) input_height;
		if ( iw/ih > (320.0/240.0) ) // 4:3 = 320:240
		{
			ih=input_height / (input_width/320.0);
			cfg.height=(int) ih;
		}
		else
			cfg.height=240;
	}
	
	input_bitmap = FreeImage_Rescale(input_bitmap,cfg.width,cfg.height,cfg.rescale_filter);
	input_bitmap = FreeImage_ConvertTo24Bits(input_bitmap);

	FreeImage_AdjustBrightness(input_bitmap,cfg.brightness);
	FreeImage_AdjustContrast(input_bitmap,cfg.contrast);
	FreeImage_AdjustGamma(input_bitmap,cfg.gamma);

	FreeImage_FlipVertical(input_bitmap);

	m_height=(int) cfg.height;
	m_width=(int) cfg.width;

	return true;
}

void RastaConverter::InitLocalStructure()
{
	unsigned x,y;

	//////////////////////////////////////////////////////////////////////////
	// Set our structure size

	unsigned width = FreeImage_GetWidth(input_bitmap);
	unsigned height = FreeImage_GetHeight(input_bitmap);
	resize_rgb_picture(&m_picture, width, height);

	// Copy data to input_bitmap and to our structure
	RGBQUAD fpixel;
	rgb atari_color;
	for (y=0;y<height;++y)
	{
		for (x=0;x<width;++x)
		{
			FreeImage_GetPixelColor(input_bitmap, x, y, &fpixel);
			atari_color=PIXEL2RGB(fpixel);
			m_picture[y][x]=atari_color;
			fpixel.rgbRed=atari_color.r;
			fpixel.rgbGreen=atari_color.g;
			fpixel.rgbBlue=atari_color.b;
			FreeImage_SetPixelColor(input_bitmap, x, y, &fpixel);
		}
	}

	// Show our picture
	if (!cfg.preprocess_only)
	{
		ShowInputBitmap();
	}
}

void RastaConverter::LoadDetailsMap()
{
	Message("Loading details map");
	FIBITMAP *fbitmap = FreeImage_Load(FreeImage_GetFileType(cfg.details_file.c_str()), cfg.details_file.c_str(), 0);
	if (!fbitmap)
		Error(string("Error loading details file: ") + cfg.details_file);
	fbitmap=FreeImage_Rescale(fbitmap,cfg.width,cfg.height,FILTER_BOX);
	fbitmap = FreeImage_ConvertTo24Bits(fbitmap);	

	FreeImage_FlipVertical(fbitmap);

	RGBQUAD fpixel;

	int x,y;

	details_data.resize(m_height);	
	for (y=0;y<m_height;++y)
	{
		details_data[y].resize(m_width);

		for (x=0;x<m_width;++x)
		{
			FreeImage_GetPixelColor(fbitmap, x, y, &fpixel);
			// average as brightness
			details_data[y][x]=(unsigned char) ( (int) ( (int)fpixel.rgbRed + (int)fpixel.rgbGreen + (int)fpixel.rgbBlue)/3);
			fpixel.rgbRed = details_data[y][x];
			fpixel.rgbGreen = details_data[y][x];
			fpixel.rgbBlue = details_data[y][x];
			if ((x+y)%2==0)
				FreeImage_SetPixelColor(destination_bitmap, x, y, &fpixel);
		}
		ShowDestinationBitmap();
	}
	FreeImage_Unload(fbitmap);
};

void RastaConverter::GeneratePictureErrorMap()
{
	if (!cfg.details_file.empty())
		LoadDetailsMap();

	unsigned int details_multiplier=255;

	const int w = (int)FreeImage_GetWidth(input_bitmap);
	const int h = (int)FreeImage_GetHeight(input_bitmap);

	for(int i=0; i<128; ++i)
	{
		m_picture_all_errors[i].resize(w * h);

		const rgb ref = atari_palette[i];

		distance_t *dst = &m_picture_all_errors[i][0];
		for (int y=0; y<h; ++y)
		{
			const screen_line& srcrow = m_picture[y];

			if (!details_data.empty())
			{
				for (int x=0; x<w; ++x)
				{
					details_multiplier = 255+ (unsigned int)(((double)details_data[y][x])*cfg.details_strength);
					*dst++ = (details_multiplier*distance_function(srcrow[x], ref))/255;
				}
			}
			else
			{
				for (int x=0; x<w; ++x)
				{
					*dst++ = distance_function(srcrow[x], ref);
				}
			}
		}
	}
}

void RastaConverter::OtherDithering()
{
	int y;

	const int w = FreeImage_GetWidth(input_bitmap);
	const int h = FreeImage_GetHeight(input_bitmap);
	const int w1 = w - 1;

	for (y=0;y<h;++y)
	{
		const bool flip = y & 1;

		for (int i=0;i<w;++i)
		{
			int x = flip ? w1 - i : i;

			rgb out_pixel=m_picture[y][x];

			if (cfg.dither!=E_DITHER_NONE)
			{
				rgb_error p=error_map[y][x];
				p.r+=out_pixel.r;
				p.g+=out_pixel.g;
				p.b+=out_pixel.b;

				if (p.r>255)
					p.r=255;
				else if (p.r<0)
					p.r=0;

				if (p.g>255)
					p.g=255;
				else if (p.g<0)
					p.g=0;

				if (p.b>255)
					p.b=255;
				else if (p.b<0)
					p.b=0;

				out_pixel.r=(unsigned char)(p.r + 0.5);
				out_pixel.g=(unsigned char)(p.g + 0.5);
				out_pixel.b=(unsigned char)(p.b + 0.5);

				out_pixel=atari_palette[FindAtariColorIndex(out_pixel)];

				rgb in_pixel = m_picture[y][x];
				rgb_error qe;
				qe.r=(int)in_pixel.r-(int)out_pixel.r;
				qe.g=(int)in_pixel.g-(int)out_pixel.g;
				qe.b=(int)in_pixel.b-(int)out_pixel.b;

				if (cfg.dither==E_DITHER_FLOYD)
				{
					/* Standard Floyd-Steinberg uses 4 pixels to diffuse */
					DiffuseError( x-1, y,   7.0/16.0, qe.r,qe.g,qe.b);
					DiffuseError( x+1, y+1, 3.0/16.0, qe.r,qe.g,qe.b);
					DiffuseError( x  , y+1, 5.0/16.0, qe.r,qe.g,qe.b);
					DiffuseError( x-1, y+1, 1.0/16.0, qe.r,qe.g,qe.b);
				}
				else if (cfg.dither==E_DITHER_LINE)
				{
					// line dithering that reduces number of colors in line
					if (y%2==0)
					{
						DiffuseError( x, y+1, 0.5, qe.r,qe.g,qe.b);
					}
				}
				else if (cfg.dither==E_DITHER_LINE2)
				{
					// line dithering
					DiffuseError( x, y+1, 0.5, qe.r,qe.g,qe.b);
				}
				else if (cfg.dither==E_DITHER_CHESS)
				{
					// Chessboard dithering
					if ((x+y)%2==0)
					{
						DiffuseError( x+1, y,   0.5, qe.r,qe.g,qe.b);
						DiffuseError( x  , y+1, 0.5, qe.r,qe.g,qe.b);
					}
				}
				else if (cfg.dither==E_DITHER_SIMPLE)
				{
					DiffuseError( x+1, y,   1.0/3.0, qe.r,qe.g,qe.b);
					DiffuseError( x  , y+1, 1.0/3.0, qe.r,qe.g,qe.b);
					DiffuseError( x+1, y+1, 1.0/3.0, qe.r,qe.g,qe.b);
				}
				else if (cfg.dither==E_DITHER_2D)
				{
					DiffuseError( x+1, y,   2.0/4.0, qe.r,qe.g,qe.b);
					DiffuseError( x  , y+1, 1.0/4.0, qe.r,qe.g,qe.b);
					DiffuseError( x+1, y+1, 1.0/4.0, qe.r,qe.g,qe.b);
				}
				else if (cfg.dither==E_DITHER_JARVIS)
				{
					DiffuseError( x+1, y,   7.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x+2, y,   5.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x-1, y+1, 3.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x-2, y+1, 5.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x  , y+1, 7.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x+1, y+1, 5.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x+2, y+1, 3.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x-1, y+2, 1.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x-2, y+2, 3.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x  , y+2, 5.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x+1, y+2, 3.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x+2, y+2, 1.0/48.0, qe.r,qe.g,qe.b);
				}
			}
			unsigned char color_index=FindAtariColorIndex(out_pixel);
			color_indexes_on_dst_picture.insert(color_index);
			out_pixel = atari_palette[color_index];
			RGBQUAD color=RGB2PIXEL(out_pixel);
			FreeImage_SetPixelColor(destination_bitmap,x,y,&color);
		}
		ShowDestinationLine(y);
		// Keep UI responsive: pump events and present per line
		switch (gui.NextFrame())
		{
		case GUI_command::REDRAW:
			ShowInputBitmap();
			ShowDestinationBitmap();
			gui.Present();
			break;
		case GUI_command::SAVE:
		case GUI_command::STOP:
		case GUI_command::CONTINUE:
		case GUI_command::SHOW_A:
		case GUI_command::SHOW_B:
		case GUI_command::SHOW_MIX:
			break;
		}
		gui.Present();
	}
}

void RastaConverter::ShowInputBitmap()
{
	unsigned int width = FreeImage_GetWidth(input_bitmap);
	unsigned int height = FreeImage_GetHeight(input_bitmap);
	gui.DisplayBitmap(0, 0, input_bitmap);
	if (cfg.dual_mode)
	{
		gui.DisplayText(0, height + 10, "Source = Destination");
	}
	else
	{
		gui.DisplayText(0, height + 10, "Source");
		gui.DisplayText(width * 4, height + 10, "Destination");
	}
	gui.DisplayText(width * 2, height + 10, "Current output");
}

void RastaConverter::ShowDestinationLine(int y)
{
	if (!cfg.preprocess_only)
	{
		unsigned int width = FreeImage_GetWidth(destination_bitmap);
		unsigned int where_x = FreeImage_GetWidth(input_bitmap);

		gui.DisplayBitmapLine(where_x, y, y, destination_bitmap);
	}
}

void RastaConverter::ShowDestinationBitmap()
{
	gui.DisplayBitmap(FreeImage_GetWidth(destination_bitmap)*2, 0, destination_bitmap);
}



void RastaConverter::PrepareDestinationPicture()
{
	Message("Preparing Destination Picture");

	int width = FreeImage_GetWidth(input_bitmap);
	int height = FreeImage_GetHeight(input_bitmap);
	int bpp = FreeImage_GetBPP(input_bitmap); // Bits per pixel

	// Allocate a new bitmap with the same dimensions and bpp
	destination_bitmap = FreeImage_Allocate(width, height, bpp);

	RGBQUAD black = { 0, 0, 0, 255 }; // Assuming 32-bit image with alpha channel

	// Fill the new bitmap with black color
	FreeImage_FillBackground(destination_bitmap, &black, 0);


	// Draw new picture on the screen
	if (cfg.dither!=E_DITHER_NONE)
	{
		if (cfg.dither==E_DITHER_KNOLL)
			KnollDithering();
		else
		{
			ClearErrorMap();
			OtherDithering();
		}
	}
	else
	{
		for (int y=0;y<m_height;++y)
		{
			for (int x=0;x<m_width;++x)
			{
				rgb out_pixel=m_picture[y][x];
				unsigned char color_index=FindAtariColorIndex(out_pixel);
				color_indexes_on_dst_picture.insert(color_index);	
				out_pixel = atari_palette[color_index];
				RGBQUAD color = RGB2PIXEL(out_pixel);
				FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
			}
			ShowDestinationLine(y);
		}
	}

	if (!cfg.preprocess_only)
	{
		ShowDestinationBitmap();
	}

	int w = FreeImage_GetWidth(input_bitmap);
	int h = FreeImage_GetHeight(input_bitmap);


	for (int y=0;y<h;++y)
	{
		for (int x=0;x<w;++x)
		{
			RGBQUAD color;
			FreeImage_GetPixelColor(destination_bitmap, x, y, &color);
			rgb out_pixel=PIXEL2RGB(color);
			m_picture[y][x]=out_pixel; // copy it always - it is used by the color distance cache m_picture_all_errors
		}
	}
}

void RastaConverter::LoadOnOffFile(const char *filename)
{
	memset(on_off.on_off,true,sizeof(on_off.on_off));

	// 1. Resize the on_off table and full it with true
	fstream f;
	f.open( filename, ios::in);
	if ( f.fail())
		Error("Error loading OnOff file");

	string line;
	unsigned int y=1;
	while( getline( f, line)) 
	{
		if (line.empty())
			continue;
		std::transform(line.begin(), line.end(), line.begin(), ::toupper);

		stringstream sl(line);
		string reg, value;
		e_target target=E_TARGET_MAX;
		unsigned int from, to;

		sl >> reg >> value >> from >> to;

		if(sl.rdstate() == ios::failbit) // failed to parse arguments?
		{
			string err="Error parsing OnOff file in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}
		if (!(value=="ON" || value=="OFF"))
		{
			string err="OnOff file: Second parameter should be ON or OFF in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}
		if (from>239 || to>239) // on_off table size
		{
			string err="OnOff file: Range value greater than 239 line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}

		if ((int)from > m_height-1 || (int)to > m_height-1)
		{
			string err="OnOff file: Range value greater than picture height in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			err+="\n";
			err+="Set range from 0 to ";
			err+=Value2String<unsigned int>(y-1);
			Error(err.c_str());
		}
		for (size_t i=0;i<E_TARGET_MAX;++i)
		{
			if (reg==string(mem_regs_names[i]))
			{
				target=(e_target) i;
				break;
			}
		}
		if (target==E_TARGET_MAX)
		{
			string err="OnOff file: Unknown register " + reg;
			err+=" in line ";
			err+=Value2String<unsigned int>(y);
			err+="\n";
			err+=line;
			Error(err.c_str());
		}
		// fill 
		for (size_t l=from;l<=to;++l)
		{
			on_off.on_off[l][target] = (value=="ON");
		}
		++y;
	}
}

bool RastaConverter::ProcessInit()
{
	DBG_PRINT("[RASTA] ProcessInit start (dual=%d quiet=%d)", (int)cfg.dual_mode, (int)quiet);
	gui.Init(cfg.command_line);

	DBG_PRINT("[RASTA] LoadAtariPalette");
	LoadAtariPalette();
	DBG_PRINT("[RASTA] LoadInputBitmap");
	if (!LoadInputBitmap())
		Error("Error loading Input Bitmap!");

	DBG_PRINT("[RASTA] InitLocalStructure");
	InitLocalStructure();
	if (!cfg.preprocess_only)
		SavePicture(cfg.output_file+"-src.png",input_bitmap);

	// set preprocess distance function
	DBG_PRINT("[RASTA] SetDistanceFunction(pre)");
	SetDistanceFunction(cfg.pre_dstf);

	// Prepare destination picture for BOTH modes to keep bootstrap behavior identical to single-frame
	PrepareDestinationPicture();
	// Preserve original behavior of saving -dst only in single-frame mode
	if (!cfg.dual_mode)
		SavePicture(cfg.output_file+"-dst.png",destination_bitmap);

	if (cfg.preprocess_only)
		exit(1);

	if (!cfg.on_off_file.empty())
		LoadOnOffFile(cfg.on_off_file.c_str());

	// set postprocess distance function
	DBG_PRINT("[RASTA] SetDistanceFunction(post)");
	SetDistanceFunction(cfg.dstf);

	DBG_PRINT("[RASTA] GeneratePictureErrorMap");
	GeneratePictureErrorMap();

	m_eval_gstate.m_max_evals = cfg.max_evals;
	m_eval_gstate.m_save_period = cfg.save_period;

	for(int i=0; i<128; ++i)
		m_picture_all_errors_array[i] = m_picture_all_errors[i].data();

	DBG_PRINT("[RASTA] Create %d evaluator(s)", cfg.threads);
	m_evaluators.resize(cfg.threads);

	unsigned long long randseed = cfg.initial_seed;

	for(size_t i=0; i<m_evaluators.size(); ++i)
	{
		// seed=0 would lock up the LFSR
		if (!randseed)
			++randseed;

		m_evaluators[i].Init(m_width, m_height, m_picture_all_errors_array, m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off, &m_eval_gstate, solutions, randseed, cfg.cache_size);

		randseed += 187927 * i;
	}

	m_eval_gstate.m_thread_count = cfg.threads;
	// Propagate optimizer selection (default LAHC)
	m_eval_gstate.m_optimizer = (cfg.optimizer == Configuration::E_OPT_LAHC) ? EvalGlobalState::OPT_LAHC : EvalGlobalState::OPT_DLAS;
	// Configure aggressive search trigger
	m_eval_gstate.m_unstuck_after = cfg.unstuck_after;
	m_eval_gstate.m_unstuck_drift_norm = cfg.unstuck_drift_norm;

	// When initializing evaluators, pass thread ID:
	for (size_t i = 0; i < m_evaluators.size(); ++i)
	{
		// seed=0 would lock up the LFSR
		if (!randseed)
			++randseed;

		m_evaluators[i].Init(m_width, m_height, m_picture_all_errors_array,
			m_picture.data(), cfg.on_off_file.empty() ? NULL : &on_off,
			&m_eval_gstate, solutions, randseed, cfg.cache_size, i);

		randseed += 187927 * i;
	}

	return true;
}


unsigned char ConvertColorRegisterToRawData(e_target t)
{
	if (t>E_COLBAK)
		t=E_COLBAK;
	switch (t)
	{
	case E_COLBAK:
		return 0;
	case E_COLOR0:
		return 1;
	case E_COLOR1:
		return 2;
	case E_COLOR2:
		return 3;
	default:
		;
	}
	assert(0); // this should never happen
	return -1;
}

bool RastaConverter::SaveScreenData(const char *filename)
{
	int x,y,a=0,b=0,c=0,d=0;
	FILE *fp=fopen(filename,"wb+");
	if (!fp)
		Error("Error saving MIC screen data");

	Message("Saving screen data");
	for(y=0;y<m_height;++y)
	{
		// encode 4 pixel colors in byte

		for (x=0;x<m_width;x+=4)
		{
			unsigned char pix=0;
			a=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x]);
			b=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x+1]);
			c=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x+2]);
			d=ConvertColorRegisterToRawData((e_target)m_eval_gstate.m_created_picture_targets[y][x+3]);
			pix |= a<<6;
			pix |= b<<4;
			pix |= c<<2;
			pix |= d;
			fwrite(&pix,1,1,fp);
		}
	}
	fclose(fp);
	return true;
}


void RastaConverter::SetConfig(Configuration &a_c)
{
	cfg=a_c;
}

/* 8x8 threshold map */
static const unsigned char threshold_map[8*8] = {
	0,48,12,60, 3,51,15,63,
	32,16,44,28,35,19,47,31,
	8,56, 4,52,11,59, 7,55,
	40,24,36,20,43,27,39,23,
	2,50,14,62, 1,49,13,61,
	34,18,46,30,33,17,45,29,
	10,58, 6,54, 9,57, 5,53,
	42,26,38,22,41,25,37,21 };

/* Luminance for each palette entry, to be initialized as soon as the program begins */
static unsigned luma[128];

bool PaletteCompareLuma(unsigned index1, unsigned index2)
{
	return luma[index1] < luma[index2];
}

double ColorCompare(int r1,int g1,int b1, int r2,int g2,int b2)
{
	double luma1 = (r1*299 + g1*587 + b1*114) / (255.0*1000);
	double luma2 = (r2*299 + g2*587 + b2*114) / (255.0*1000);
	double lumadiff = luma1-luma2;
	double diffR = (r1-r2)/255.0, diffG = (g1-g2)/255.0, diffB = (b1-b2)/255.0;
	return (diffR*diffR*0.299 + diffG*diffG*0.587 + diffB*diffB*0.114)*0.75
		+ lumadiff*lumadiff;
}

struct MixingPlan
{
    unsigned colors[64];
};

double random_plus_minus(double val)
{
	double result;
	int val2=100.0*val;
	result = random(val2);
	if (random(2))
		result*=-1;
	return result/100.0;
}


MixingPlan RastaConverter::DeviseBestMixingPlan(rgb color)
{
	MixingPlan result = { {0} };
	const double X = cfg.dither_strength/100; // Error multiplier
	rgb src=color;
	rgb_error e;
	e.zero(); // Error accumulator
	for(unsigned c=0; c<64; ++c)
	{
		// Current temporary value
		rgb_error temp;
		temp.r = src.r + e.r * X *(1+random_plus_minus(cfg.dither_randomness));
		temp.g = src.g + e.g * X *(1+random_plus_minus(cfg.dither_randomness));
		temp.b = src.b + e.b * X *(1+random_plus_minus(cfg.dither_randomness));

		// Clamp it in the allowed RGB range
		if(temp.r<0) temp.r=0; else if(temp.r>255) temp.r=255;
		if(temp.g<0) temp.g=0; else if(temp.g>255) temp.g=255;
		if(temp.b<0) temp.b=0; else if(temp.b>255) temp.b=255;
		// Find the closest color from the palette
		double least_penalty = 1e99;
		unsigned chosen = c%128;
		for(unsigned index=0; index<128; ++index)
		{
			rgb color2;
			color2.r=temp.r;
			color2.g=temp.g;
			color2.b=temp.b;

			double penalty = distance_function(atari_palette[index], color2);
			if(penalty < least_penalty)
			{ 
				least_penalty = penalty; 
				chosen=index; 
			}
		}
		// Add it to candidates and update the error
		result.colors[c] = chosen;
		rgb color = atari_palette[chosen];
		e.r += src.r-color.r;
		e.g += src.g-color.g;
		e.b += src.b-color.b;
	}
	// Sort the colors according to luminance
	std::sort(result.colors, result.colors+64, PaletteCompareLuma);
	return result;
}

void RastaConverter::ParallelFor(int from, int to, void *(*start_routine)(void*))
{
	void *status;
	vector<std::thread> threads;
	vector<parallel_for_arg_t> threads_arg;
	/* Initialize and set thread detached attribute */

	threads.reserve(cfg.threads);
	threads_arg.resize(cfg.threads);

	int step=abs(to-from)/cfg.threads;
	for (int t=0;t<cfg.threads;++t)
	{
		threads_arg[t].this_ptr=this;
		threads_arg[t].from=from;
		if (t==cfg.threads-1) // last one
			threads_arg[t].to=to;
		else
			threads_arg[t].to=from+step;
		threads.emplace_back( std::bind( start_routine, ( void* )&threads_arg[t] ) );
		from+=step;
	}
	for (int t=0;t<cfg.threads;++t)
	{
		threads[t].join();
	}
	return;
}

// This is still quite ugly, KnollDitheringParallel should be passed in *arg so this helper could be generic
void *RastaConverter::KnollDitheringParallelHelper(void *arg)
{
	parallel_for_arg_t *param=(parallel_for_arg_t *)arg;
	((RastaConverter *)param->this_ptr)->KnollDitheringParallel(param->from,param->to);
	return NULL;
}

void RastaConverter::KnollDitheringParallel(int from, int to)
{
	std::vector<unsigned char> local_line;
	local_line.resize((size_t)m_width);
	std::set<unsigned char> local_indices;
	for(int y=from; y<to; ++y)
	{
		local_indices.clear();
		for(unsigned x=0; x<(unsigned)m_width; ++x)
		{
			rgb r_color = m_picture[y][x];
			unsigned map_value = threshold_map[(x & 7) + ((y & 7) << 3)];
			MixingPlan plan = DeviseBestMixingPlan(r_color);
			unsigned char color_index=plan.colors[ map_value ];
			local_line[x] = color_index;
			local_indices.insert(color_index);
		}
		for(unsigned x=0; x<(unsigned)m_width; ++x)
		{
			rgb out_pixel = atari_palette[ local_line[x] ];
			m_picture[y][x] = out_pixel;
		}
		{
			std::lock_guard<std::mutex> lock(m_color_set_mutex);
			for (auto v : local_indices) color_indexes_on_dst_picture.insert(v);
		}
		// Mark this line ready so the main thread can draw it
		if (m_knoll_line_ready)
			m_knoll_line_ready[(size_t)y].store(1, std::memory_order_release);
	}
}

void RastaConverter::KnollDithering()
{
	Message("Knoll Dithering             ");
	for(unsigned c=0; c<128; ++c)
	{
		luma[c] = atari_palette[c].r*299 + atari_palette[c].g*587 + atari_palette[c].b*114;
	}
	// Initialize progress flags for multi-threaded readiness
	m_knoll_line_ready.reset(new std::atomic<unsigned char>[(size_t)m_height]);
	m_knoll_line_drawn.assign((size_t)m_height, 0);
	for (int i=0;i<m_height;++i) m_knoll_line_ready[(size_t)i].store(0, std::memory_order_relaxed);
	// Show initial empty destination area so window isn't blank
	ShowDestinationBitmap();
	gui.Present();
	// Launch workers (non-blocking) with robust partitioning
	std::vector<std::thread> threads;
	std::vector<parallel_for_arg_t> threads_arg;
	int total_threads = std::max(1, cfg.threads);
	threads.reserve(total_threads);
	threads_arg.resize(total_threads);
	int from=0;
	int to=m_height;
	int step = (to - from + total_threads - 1) / total_threads; // ceil division
	for (int t=0;t<total_threads;++t)
	{
		threads_arg[t].this_ptr=this;
		threads_arg[t].from=from;
		int tto = from + step;
		if (t==total_threads-1 || tto>to) tto = to;
		threads_arg[t].to=tto;
		threads.emplace_back( std::bind( KnollDitheringParallelHelper, ( void* )&threads_arg[t] ) );
		from = tto;
	}
	// Progressive commit loop: draw lines as they become ready
	int next_to_draw = 0;
	int presented_until = -1;
	while (next_to_draw < m_height)
	{
		// Draw any contiguous ready lines starting from next_to_draw
		while (next_to_draw < m_height && m_knoll_line_ready[(size_t)next_to_draw].load(std::memory_order_acquire))
		{
			if (!m_knoll_line_drawn[(size_t)next_to_draw])
			{
				for (int x=0; x<m_width; ++x)
				{
					rgb out_pixel = m_picture[next_to_draw][x];
					RGBQUAD color=RGB2PIXEL(out_pixel);
					FreeImage_SetPixelColor(destination_bitmap, x, next_to_draw, &color);
				}
				ShowDestinationLine(next_to_draw);
				m_knoll_line_drawn[(size_t)next_to_draw] = 1;
			}
			++next_to_draw;
		}
		// Also draw any out-of-order ready lines to avoid waiting on the first line
		int drawn_this_iter = 0;
		for (int y=0; y<m_height && drawn_this_iter<8; ++y)
		{
			if (!m_knoll_line_drawn[(size_t)y] && m_knoll_line_ready[(size_t)y].load(std::memory_order_acquire))
			{
				for (int x=0; x<m_width; ++x)
				{
					rgb out_pixel = m_picture[y][x];
					RGBQUAD color=RGB2PIXEL(out_pixel);
					FreeImage_SetPixelColor(destination_bitmap, x, y, &color);
				}
				ShowDestinationLine(y);
				m_knoll_line_drawn[(size_t)y] = 1;
				++drawn_this_iter;
			}
		}
		// Pump events and present periodically to keep UI responsive
		switch (gui.NextFrame())
		{
		case GUI_command::REDRAW:
			ShowInputBitmap();
			ShowDestinationBitmap();
			gui.Present();
			break;
		case GUI_command::SAVE:
		case GUI_command::STOP:
		case GUI_command::CONTINUE:
		case GUI_command::SHOW_A:
		case GUI_command::SHOW_B:
		case GUI_command::SHOW_MIX:
			break;
		}
		if (presented_until != next_to_draw)
		{
			gui.Present();
			presented_until = next_to_draw;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	// Join workers before leaving
	for (int t=0;t<total_threads;++t)
	{
		threads[t].join();
	}
}

void RastaConverter::ClearErrorMap()
{
	// set proper size if empty
	if (error_map.empty())
	{
		error_map.resize(m_height);
		for (int y=0;y<m_height;++y)
		{
			error_map[y].resize(m_width+1);
		}
	}
	// clear the map
	for (int y=0;y<m_height;++y)
	{
		for (int x=0;x<m_width;++x)
		{
			error_map[y][x].zero();	
		}
	}
}

void RastaConverter::CreateLowColorRasterPicture(raster_picture *r)
{
	CreateEmptyRasterPicture(r);
	set < unsigned char >::iterator m,_m;
	int i=0;
	for (m=color_indexes_on_dst_picture.begin(),_m=color_indexes_on_dst_picture.end();m!=_m;++m,++i)
	{
		r->mem_regs_init[E_COLOR0+i]=(*m)*2;
	}
}


void RastaConverter::CreateEmptyRasterPicture(raster_picture *r)
{
	memset(r->mem_regs_init,0,sizeof(r->mem_regs_init));
	SRasterInstruction i;
	i.loose.instruction=E_RASTER_NOP;
	i.loose.target=E_COLBAK;
	i.loose.value=0;
	FreeImage_GetWidth(input_bitmap);
	// in line 0 we set init registers
	for (size_t y=0;y<r->raster_lines.size();++y)
	{
		r->raster_lines[y].instructions.push_back(i);
		r->raster_lines[y].cycles+=2;
		r->raster_lines[y].rehash();
	}
}

void RastaConverter::CreateSmartRasterPicture(raster_picture *r)
{
	SRasterInstruction i;
	int dest_colors;
	int dest_regs;
	int x,y;
	rgb color;

	memset(r->mem_regs_init,0,sizeof(r->mem_regs_init));

	dest_regs=8;

	if (cfg.init_type==E_INIT_LESS)
		dest_colors=dest_regs;
	else
		dest_colors=dest_regs+4;


	int width = FreeImage_GetWidth(input_bitmap);
	// in line 0 we set init registers

//	FreeImage_FlipVertical(input_bitmap);
	FIBITMAP *f_copy = FreeImage_Copy(input_bitmap,0,1,width,0);
//	FreeImage_FlipVertical(input_bitmap);

	for (y=0;y<(int)r->raster_lines.size();++y)
	{
		RGBQUAD fpixel;
		rgb atari_color;
		for (x=0;x<m_width;++x)
		{
			atari_color=m_picture[y][x];
			fpixel.rgbRed=atari_color.r;
			fpixel.rgbGreen=atari_color.g;
			fpixel.rgbBlue=atari_color.b;
			FreeImage_SetPixelColor(f_copy, x, 0, &fpixel);
		}
		FIBITMAP *f_copy24bits2;
		// create new picture from line y 
		FIBITMAP *f_copy24bits = FreeImage_ConvertTo24Bits(f_copy);	
		// quantize it 
		FIBITMAP *f_quant = FreeImage_ColorQuantizeEx(f_copy24bits,FIQ_WUQUANT,dest_colors);
		if (dest_colors>4)
			f_copy24bits2 = FreeImage_ConvertTo24Bits(f_quant);
		else
			f_copy24bits2 = FreeImage_ConvertTo24Bits(f_copy);


		map <int,int > color_map;
		map <int,int >::iterator j,_j;
		multimap <int,int, greater <int> > sorted_colors;
		multimap <int,int, greater <int> >::iterator m;
		map <int,int> color_position;
		for (x=0;x<width;++x)
		{
			RGBQUAD fpixel;
			FreeImage_GetPixelColor(f_copy24bits2, x,0, &fpixel);
			int c = fpixel.rgbRed + fpixel.rgbGreen * 0x100 + fpixel.rgbBlue * 0x10000;
			color_map[c]++;
			if (color_position.find(c)==color_position.end())
			{
				color_position[c]=x;
			}
		}

		// copy colors to sorted
		for (j=color_map.begin(),_j=color_map.end();j!=_j;++j)
		{
			sorted_colors.insert(pair<int,int>(j->second,j->first));
		}


		// convert colors to series of LDA/STA in order of appearance. Ignore for now(?) regs in prev line

		m=sorted_colors.begin();
		for (int k=0;k<dest_regs && k<(int)sorted_colors.size();++k,++m)
		{
			int c=m->second;
			color.r=c & 0xFF;
			color.g=(c>>8) & 0xFF;
			color.b=(c>>16) & 0xFF;

			// lda
			i.loose.instruction=(e_raster_instruction) (E_RASTER_LDA+k%3); // k%3 to cycle through A,X,Y regs
			if (k>E_COLBAK && y%2==1 && dest_colors>4)
				i.loose.value=(e_target) color_position[k]+sprite_screen_color_cycle_start; // sprite position
			else
				i.loose.value=FindAtariColorIndex(color)*2;
			i.loose.target=E_COLOR0;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=2;

			// sta 
			i.loose.instruction=(e_raster_instruction) (E_RASTER_STA+k%3); // k%3 to cycle through A,X,Y regs
			i.loose.value=(random(128)*2);

			if (k>E_COLBAK && y%2==1 && dest_colors>4)
				i.loose.target=(e_target) (k+4); // position
			else
				i.loose.target=(e_target) k;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=4;	

			assert(r->raster_lines[y].cycles<free_cycles);
		}

		r->raster_lines[y].rehash();

		FreeImage_Unload(f_copy24bits);
		FreeImage_Unload(f_quant);
		FreeImage_Unload(f_copy24bits2);
	}
	FreeImage_Unload(f_copy);
}

void RastaConverter::CreateRandomRasterPicture(raster_picture *r)
{
	SRasterInstruction i;
	int x;
	memset(r->mem_regs_init,0,sizeof(r->mem_regs_init));

	x=random(m_width); 
	r->mem_regs_init[E_COLPM0]=FindAtariColorIndex(m_picture[0][x])*2;
	r->mem_regs_init[E_HPOSP0]=x+sprite_screen_color_cycle_start;

	x=random(m_width); 
	r->mem_regs_init[E_COLPM1]=FindAtariColorIndex(m_picture[0][x])*2;
	r->mem_regs_init[E_HPOSP1]=x+sprite_screen_color_cycle_start;

	x=random(m_width); 
	r->mem_regs_init[E_COLPM2]=FindAtariColorIndex(m_picture[0][x])*2;
	r->mem_regs_init[E_HPOSP2]=x+sprite_screen_color_cycle_start;

	x=random(m_width); 
	r->mem_regs_init[E_COLPM3]=FindAtariColorIndex(m_picture[0][x])*2;
	r->mem_regs_init[E_HPOSP3]=x+sprite_screen_color_cycle_start;

	for (size_t y=0;y<r->raster_lines.size();++y)
	{
		// lda random
		i.loose.instruction=E_RASTER_LDA;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLOR0;
		r->raster_lines[y].instructions.push_back(i);
		// sta 
		i.loose.instruction=E_RASTER_STA;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLOR0;
		r->raster_lines[y].instructions.push_back(i);

		// ldx random
		i.loose.instruction=E_RASTER_LDX;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLOR1;
		r->raster_lines[y].instructions.push_back(i);
		// stx 
		i.loose.instruction=E_RASTER_STX;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLOR1;
		r->raster_lines[y].instructions.push_back(i);

		// ldy random
		i.loose.instruction=E_RASTER_LDY;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLOR2;
		r->raster_lines[y].instructions.push_back(i);
		// sty 
		i.loose.instruction=E_RASTER_STY;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLOR2;
		r->raster_lines[y].instructions.push_back(i);

		// lda random
		i.loose.instruction=E_RASTER_LDA;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.loose.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.loose.target=E_COLBAK;
		r->raster_lines[y].instructions.push_back(i);
		// sty 
		i.loose.instruction=E_RASTER_STA;
		r->raster_lines[y].cycles+=4;
		i.loose.value=(random(128)*2);
		i.loose.target=E_COLBAK;
		r->raster_lines[y].instructions.push_back(i);

		assert(r->raster_lines[y].cycles<free_cycles);
	}
}

void RastaConverter::DiffuseError( int x, int y, double quant_error, double e_r,double e_g,double e_b)
{
	if (! (x>=0 && x<m_width && y>=0 && y<m_height) )
		return;

	rgb_error p = error_map[y][x];
	p.r += e_r * quant_error*cfg.dither_strength*(1+random_plus_minus(cfg.dither_randomness));
	p.g += e_g * quant_error*cfg.dither_strength*(1+random_plus_minus(cfg.dither_randomness));
	p.b += e_b * quant_error*cfg.dither_strength*(1+random_plus_minus(cfg.dither_randomness));
	if (p.r>255)
		p.r=255;
	else if (p.r<0)
		p.r=0;
	if (p.g>255)
		p.g=255;
	else if (p.g<0)
		p.g=0;
	if (p.b>255)
		p.g=255;
	else if (p.g<0)
		p.g=0;
	error_map[y][x]=p;
}

void RastaConverter::OptimizeRasterProgram(raster_picture *pic)
{
	struct previous_reg_usage {
		int i;
		int y;
	};
	/*
		E_RASTER_LDA,
		E_RASTER_LDX,
		E_RASTER_LDY,
	*/

	previous_reg_usage p_usage[3]=  // a,x,y;
	{ 
		{ -1, -1 },
		{ -1, -1 },
		{ -1, -1 }
	};

	for (int y=0;y<m_height;++y)
	{
		size_t size=pic->raster_lines[y].instructions.size();
		SRasterInstruction *__restrict rastinsns = &pic->raster_lines[y].instructions[0];
		for (size_t i=0;i<size;++i)
		{
			unsigned char ins=rastinsns[i].loose.instruction;
			if (ins<=E_RASTER_LDY)
			{
				if (p_usage[ins].i != -1)
				{
					// nop previous usage of this register
					pic->raster_lines[ p_usage[ins].y ].instructions[ p_usage[ins].i ].loose.instruction=E_RASTER_NOP;
				}
				p_usage[ins].i=i;
				p_usage[ins].y=y;
			}
			else if (ins>=E_RASTER_STA)
			{
				p_usage[ins-E_RASTER_STA].i=-1;
			}

		}	
	}
}

void RastaConverter::FindPossibleColors()
{
	m_eval_gstate.m_possible_colors_for_each_line.resize(m_height);
	set < unsigned char > set_of_colors;

	// For each screen line set the possible colors
	vector < unsigned char > vector_of_colors;
	for (int l=m_height-1;l>=0;--l)
	{
		for (int x=0;x<m_width;++x)
			set_of_colors.insert(FindAtariColorIndex(m_picture[l][x])*2);				

		// copy set to vector
		vector_of_colors.resize(set_of_colors.size());
		copy(set_of_colors.begin(), set_of_colors.end(), vector_of_colors.data());
		m_eval_gstate.m_possible_colors_for_each_line[l]=vector_of_colors;
	}
}

void RastaConverter::Init()
{
	if (!cfg.continue_processing)
	{
		raster_picture m(m_height);
		init_finished=false;

		if (color_indexes_on_dst_picture.size()<5)
			CreateLowColorRasterPicture(&m);
		else if (cfg.init_type==E_INIT_RANDOM)
			CreateRandomRasterPicture(&m);
		else if (cfg.init_type==E_INIT_EMPTY)
			CreateEmptyRasterPicture(&m);
		else // LESS or SMART
			CreateSmartRasterPicture(&m);

		m_eval_gstate.m_best_pic = m;
	}

	init_finished=true;
}

void RastaConverter::TestRasterProgram(raster_picture *pic)
{
	int x,y;
	rgb white;
	rgb black;
	white.g=white.b=white.r=255;
	black.g=black.b=black.r=0;

	for (y=0;y<m_height;++y)
	{
		pic->raster_lines[y].cycles=6;
		pic->raster_lines[y].instructions.resize(2);
		pic->raster_lines[y].instructions[0].loose.instruction=E_RASTER_LDA;
		if (y%2==0)
			pic->raster_lines[y].instructions[0].loose.value=0xF;
		else
			pic->raster_lines[y].instructions[0].loose.value=0x33;

		pic->raster_lines[y].instructions[1].loose.instruction=E_RASTER_STA;
		pic->raster_lines[y].instructions[1].loose.target=E_COLOR2;


		for (x=0;x<m_width;++x)
			m_picture[y][x]=black;
		for (int i=0;i<CYCLES_MAX;++i)
		{
			x=screen_cycles[i].offset;
			if (x>=0 && x<m_width)
				m_picture[y][x]=white;
		}
	}
}

void RastaConverter::ShowMutationStats()
{
	int row = 0;
	for (int i=0;i<E_MUTATION_MAX;++i)
	{
		// Show dual-only mutation stat only in dual mode
		if (!cfg.dual_mode && i == E_MUTATION_COMPLEMENT_VALUE_DUAL) continue;
		gui.DisplayText(0, 230 + 20 * row, string(mutation_names[i]) + string("  ") + format_with_commas(m_eval_gstate.m_mutation_stats[i]));
		++row;
	}

	gui.DisplayText(320, 250, string("Evaluations: ") + format_with_commas(m_eval_gstate.m_evaluations));
	gui.DisplayText(320, 270, string("LastBest: ") + format_with_commas(m_eval_gstate.m_last_best_evaluation) + string("                "));
	gui.DisplayText(320, 290, string("Rate: ") + format_with_commas((unsigned long long)m_rate) + string("                "));
	{
		double norm = NormalizeScore(m_eval_gstate.m_best_result);
		std::string line = std::string("Norm. Dist: ") + format_with_commas(norm);
		// Show current normalized drift if active
		if (m_eval_gstate.m_current_norm_drift > 0.0 && m_eval_gstate.m_unstuck_after > 0 && m_eval_gstate.m_evaluations > m_eval_gstate.m_last_best_evaluation) {
			unsigned long long plateau = m_eval_gstate.m_evaluations - m_eval_gstate.m_last_best_evaluation;
			if (plateau >= m_eval_gstate.m_unstuck_after) {
				line += std::string(" (+") + format_with_commas(m_eval_gstate.m_current_norm_drift) + std::string(")");
			}
		}
		line += std::string("                ");
		gui.DisplayText(320, 310, line);
	}

	// Additional dual-mode status lines
	if (cfg.dual_mode)
	{
		const bool focusB = m_eval_gstate.m_dual_stage_focus_B.load(std::memory_order_relaxed);
		const char *showing = (m_dual_display == DualDisplayMode::A) ? "A" : (m_dual_display == DualDisplayMode::B) ? "B" : "M";
		EvalGlobalState::DualPhase phase = m_eval_gstate.m_dual_phase.load(std::memory_order_relaxed);
		std::string phaseText;
		switch (phase) {
			case EvalGlobalState::DUAL_PHASE_BOOTSTRAP_A: phaseText = "Phase: Bootstrap A"; break;
			case EvalGlobalState::DUAL_PHASE_BOOTSTRAP_B: {
				bool copied = m_eval_gstate.m_dual_bootstrap_b_copied.load(std::memory_order_relaxed);
				phaseText = copied ? "Phase: Bootstrap B (copy)" : "Phase: Bootstrap B (generate)"; break;
			}
			case EvalGlobalState::DUAL_PHASE_ALTERNATING: phaseText = std::string("Phase: Alternating, optimizing ") + (focusB ? "B" : "A"); break;
			default: phaseText = "Phase: -"; break;
		}
		gui.DisplayText(320, 330, phaseText);
		gui.DisplayText(320, 350, std::string("Showing: ") + showing);
		gui.DisplayText(320, 370, "Press [A] [B] [M]ix");
	}
}

void RastaConverter::SaveBestSolution()
{
	if (!init_finished)
		return;

	// Note that we are assuming that we have exclusive access to global state.

	if (!cfg.dual_mode) {
		raster_picture pic = m_eval_gstate.m_best_pic;

		ShowLastCreatedPicture();
		SaveRasterProgram(string(cfg.output_file+".rp"), &pic);
		OptimizeRasterProgram(&pic);
		SaveRasterProgram(string(cfg.output_file+".opt"), &pic);
		SavePMG(string(cfg.output_file+".pmg"));
		SaveScreenData  (string(cfg.output_file+".mic").c_str());
		SavePicture     (cfg.output_file,output_bitmap);
		SaveStatistics((cfg.output_file+".csv").c_str());
		SaveOptimizerState((cfg.output_file+".optstate").c_str());
		return;
	}

	// Dual-mode saving: save A, B, and blended
	ShowLastCreatedPictureDual();

	// Build directory prefix from cfg.output_file so dual outputs go to the same folder
	std::string __out_dir_prefix;
	{
		std::string __of = cfg.output_file;
		size_t __pos = __of.find_last_of("/\\");
		__out_dir_prefix = (__pos == std::string::npos) ? std::string() : __of.substr(0, __pos + 1);
	}

	// A
	{
		raster_picture picA = m_eval_gstate.m_best_pic;
		SaveRasterProgram(__out_dir_prefix + string("out_dual_A.rp"), &picA);
		OptimizeRasterProgram(&picA);
		SaveRasterProgram(__out_dir_prefix + string("out_dual_A.opt"), &picA);
		SavePMGWithSprites(__out_dir_prefix + string("out_dual_A.pmg"), m_eval_gstate.m_sprites_memory);
		SaveScreenDataFromTargets((__out_dir_prefix + string("out_dual_A.mic")).c_str(), m_eval_gstate.m_created_picture_targets);
		if (output_bitmap_A) SavePicture(__out_dir_prefix + string("out_dual_A.png"), output_bitmap_A);
	}
	// B
	{
		raster_picture picB = m_best_pic_B.raster_lines.empty() ? m_eval_gstate.m_best_pic : m_best_pic_B;
		SaveRasterProgram(__out_dir_prefix + string("out_dual_B.rp"), &picB);
		OptimizeRasterProgram(&picB);
		SaveRasterProgram(__out_dir_prefix + string("out_dual_B.opt"), &picB);
		SavePMGWithSprites(__out_dir_prefix + string("out_dual_B.pmg"), m_sprites_memory_B);
		if (m_created_picture_targets_B.empty()) m_created_picture_targets_B = m_eval_gstate.m_created_picture_targets; // fallback
		SaveScreenDataFromTargets  ((__out_dir_prefix + string("out_dual_B.mic")).c_str(), m_created_picture_targets_B);
		if (output_bitmap_B) SavePicture(__out_dir_prefix + string("out_dual_B.png"), output_bitmap_B);
	}
	// Blended
	if (output_bitmap_blended) SavePicture(__out_dir_prefix + string("out_dual_blended.png"), output_bitmap_blended);

	// Stats
	SaveStatistics((cfg.output_file+".csv").c_str());
	SaveOptimizerState((cfg.output_file+".optstate").c_str());
}

RastaConverter::RastaConverter()
	: init_finished(false)
{
}

void RastaConverter::MainLoop()
{
	Message("Optimization started.");

	output_bitmap = FreeImage_Allocate(cfg.width, cfg.height, 24);
	if (cfg.dual_mode) {
		output_bitmap_A = FreeImage_Allocate(cfg.width, cfg.height, 24);
		output_bitmap_B = FreeImage_Allocate(cfg.width, cfg.height, 24);
		output_bitmap_blended = FreeImage_Allocate(cfg.width, cfg.height, 24);
	}

	DBG_PRINT("[RASTA] MainLoop start (dual=%d)", (int)cfg.dual_mode);

	FindPossibleColors();

	Init();

	// Mark optimization start time for statistics (seconds since start)
	m_eval_gstate.m_time_start = time(NULL);
	m_previous_save_time = std::chrono::steady_clock::now();

	bool clean_first_evaluation = cfg.continue_processing;
	auto last_rate_check_tp = std::chrono::steady_clock::now();

	bool pending_update = false;

	// spin up only one evaluator -- we need its result before the rest can go, unless in dual mode
	// Do not hold the lock across UI work; acquire on demand
	std::unique_lock<std::mutex> lock{ m_eval_gstate.m_mutex, std::defer_lock };
	lock.lock();
	if (!cfg.dual_mode) {
		m_evaluators[0].Start();
	}

	unsigned long long last_eval = 0;
	bool eval_inited = false;

	if (cfg.dual_mode) {
		lock.unlock();
		DBG_PRINT("[RASTA] Enter MainLoopDual");
		MainLoopDual();
		return;
	}

	bool running = true;
	while (running)
	{

		// Release global lock during UI/rendering to avoid blocking workers
		if (lock.owns_lock()) lock.unlock();

		if (eval_inited && !cfg.quiet)
		{
			auto next_rate_check_tp = std::chrono::steady_clock::now();

			double secs = std::chrono::duration<double>(next_rate_check_tp - last_rate_check_tp).count();
			if (secs > 0.25)
			{
				m_rate = (double)(m_eval_gstate.m_evaluations - last_eval) / secs;

				last_rate_check_tp = next_rate_check_tp;
				last_eval = m_eval_gstate.m_evaluations;

				if (pending_update)
				{
					pending_update = false;
					ShowLastCreatedPicture();
				}

				ShowMutationStats();

				switch (gui.NextFrame())
				{
				case GUI_command::SAVE:
					SaveBestSolution();
					Message("Saved.");
					break;
				case GUI_command::STOP:
					running = false;
					break;
				case GUI_command::CONTINUE:
					break;
				case GUI_command::REDRAW:
					ShowInputBitmap();
					if (destination_bitmap) ShowDestinationBitmap();
					if (cfg.dual_mode) ShowLastCreatedPictureDual(); else ShowLastCreatedPicture();
					ShowMutationStats();
					gui.Present();
					break;
				case GUI_command::SHOW_A:
					if (cfg.dual_mode) { m_dual_display = DualDisplayMode::A; ShowLastCreatedPictureDual(); }
					break;
				case GUI_command::SHOW_B:
					if (cfg.dual_mode) { m_dual_display = DualDisplayMode::B; ShowLastCreatedPictureDual(); }
					break;
				case GUI_command::SHOW_MIX:
					if (cfg.dual_mode) { m_dual_display = DualDisplayMode::MIX; ShowLastCreatedPictureDual(); }
					break;
				}
			}
		}

		// Reacquire lock before waiting on condition/flags
		if (!lock.owns_lock()) lock.lock();

		auto now = std::chrono::steady_clock::now();
		auto deadline = now + std::chrono::nanoseconds( 250000000 );

		if ( std::cv_status::timeout == m_eval_gstate.m_condvar_update.wait_until( lock, deadline ) )
			continue;

		if (m_eval_gstate.m_update_initialized)
		{
			m_eval_gstate.m_update_initialized = false;
			// Start remaining workers without holding the lock
			lock.unlock();
			for (size_t i = 1; i < m_evaluators.size(); ++i)
				m_evaluators[i].Start();
			lock.lock();

			eval_inited = true;
			pending_update = true;
		}

		if (m_eval_gstate.m_update_improvement)
		{
			m_eval_gstate.m_update_improvement = false;

			pending_update = true;
		}

		if (cfg.save_period == -1) // auto
		{
			using namespace std::literals::chrono_literals;
			if ( now - m_previous_save_time > 30s )
			{
				m_previous_save_time = now;
				SaveBestSolution();
			}
		}
		else if (m_eval_gstate.m_update_autosave)
		{
			m_eval_gstate.m_update_autosave = false;
			SaveBestSolution();
		}

		if (m_eval_gstate.m_finished)
		{
			if (m_eval_gstate.m_best_result == 0)
			{
				Message("FINISHED: distance=0");
			}
			break;
		}
	}

	m_eval_gstate.m_finished = true;

	while(m_eval_gstate.m_threads_active > 0)
	{
		m_eval_gstate.m_condvar_update.wait( lock );
	}
}

void RastaConverter::ShowLastCreatedPicture()
{
	int x,y;
	// Draw new picture on the screen
	for (y=0;y<m_height;++y)
	{
		for (x=0;x<m_width;++x)
		{
			rgb atari_color=atari_palette[m_eval_gstate.m_created_picture[y][x]];
			RGBQUAD color=RGB2PIXEL(atari_color);
			FreeImage_SetPixelColor(output_bitmap, x, y, &color);
		}
	}

	int w = FreeImage_GetWidth(output_bitmap);
	gui.DisplayBitmap(w, 0, output_bitmap);
}

void RastaConverter::SavePMG(string name)
{
	size_t sprite,y,bit;
	unsigned char b;
	Message("Saving sprites (PMG)");

	FILE *fp=fopen(name.c_str(),"wt+");
	if (!fp)
		Error("Error saving PMG handler");

	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; ---------------------------------- \n");

	fprintf(fp,"missiles\n");

	fprintf(fp,"\t.ds $100\n");


	for(sprite=0;sprite<4;++sprite)
	{
		fprintf(fp,"player%d\n",(int)sprite);
		fprintf(fp,"\t.he 00 00 00 00 00 00 00 00");
		for (y=0;y<240;++y)
		{
			b=0;
			for (bit=0;bit<8;++bit)
			{
				if (y > (size_t)m_height)
					m_eval_gstate.m_sprites_memory[y][sprite][bit]=0;

				b|=(m_eval_gstate.m_sprites_memory[y][sprite][bit])<<(7-bit);
			}
			fprintf(fp," %02X",b);
			if (y%16==7)
				fprintf(fp,"\n\t.he");
		}
		fprintf(fp," 00 00 00 00 00 00 00 00\n");
	}
	fclose(fp);
}

bool RastaConverter::GetInstructionFromString(const string& line, SRasterInstruction &instr)
{
	static const char *load_names[3]=
	{
		"lda",
		"ldx",
		"ldy",
	};
	static const char *store_names[3]=
	{
		"sta",
		"stx",
		"sty",
	};

	size_t pos_comment, pos_instr, pos_value, pos_target;

	if (line.find(":")!=string::npos)
		return false;

	pos_comment=line.find(";");
	if (pos_comment==string::npos)
		pos_comment=INT_MAX;

	pos_value=line.find("$");

	size_t i,j;

	instr.loose.instruction=E_RASTER_MAX;

	if (line.find("nop") != string::npos)
	{
		instr.loose.instruction = E_RASTER_NOP;
		instr.loose.value = 0;
		instr.loose.target = E_COLBAK;
		return true;
	}

	// check load instructions
	for (i=0;i<3;++i)
	{
		pos_instr=line.find(load_names[i]);
		if (pos_instr!=string::npos)
		{
			if (pos_instr<pos_comment)
			{
				instr.loose.instruction= (e_raster_instruction) (E_RASTER_LDA+i);
				pos_value=line.find("$");
				if (pos_value==string::npos)
					gui.Error("Load instruction: No value for Load Register");
				++pos_value;
				string val_string=line.substr(pos_value,2);
				instr.loose.value=String2HexValue<int>(val_string);
				instr.loose.target = E_TARGET_MAX;
				return true;
			}
		}
	}
	// check store instructions
	for (i=0;i<3;++i)
	{
		pos_instr=line.find(store_names[i]);
		if (pos_instr!=string::npos)
		{
			if (pos_instr<pos_comment)
			{
				instr.loose.instruction=(e_raster_instruction) (E_RASTER_STA+i);
				// find target
				for (j=0;j<=E_TARGET_MAX;++j)
				{
					pos_target=line.find(mem_regs_names[j]);
					if (pos_target!=string::npos)
					{
						instr.loose.target=(e_target) (E_COLOR0+j);
						instr.loose.value = 0;
						return true;
					}
				}
				gui.Error("Load instruction: Unknown target for store");
			}
		}
	}
	return false;
}

void RastaConverter::LoadRegInits(string name)
{
	Message("Loading Reg Inits");

	fstream f;
	f.open( name.c_str(), ios::in);
	if ( f.fail())
		Error("Error loading reg inits");

	string line;
	SRasterInstruction instr;

	uint8_t a = 0;
	uint8_t x = 0;
	uint8_t y = 0;

	while( getline( f, line)) 
	{
		instr.loose.target=E_TARGET_MAX;
		if (GetInstructionFromString(line,instr))
		{
			switch(instr.loose.instruction)
			{
				case E_RASTER_LDA:
					a = instr.loose.value;
					break;
				case E_RASTER_LDX:
					x = instr.loose.value;
					break;
				case E_RASTER_LDY:
					y = instr.loose.value;
					break;
				case E_RASTER_STA:
					if (instr.loose.target != E_TARGET_MAX)
						m_eval_gstate.m_best_pic.mem_regs_init[instr.loose.target] = a;
					break;
				case E_RASTER_STX:
					if (instr.loose.target != E_TARGET_MAX)
						m_eval_gstate.m_best_pic.mem_regs_init[instr.loose.target] = x;
					break;
				case E_RASTER_STY:
					if (instr.loose.target != E_TARGET_MAX)
						m_eval_gstate.m_best_pic.mem_regs_init[instr.loose.target] = y;
					break;
			}
		}
	}

}

void RastaConverter::LoadRasterProgram(string name)
{
	Message("Loading Raster Program");

	fstream f;
	f.open( name.c_str(), ios::in);
	if ( f.fail())
		Error("Error loading Raster Program");

	string line;

	SRasterInstruction instr;
	raster_line current_raster_line;
	current_raster_line.cycles=0;
	size_t pos;
	bool line_started = false;
	
	while( getline( f, line)) 
	{
		// skip filler
		if (line.find("; filler")!=string::npos)
			continue;

		// get info about the file
		pos=line.find("; Evaluations:");
		if (pos!=string::npos)
			m_eval_gstate.m_evaluations=String2Value<unsigned long long>(line.substr(pos+15));

		pos=line.find("; InputName:");
		if (pos!=string::npos)
			cfg.input_file=(line.substr(pos+13));

		pos=line.find("; CmdLine:");
		if (pos!=string::npos)
			cfg.command_line=(line.substr(pos+11));

		if (line.compare(0, 4, "line", 4) == 0)
		{
			line_started = true;
			continue;
		}

		if (!line_started)
			continue;

		// if next raster line
		if (line.find("cmp byt2")!=string::npos && current_raster_line.cycles>0)
		{
			current_raster_line.rehash();
			m_eval_gstate.m_best_pic.raster_lines.push_back(current_raster_line);
			current_raster_line.cycles=0;
			current_raster_line.instructions.clear();
			line_started = false;
			continue;
		}

		// add instruction to raster program if proper instruction
		if (GetInstructionFromString(line,instr))
		{
			current_raster_line.cycles+=GetInstructionCycles(instr);
			current_raster_line.instructions.push_back(instr);
		}
	}
}

bool RastaConverter::LoadRasterProgramInto(raster_picture& dst, const std::string& rp_path, const std::string& ini_path)
{
	// Reset target
	dst = raster_picture();
	dst.raster_lines.clear();
	// Temporarily load into global best, then copy to dst; reuse existing parsing logic
	LoadRegInits(ini_path);
	LoadRasterProgram(rp_path);
	dst = m_eval_gstate.m_best_pic;
	return !dst.raster_lines.empty();
}

bool RastaConverter::Resume()
{
	// Derive base from cfg.output_file and directory
	std::string base = cfg.output_file.empty() ? std::string("output.png") : cfg.output_file;
	// Split dir and filename
	std::string dir;
	{
		size_t pos = base.find_last_of("/\\");
		if (pos != std::string::npos) { dir = base.substr(0, pos + 1); }
	}
	// Expected single-frame paths
	std::string sf_rp = base + ".rp";
	std::string sf_ini = base + ".rp.ini";
	std::string sf_opt = base + ".optstate";
	// Expected dual-frame paths
	std::string df_a_rp = dir + "out_dual_A.rp";
	std::string df_a_ini = dir + "out_dual_A.rp.ini";
	std::string df_b_rp = dir + "out_dual_B.rp";
	std::string df_b_ini = dir + "out_dual_B.rp.ini";

	// Prefer dual if both A and B exist; fallback to single-frame
	auto file_exists = [](const std::string& path) -> bool {
		FILE* f = fopen(path.c_str(), "rb"); if (f) { fclose(f); return true; } return false;
	};

	bool has_dual = file_exists(df_a_rp) && file_exists(df_b_rp);
	bool has_single = file_exists(sf_rp);

	if (has_dual)
	{
		// Enable dual mode if not already
		cfg.dual_mode = true;
		// Load into locals then assign to avoid clobbering A with B during parse
		raster_picture picA;
		raster_picture picB;
		if (!LoadRasterProgramInto(picA, df_a_rp, df_a_ini))
			Error(std::string("Error loading dual resume A: ") + df_a_rp);
		if (!LoadRasterProgramInto(picB, df_b_rp, df_b_ini))
			Error(std::string("Error loading dual resume B: ") + df_b_rp);
		m_eval_gstate.m_best_pic = picA;
		m_best_pic_B = picB;
	}
	else if (has_single)
	{
		// Single-frame resume
		LoadRegInits(sf_ini);
		LoadRasterProgram(sf_rp);
	}
	else
	{
		Error("/continue: no saved program found for resume (looked for single and dual outputs)");
	}

	// Load optimizer state if present
	if (file_exists(sf_opt)) {
		LoadOptimizerState(sf_opt);
	}

	// Re-parse saved command line to restore other options, but keep current CLI /output if set
	std::string cli_out = cfg.output_file;
	bool keep_cli_out = !cli_out.empty();
	cfg.ProcessCmdLine();
	if (keep_cli_out) cfg.output_file = cli_out;
	return true;
}

void RastaConverter::SaveRasterProgram(string name, raster_picture *pic)
{
	Message("Saving Raster Program");

	FILE *fp=fopen(string(name+".ini").c_str(),"wt+");
	if (!fp)
		Error("Error saving Raster Program");

	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; ---------------------------------- \n");

	fprintf(fp,"\n; Initial values \n");

	for(size_t y=0;y<sizeof(pic->mem_regs_init);++y)
	{
		fprintf(fp,"\tlda ");
		fprintf(fp,"#$%02X\n",pic->mem_regs_init[y]);		
		fprintf(fp,"\tsta ");
		fprintf(fp,"%s\n",mem_regs_names[y]);		
	}

	// zero registers
	fprintf(fp,"\tlda #$0\n");
	fprintf(fp,"\ttax\n");
	fprintf(fp,"\ttay\n");

	fprintf(fp,"\n; Set proper count of wsyncs \n");
	fprintf(fp,"\n\t:2 sta wsync\n");

	fclose(fp);

	// Save picture height into a separate header file
	FILE *fh=fopen(string(name+".h").c_str(),"wt+");
	if (!fh)
		Error("Error saving picture height header file");
	fprintf(fh,"; Set proper picture height\n");
	fprintf(fh,"PIC_HEIGHT = %d\n",m_height);
	fclose(fh);

	fp=fopen(name.c_str(),"wt+");
	if (!fp)
		Error("Error saving DLI handler");

	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; InputName: %s\n",cfg.input_file.c_str());
	fprintf(fp,"; CmdLine: %s\n",cfg.command_line.c_str());
	fprintf(fp,"; Evaluations: %llu\n", (unsigned long long)m_eval_gstate.m_evaluations);
	fprintf(fp,"; Score: %g\n",NormalizeScore(m_eval_gstate.m_best_result));
	fprintf(fp,"; ---------------------------------- \n");

	fprintf(fp,"; Proper offset \n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tcmp byt2;\n");

	int h = FreeImage_GetHeight(input_bitmap);

	for(int y=0;y<h;++y)
	{
		fprintf(fp,"line%d\n",y);
		size_t prog_len=pic->raster_lines[y].instructions.size();
		for (size_t i=0;i<prog_len;++i)
		{
			SRasterInstruction instr=pic->raster_lines[y].instructions[i];
			bool save_target=false;
			bool save_value=false;
			fprintf(fp,"\t");
			switch (instr.loose.instruction)
			{
			case E_RASTER_LDA:
				fprintf(fp,"lda ");
				save_value=true;
				break;
			case E_RASTER_LDX:
				fprintf(fp,"ldx ");
				save_value=true;
				break;
			case E_RASTER_LDY:
				fprintf(fp,"ldy ");
				save_value=true;
				break;
			case E_RASTER_NOP:
				fprintf(fp,"nop ");
				break;
			case E_RASTER_STA:
				fprintf(fp,"sta ");
				save_target=true;
				break;
			case E_RASTER_STX:
				fprintf(fp,"stx ");
				save_target=true;
				break;
			case E_RASTER_STY:
				fprintf(fp,"sty ");
				save_target=true;
				break;
			default:
				Error("Unknown instruction!");
			}
			if (save_value)
			{
				fprintf(fp,"#$%02X ; %d (spr=%d)",instr.loose.value,instr.loose.value,instr.loose.value-48);
			}
			else if (save_target)
			{
				if (instr.loose.target>E_TARGET_MAX)
					Error("Unknown target in instruction!");
				fprintf(fp,"%s",mem_regs_names[instr.loose.target]);
			}
			fprintf(fp,"\n");			
		}
		for (int cycle=pic->raster_lines[y].cycles;cycle<free_cycles;cycle+=2)
		{
			fprintf(fp,"\tnop ; filler\n");
		}
		fprintf(fp,"\tcmp byt2; on zero page so 3 cycles\n");
	}
	fprintf(fp,"; ---------------------------------- \n");
	fclose(fp);
}

double RastaConverter::NormalizeScore(double raw_score)
{
	return raw_score / (((double)m_width*(double)m_height)*(MAX_COLOR_DISTANCE/10000));
}



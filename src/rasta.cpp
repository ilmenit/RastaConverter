// - wznawianie
// - warunek stopu
// - maska dokladnosci + scale
// - nazwy plików dla generatora z pliku CFG
// - inaczej dithering /dither(ing)=
// - obszary gdzie poszczególne obiekty w³¹czome lub wy³¹czone

const char *program_version="Beta3";

#pragma warning (disable: 4312)
#pragma warning (disable: 4996)

#include <math.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <string>
#include "FreeImage.h"
#include <allegro.h> 
#include "CommandLineParser.h"
#include "string_conv.h"
#include <assert.h>
#include "config.h"
#include <float.h>
#include <iostream>
#include <fstream>
#include <strstream>
#include <sstream>
#include <ctype.h>
#include <iomanip>
#include <iterator>
#include <unordered_map>

#include "rasta.h"
#include "main.h"

// Cycle where WSYNC starts - 105?
#define WSYNC_START 104
// Normal screen CPU cycle 24-104 = 80 cycles = 160 color cycles

// global variables
int solutions=1;
const int sprite_screen_color_cycle_start=48;
const int sprite_size=32;

extern bool operator<(const rgb &l, const rgb &r);

void quit_function(void)
{
	allegro_exit();
	exit(0);
}

void error(char *e)
{
	allegro_message(e);
	allegro_exit();
	exit(1);
}

void error(char *e, int i)
{
	allegro_message("%s %d",e,i);
	allegro_exit();
	exit(1);
}

int random(int range)
{
	if (range==0)
		return 0;
	return rand()%range;
}

void ShowHelp()
{
	error("RastaConverter by Jakub Debski '2012\n\nRastaConverter.exe InputFile [options]\n\nRead help.txt for help");
}

extern int screen_color_depth;
extern int desktop_width;
extern int desktop_height;

bool user_closed_app=false;

f_rgb_distance distance_function;

void Message(char *message)
{
	rectfill(screen,0,440,640,460,0);
	textprintf_ex(screen, font, 0, 440, makecol(0xF0,0xF0,0xF0), 0, "%s", message);
}

void Message(char *message, int i)
{
	rectfill(screen,0,440,640,460,0);
	textprintf_ex(screen, font, 0, 440, makecol(0xF0,0xF0,0xF0), 0, "%s %d", message,i);
}


using namespace std;
using namespace Epoch::Foundation;

#define PIXEL2RGB(p) (*((rgb*) &p))
#define RGB2PIXEL(p) (*((int*) &p))

unsigned char reg_a, reg_x, reg_y;

unsigned char mem_regs[E_TARGET_MAX];
unsigned char sprite_shift_regs[4];
unsigned char sprite_shift_start_array[256];

char *mem_regs_names[E_TARGET_MAX+1]=
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

int free_cycles=0; // must be set depending on the mode, PMG, LMS etc.
ScreenCycle screen_cycles[CYCLES_MAX];

// we limit PMG memory to visible 240 bytes
unsigned char sprites_memory[240][4][8]; // we convert it to 240 bytes of PMG memory at the end of processing.

rgb atari_palette[128]; // 128 colors in mode 15!


void create_cycles_table()
{
	char antic_cycles[CYCLE_MAP_SIZE]="IPPPPAA             G G GRG GRG GRG GRG GRG GRG GRG GRG GRG G G G G G G G G G G G G G G G G G G G G              M";
	int antic_xpos, last_antic_xpos=0;
	int cpu_xpos = 0;
	for (antic_xpos = 0; antic_xpos < CYCLE_MAP_SIZE; antic_xpos++) 
	{
		char c = antic_cycles[antic_xpos];
		// we have set normal width, graphics mode, PMG and LMS in each line
		if (c != 'G' && c != 'R' && c != 'P' && c != 'M' && c != 'I' && c != 'A') 
		{
			/*Not a stolen cycle*/
			assert(cpu_xpos<CYCLES_MAX);
			screen_cycles[cpu_xpos].offset=(antic_xpos-24)*2;
			if (cpu_xpos>0)
			{
				screen_cycles[cpu_xpos-1].length=(antic_xpos-last_antic_xpos)*2;
			}
			last_antic_xpos=antic_xpos;
			cpu_xpos++;
		}
	}
	free_cycles=cpu_xpos-1;
	if (free_cycles%2 != 0)
		--free_cycles;

	free_cycles=53; // !!! by experience

	screen_cycles[cpu_xpos-1].length=(antic_xpos-24)*2;
	return;
}

bool LoadAtariPalette(string filename)
{
	Message("Loading palette");
	size_t i;
	rgb col;
	col.a=0;
	FILE *fp=fopen(filename.c_str(),"rb");
	if (!fp)
	{
		fp=fopen((string("palettes\\")+filename).c_str(),"rb");
		if (!fp)
		{
			fp=fopen((string("palettes\\")+filename+string(".act")).c_str(),"rb");
			if (!fp)
			{
				fp=fopen((string("palettes\\")+filename+string(".pal")).c_str(),"rb");
				if (!fp)
					error("Error opening .act palette file");
			}
		}
	}
	for (i=0;i<256;++i)
	{
		col.r=fgetc(fp);
		col.g=fgetc(fp);
		col.b=fgetc(fp);
		// limit it to 128 colors!
		// use every second color
		if (i%2==0)
			atari_palette[i >> 1] = col;
	}
	fclose(fp);
	return true;
}

char *mutation_names[E_MUTATION_MAX]=
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
};

inline void RGBtoYUV(double r, double g, double b, double &y, double &u, double &v)
{
	y = 0.299*r + 0.587*g + 0.114*b;
	u= (b-y)*0.565;
	v= (r-y)*0.713;
}

#define MAX_COLOR_DISTANCE (255*255*3)

distance_t RGByuvDistance(const rgb &col1, const rgb &col2)
{
	int dr = col2.r - col1.r;
	int dg = col2.g - col1.g;
	int db = col2.b - col1.b;

	float dy = 0.299f*dr + 0.587f*dg + 0.114f*db;
	float du = (db-dy)*0.565f;
	float dv = (dr-dy)*0.713f;

	float d = dy*dy + du*du + dv*dv;

	if (d > (float)DISTANCE_MAX)
		d = (float)DISTANCE_MAX;

	return (distance_t)d;

//	int dy = 38*dr + 75*dg + 14*db;
//	int du = (db - ((dy + 64) >> 7)) * 72;
//	int dv = (dr - ((dy + 64) >> 7)) * 91;

//	return (dy*dy + du*du + dv*dv) >> 14;
}


distance_t RGBEuclidianDistance(const rgb &col1, const rgb &col2)
{
	int distance=0;

	// euclidian distance
	int dr = col1.r - col2.r;
	int dg = col1.g - col2.g;
	int db = col1.b - col2.b;

	int d = dr*dr + dg*dg + db*db;

	if (d > DISTANCE_MAX)
		d = DISTANCE_MAX;

	return (distance_t)d;
}

void resize_rgb_picture(vector < screen_line > *picture, size_t width, size_t height)
{
	size_t y;
	picture->resize(height);
	for (y=0;y<height;++y)
	{
		(*picture)[y].Resize(width);
	}
}

void resize_target_picture(vector < line_target > *picture, size_t width, size_t height)
{
	size_t y;
	picture->resize(height);
	for (y=0;y<height;++y)
	{
		(*picture)[y].Resize(width);
	}
}

unsigned char FindAtariColorIndex(rgb &col);
bool LoadAtariPalette(string filename);

unsigned char FindAtariColorIndex(rgb &col)
{
	unsigned char i;
	// Find the most similar color in the Atari Palette
	unsigned char most_similar=0;
	double distance;
	double min_distance=DBL_MAX;
	for(i=0;i<128;++i)
	{
		distance = distance_function(col,atari_palette[i]);
		if (distance < min_distance)
		{
			min_distance=distance;
			most_similar=i;
		}
	}
	return most_similar;
}

bool RastaConverter::SavePicture(string filename, BITMAP *to_save)
{
	Message("Saving picture");

	BITMAP *expected_bitmap = create_bitmap_ex(screen_color_depth,cfg.width*2,cfg.height);

	stretch_blit(to_save,expected_bitmap,0,0,to_save->w,to_save->h,0,0,expected_bitmap->w,expected_bitmap->h);
	// copy expected bitmap to FreeImage bitmap to save it as png
	FIBITMAP *f_outbitmap = FreeImage_Allocate(expected_bitmap->w,expected_bitmap->h, 24);	

	int x,y;
	int color;
	for (y=0;y<expected_bitmap->h;++y)
	{
		for (x=0;x<expected_bitmap->w;++x)
		{
			color = getpixel( expected_bitmap,x,y);
			FreeImage_SetPixelColor(f_outbitmap, x, y, (RGBQUAD *)&color);
		}
	}

	FreeImage_FlipVertical(f_outbitmap);
	FreeImage_Save(FIF_PNG,f_outbitmap,filename.c_str());
	FreeImage_Unload(f_outbitmap);
	return true;
}

bool RastaConverter::LoadInputBitmap()
{
	Message("Loading and initializing file");
	fbitmap = FreeImage_Load(FreeImage_GetFileType(cfg.input_file.c_str()), cfg.input_file.c_str(), 0);
	if (!fbitmap)
		error("Error loading input file");
	fbitmap=FreeImage_Rescale(fbitmap,cfg.width,cfg.height,cfg.rescale_filter);

	if (screen_color_depth==32)
		fbitmap=FreeImage_ConvertTo32Bits(fbitmap);
	else
		fbitmap=FreeImage_ConvertTo24Bits(fbitmap);

	FreeImage_FlipVertical(fbitmap);

	set_palette(palette);
	input_bitmap  = create_bitmap_ex(screen_color_depth,cfg.width,cfg.height);
	output_bitmap  = create_bitmap_ex(screen_color_depth,cfg.width,cfg.height);
	destination_bitmap  = create_bitmap_ex(screen_color_depth,cfg.width,cfg.height);

	m_height=(int) cfg.height;
	m_width=(int) cfg.width;

	return true;
}

void RastaConverter::InitLocalStructure()
{
	int x,y;
	//////////////////////////////////////////////////////////////////////////
	// Set color distance

	if (cfg.euclid)
		distance_function = RGBEuclidianDistance;
	else
		distance_function = RGByuvDistance;

	//////////////////////////////////////////////////////////////////////////
	// Set our structure size

	resize_rgb_picture(&m_picture,input_bitmap->w,input_bitmap->h);

	// Copy data to input_bitmap and to our structure
	RGBQUAD fpixel;
	rgb atari_color;
	for (y=0;y<input_bitmap->h;++y)
	{
		for (x=0;x<input_bitmap->w;++x)
		{
			FreeImage_GetPixelColor(fbitmap, x, y, &fpixel);
			putpixel( input_bitmap,x,y,makecol(fpixel.rgbRed,fpixel.rgbGreen,fpixel.rgbBlue));
			atari_color=PIXEL2RGB(fpixel);
			m_picture[y][x]=atari_color;
			fpixel.rgbRed=atari_color.r;
			fpixel.rgbGreen=atari_color.g;
			fpixel.rgbBlue=atari_color.b;
			FreeImage_SetPixelColor(fbitmap, x, y, &fpixel);
		}
	}

	for(int i=0; i<128; ++i)
	{
		m_picture_all_errors[i].resize(input_bitmap->w * input_bitmap->h);

		const rgb ref = atari_palette[i];

		auto *dst = &m_picture_all_errors[i][0];
		for (y=0;y<input_bitmap->h;++y)
		{
			auto& dstrow = m_picture_all_errors[i][y];
			const auto& srcrow = m_picture[y];

			for (x=0;x<input_bitmap->w;++x)
			{
				*dst++ = distance_function(srcrow[x], ref);
			}
		}
	}

	clear_bitmap(screen);
	// Show our picture
	if (desktop_width>=320*3)
	{
		stretch_blit(input_bitmap,screen,0,0,input_bitmap->w,input_bitmap->h,0,0,input_bitmap->w*2,input_bitmap->h);
		textprintf_ex(screen, font, 0, input_bitmap->h+10, makecol(0x80,0x80,0x80), 0, "Source");
		textprintf_ex(screen, font, input_bitmap->w*2, input_bitmap->h+10, makecol(0x80,0x80,0x80), 0, "Current output");
		textprintf_ex(screen, font, input_bitmap->w*4, input_bitmap->h+10, makecol(0x80,0x80,0x80), 0, "Destination");
	}
	else
	{
		blit(input_bitmap,screen,0,0,0,0,input_bitmap->w,input_bitmap->h);
		textprintf_ex(screen, font, 0, input_bitmap->h+10, makecol(0x80,0x80,0x80), 0, "Source");
		textprintf_ex(screen, font, input_bitmap->w, input_bitmap->h+10, makecol(0x80,0x80,0x80), 0, "Current output");
		textprintf_ex(screen, font, input_bitmap->w*2, input_bitmap->h+10, makecol(0x80,0x80,0x80), 0, "Destination");
	}

};

void RastaConverter::PrepareDestinationPicture()
{
	int x,y;

	if (cfg.dither!=E_DITHER_NONE)
		ClearErrorMap();

	// Draw new picture on the screen
	const int w = input_bitmap->w;
	const int w1 = w - 1;

	for (y=0;y<input_bitmap->h;++y)
	{
		const bool flip = (cfg.dither==E_DITHER_FLOYD) && (y & 1);

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
					/* Standard Floyd–Steinberg uses 4 pixels to diffuse */

					if (flip)
					{
						DiffuseError( x-1, y,   7.0/16.0, qe.r,qe.g,qe.b);
						DiffuseError( x+1, y+1, 3.0/16.0, qe.r,qe.g,qe.b);
						DiffuseError( x  , y+1, 5.0/16.0, qe.r,qe.g,qe.b);
						DiffuseError( x-1, y+1, 1.0/16.0, qe.r,qe.g,qe.b);
					}
					else
					{
						DiffuseError( x+1, y,   7.0/16.0, qe.r,qe.g,qe.b);
						DiffuseError( x-1, y+1, 3.0/16.0, qe.r,qe.g,qe.b);
						DiffuseError( x  , y+1, 5.0/16.0, qe.r,qe.g,qe.b);
						DiffuseError( x+1, y+1, 1.0/16.0, qe.r,qe.g,qe.b);
					}
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
					DiffuseError( x+1, y+1, 3.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x-1, y+2, 1.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x-2, y+2, 3.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x  , y+2, 5.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x+1, y+2, 3.0/48.0, qe.r,qe.g,qe.b);
					DiffuseError( x+1, y+2, 1.0/48.0, qe.r,qe.g,qe.b);
				}
			}
			out_pixel = atari_palette[FindAtariColorIndex(out_pixel)];
			int color=RGB2PIXEL(out_pixel);
			putpixel(destination_bitmap,x,y,color);
		}
	}

	if (desktop_width>=320*3)
		stretch_blit(destination_bitmap,screen,0,0,destination_bitmap->w,destination_bitmap->h,input_bitmap->w*4,0,destination_bitmap->w*2,destination_bitmap->h);
	else
		blit(destination_bitmap,screen,0,0,input_bitmap->w*2,0,destination_bitmap->w,destination_bitmap->h);

	if (cfg.dither)
	{
		for (y=0;y<input_bitmap->h;++y)
		{
			for (x=0;x<input_bitmap->w;++x)
			{
				int color = getpixel(destination_bitmap,x,y);
				rgb out_pixel=PIXEL2RGB(color);
				m_picture[y][x]=out_pixel;
			}
		}
	}
}

bool RastaConverter::ProcessInit()
{
	InitLocalStructure();
	SavePicture(cfg.output_file+"-src.png",input_bitmap);

	// Apply dithering or other picture transformation
	PrepareDestinationPicture();
	SavePicture(cfg.output_file+"-dst.png",destination_bitmap);
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
	}
	assert(0); // this should never happen
	return -1;
}

bool RastaConverter::SaveScreenData(const char *filename)
{
	int x,y,a=0,b=0,c=0,d=0;
	FILE *fp=fopen(filename,"wb+");
	if (!fp)
		error("Error saving MIC screen data");

	Message("Saving screen data");
	for(y=0;y<m_height;++y)
	{
		// encode 4 pixel colors in byte

		for (x=0;x<m_width;x+=4)
		{
			unsigned char pix=0;
			a=ConvertColorRegisterToRawData((e_target)m_created_picture_targets[y][x]);
			b=ConvertColorRegisterToRawData((e_target)m_created_picture_targets[y][x+1]);
			c=ConvertColorRegisterToRawData((e_target)m_created_picture_targets[y][x+2]);
			d=ConvertColorRegisterToRawData((e_target)m_created_picture_targets[y][x+3]);
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

void RastaConverter::CreateEmptyRasterPicture(raster_picture *r)
{
	memset(r->mem_regs_init,0,sizeof(r->mem_regs_init));
	SRasterInstruction i;
	i.instruction=E_RASTER_NOP;
	i.target=E_COLBAK;
	i.value=0;
	int size = FreeImage_GetWidth(fbitmap);
	// in line 0 we set init registers
	for (size_t y=0;y<r->raster_lines.size();++y)
	{
		r->raster_lines[y].instructions.push_back(i);
		r->raster_lines[y].cycles+=2;
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

	dest_regs=8;
	if (cfg.init_type==E_INIT_LESS)
		dest_colors=dest_regs;
	else
		dest_colors=dest_regs+4;

	FreeImage_FlipVertical(fbitmap);

	int size = FreeImage_GetWidth(fbitmap);
	// in line 0 we set init registers
	for (y=0;y<(int)r->raster_lines.size();++y)
	{
		// create new picture from line y 
		FIBITMAP *f_copy = FreeImage_Copy(fbitmap,0,y+1,size,0);	
		int size2 = FreeImage_GetHeight(f_copy);
		FIBITMAP *f_copy24bits = FreeImage_ConvertTo24Bits(f_copy);	

		// quantize it 
		FIBITMAP *f_quant = FreeImage_ColorQuantizeEx(f_copy24bits,FIQ_WUQUANT,dest_colors);
		FIBITMAP *f_copy24bits2 = FreeImage_ConvertTo24Bits(f_quant);

		map <int,int > color_map;
		map <int,int >::iterator j,_j;
		multimap <int,int, greater <int> > sorted_colors;
		multimap <int,int, greater <int> >::iterator m;
		map <int,int> color_position;
		for (x=0;x<size;++x)
		{
			RGBQUAD fpixel;
			FreeImage_GetPixelColor(f_copy24bits2, x,0, &fpixel);
			int c=makecol(fpixel.rgbRed,fpixel.rgbGreen,fpixel.rgbBlue);
			//			putpixel( screen,x,y,c);
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
			color.r=getr(c);
			color.g=getg(c);
			color.b=getb(c);

			// lda
			i.instruction=(e_raster_instruction) (E_RASTER_LDA+k%3); // k%3 to cycle through A,X,Y regs
			if (k>E_COLBAK && y%2==1)
				i.value=(e_target) color_position[k]+sprite_screen_color_cycle_start; // sprite position
			else
				i.value=FindAtariColorIndex(color)*2;
			i.target=E_COLOR0;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=2;

			// sta 
			i.instruction=(e_raster_instruction) (E_RASTER_STA+k%3); // k%3 to cycle through A,X,Y regs
			i.value=(random(128)*2);

			if (k>E_COLBAK && y%2==1)
				i.target=(e_target) (k+4); // position
			else
				i.target=(e_target) k;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=4;	

			assert(r->raster_lines[y].cycles<free_cycles);
		}

		FreeImage_Unload(f_copy);
		FreeImage_Unload(f_copy24bits);
		FreeImage_Unload(f_quant);
		FreeImage_Unload(f_copy24bits2);
	}
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
		i.instruction=E_RASTER_LDA;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.target=E_COLOR0;
		r->raster_lines[y].instructions.push_back(i);
		// sta 
		i.instruction=E_RASTER_STA;
		r->raster_lines[y].cycles+=4;
		i.value=(random(128)*2);
		i.target=E_COLOR0;
		r->raster_lines[y].instructions.push_back(i);

		// ldx random
		i.instruction=E_RASTER_LDX;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.target=E_COLOR1;
		r->raster_lines[y].instructions.push_back(i);
		// stx 
		i.instruction=E_RASTER_STX;
		r->raster_lines[y].cycles+=4;
		i.value=(random(128)*2);
		i.target=E_COLOR1;
		r->raster_lines[y].instructions.push_back(i);

		// ldy random
		i.instruction=E_RASTER_LDY;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.target=E_COLOR2;
		r->raster_lines[y].instructions.push_back(i);
		// sty 
		i.instruction=E_RASTER_STY;
		r->raster_lines[y].cycles+=4;
		i.value=(random(128)*2);
		i.target=E_COLOR2;
		r->raster_lines[y].instructions.push_back(i);

		// lda random
		i.instruction=E_RASTER_LDA;
		r->raster_lines[y].cycles+=2;
		x=random(m_width);
		i.value=FindAtariColorIndex(m_picture[y][x])*2;
		i.target=E_COLBAK;
		r->raster_lines[y].instructions.push_back(i);
		// sty 
		i.instruction=E_RASTER_STA;
		r->raster_lines[y].cycles+=4;
		i.value=(random(128)*2);
		i.target=E_COLBAK;
		r->raster_lines[y].instructions.push_back(i);

		assert(r->raster_lines[y].cycles<free_cycles);
	}
}


inline int RastaConverter::GetInstructionCycles(const SRasterInstruction &instr)
{
	switch(instr.instruction)
	{
	case E_RASTER_NOP:
	case E_RASTER_LDA:
	case E_RASTER_LDX:
	case E_RASTER_LDY:
		return 2;
	}
	return 4;
}

void RastaConverter::DiffuseError( int x, int y, double quant_error, double e_r,double e_g,double e_b)
{
	if (! (x>=0 && x<m_width && y>=0 && y<m_height) )
		return;

	rgb_error p = error_map[y][x];
	p.r += e_r * quant_error;
	p.g += e_g * quant_error;
	p.b += e_b * quant_error;
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

template<fn_rgb_distance& T_distance_function>
e_target RastaConverter::FindClosestColorRegister(int index, int x,int y, bool &restart_line)
{
	distance_t distance;
	int sprite_bit;
	int best_sprite_bit;
	e_target result=E_COLBAK;
	distance_t min_distance = DISTANCE_MAX;
	bool sprite_covers_colbak=false;

	// check sprites

	// Sprites priority is 0,1,2,3

	for (int temp=E_COLPM0;temp<=E_COLPM3;++temp)
	{
		int sprite_pos=sprite_shift_regs[temp-E_COLPM0];

		int sprite_x=sprite_pos-sprite_screen_color_cycle_start;
//		if (x>=sprite_x && x<sprite_x+sprite_size)

		unsigned x_offset = (unsigned)(x - sprite_x);
		if (x_offset < sprite_size)
		{
//			sprite_bit=(x-sprite_x)/4; // bit of this sprite memory
			sprite_bit=x_offset >> 2; // bit of this sprite memory
			assert(sprite_bit<8);

			sprite_covers_colbak=true;

//			distance = T_distance_function(pixel,atari_palette[mem_regs[temp]/2]);
			distance = m_picture_all_errors[mem_regs[temp]/2][index];
			if (sprites_memory[y][temp-E_COLPM0][sprite_bit])
			{
				// priority of sprites - next sprites are hidden below that one, so they are not processed
				best_sprite_bit=sprite_bit;
				result=(e_target) temp;
				min_distance = distance;
				break;
			}
			if (distance<min_distance)
			{
				best_sprite_bit=sprite_bit;
				result=(e_target) temp;
				min_distance=distance;
			}
		}
	}

	// check standard colors

	int last_color_register;

	if (sprite_covers_colbak)
		last_color_register=E_COLOR2; // COLBAK is not used
	else
		last_color_register=E_COLBAK;

	for (int temp=E_COLOR0;temp<=last_color_register;++temp)
	{
//		distance = T_distance_function(pixel,atari_palette[mem_regs[temp]/2]);
		distance = m_picture_all_errors[mem_regs[temp]/2][index];
		if (distance<min_distance)
		{
			min_distance=distance;
			result=(e_target) temp;
		}
	}

	// the best color is in sprite, then set the proper bit of the sprite memory and then restart this line
	if (result>=E_COLPM0 && result<=E_COLPM3)
	{
		// if PMG bit has been modified, then restart this line, because previous pixels of COLBAK may be covered
		if (sprites_memory[y][result-E_COLPM0][best_sprite_bit]==false)
		{
			restart_line=true;
			sprites_memory[y][result-E_COLPM0][best_sprite_bit]=true;
		}

	}
	return result;
}


inline void RastaConverter::ExecuteInstruction(const SRasterInstruction &instr, int x)
{
	int reg_value=-1;
	switch(instr.instruction)
	{
	case E_RASTER_LDA:
		reg_a=instr.value;
		break;
	case E_RASTER_LDX:
		reg_x=instr.value;
		break;
	case E_RASTER_LDY:
		reg_y=instr.value;
		break;
	case E_RASTER_STA:
		reg_value=reg_a;
		break;
	case E_RASTER_STX:
		reg_value=reg_x;
		break;
	case E_RASTER_STY:
		reg_value=reg_y;
		break;
	}

	if (reg_value!=-1)
	{
		// make write to sprite0 4 cycle nop in border mode
		if (!cfg.border || (instr.target!=E_HPOSP0 && instr.target!=E_COLPM0))
		{
			const unsigned hpos_index = (unsigned)(instr.target - E_HPOSP0);
			if (hpos_index < 4) {
				sprite_shift_start_array[mem_regs[instr.target]] &= ~(1 << hpos_index);

				mem_regs[instr.target]=reg_value;

				sprite_shift_start_array[mem_regs[instr.target]] |= (1 << hpos_index);
			} else {
				mem_regs[instr.target]=reg_value;
			}
		}
	}
}



void RastaConverter::SetSpriteBorders(raster_picture *pic)
{
	int y;
	SRasterInstruction i;
	for (y=0;y<m_height;++y)
	{
		i.instruction=E_RASTER_NOP;
		i.target=E_COLBAK;
		i.value=0;
		while(pic->raster_lines[y].cycles<free_cycles)
		{
			pic->raster_lines[y].instructions.push_back(i);
			pic->raster_lines[y].cycles+=2;

		}

		// change last instructions to:
		// LDA #0
		// STA COLPM0
		// LDA #208 ; right border
		// STA HPOSP0 ; here we shoule be after the right border
		// LDA #48 ; left border
		// STA HPOSP0
	}
}

void ResetSpriteShiftStartArray()
{
	memset(sprite_shift_start_array, 0, sizeof sprite_shift_start_array);

	for(int i=0; i<4; ++i)
		sprite_shift_start_array[mem_regs[i+E_HPOSP0]] |= (1 << i);
}

struct line_machine_state
{
	unsigned char reg_a;
	unsigned char reg_x;
	unsigned char reg_y;
	unsigned char mem_regs[E_TARGET_MAX];

	void capture()
	{
		this->reg_a = ::reg_a;
		this->reg_x = ::reg_x;
		this->reg_y = ::reg_y;

		memcpy(this->mem_regs, ::mem_regs, sizeof this->mem_regs);
	}

	void apply() const
	{
		::reg_a = this->reg_a;
		::reg_x = this->reg_x;
		::reg_y = this->reg_y;
		memcpy(::mem_regs, this->mem_regs, sizeof ::mem_regs);
	}
};

struct line_cache_key
{
	size_t hash;
	line_machine_state entry_state;
	const SRasterInstruction *insns;
	unsigned insn_count;
};

struct line_cache_key_hash
{
	size_t operator()(const line_cache_key& key) const
	{
		unsigned h = 0;
		
		h += (size_t)key.entry_state.reg_a;
		h += (size_t)key.entry_state.reg_x << 8;
		h += (size_t)key.entry_state.reg_y << 16;
		h += key.insn_count << 24;

		for(int i=0; i<E_TARGET_MAX; ++i)
			h += (size_t)key.entry_state.mem_regs[i] << (8*(i & 3));

		for(unsigned i=0; i<key.insn_count; ++i)
		{
			const SRasterInstruction& insn = key.insns[i];

			h += (size_t)insn.value;
			h += (size_t)insn.target << 8;
			h += (size_t)insn.instruction << 16;

			h = (h >> 27) + (h << 5);
		}

		return h;
	}
};

struct line_cache_key_eq
{
	bool operator()(const line_cache_key& key1, const line_cache_key& key2) const
	{
		if (key1.hash != key2.hash) return false;
		if (key1.entry_state.reg_a != key2.entry_state.reg_a) return false;
		if (key1.entry_state.reg_x != key2.entry_state.reg_x) return false;
		if (key1.entry_state.reg_y != key2.entry_state.reg_y) return false;
		if (memcmp(key1.entry_state.mem_regs, key2.entry_state.mem_regs, sizeof key1.entry_state.mem_regs)) return false;

		if (key1.insn_count != key2.insn_count) return false;

		for(unsigned i=0; i<key1.insn_count; ++i) {
			if (!(key1.insns[i] == key2.insns[i]))
				return false;
		}

		return true;
	}
};

struct line_cache_result
{
	line_machine_state new_state;
	vector<unsigned char> color_row;
	vector<unsigned char> target_row;
	unsigned char sprite_data[4][8];
};

unordered_map<line_cache_key, line_cache_result, line_cache_key_hash, line_cache_key_eq> line_cache;
const size_t LINE_CACHE_INSN_POOL_SIZE = 1048576*4;
SRasterInstruction line_cache_insn_pool[LINE_CACHE_INSN_POOL_SIZE];
size_t line_cache_insn_pool_level = 0;

unsigned char old_reg_a,old_reg_x,old_reg_y;
unsigned char old_mem_regs[E_TARGET_MAX];

void StoreLineRegs()
{
	old_reg_a=reg_a;
	old_reg_x=reg_x;
	old_reg_y=reg_y;
	memcpy(old_mem_regs,mem_regs,sizeof(mem_regs));
}

void RestoreLineRegs()
{
	reg_a=old_reg_a;
	reg_x=old_reg_x;
	reg_y=old_reg_y;
	memcpy(mem_regs,old_mem_regs,sizeof(mem_regs));
}

void RastaConverter::ExecuteRasterProgram(raster_picture *pic)
{
	if (distance_function == RGByuvDistance)
		ExecuteRasterProgramT<RGByuvDistance>(pic);
	else
		ExecuteRasterProgramT<RGBEuclidianDistance>(pic);
}

template<fn_rgb_distance& T_distance_function> 
void RastaConverter::ExecuteRasterProgramT(raster_picture *pic)
{
	int x,y; // currently processed pixel

	int cycle;
	int next_instr_offset;
	int ip; // instruction pointer

	const SRasterInstruction *__restrict instr;

	reg_a=0;
	reg_x=0;
	reg_y=0;
	memset(sprite_shift_regs,0,sizeof(sprite_shift_regs));
	memcpy(mem_regs,pic->mem_regs_init,sizeof(pic->mem_regs_init));
	memset(sprites_memory,0,sizeof(sprites_memory));
	
	bool restart_line=false;
	bool shift_start_array_dirty = true;
	for (y=0;y<m_height;++y)
	{
		if (restart_line)
		{
			RestoreLineRegs();
			shift_start_array_dirty = true;
		}
		else
		{
			StoreLineRegs();
		}

		// snapshot current machine state
		const auto *__restrict rastinsns = pic->raster_lines[y].instructions.data();
		const int rastinsncnt = pic->raster_lines[y].instructions.size();

		line_cache_key lck;
		lck.entry_state.capture();
		lck.insns = rastinsns;
		lck.insn_count = rastinsncnt;
		lck.hash = line_cache_key_hash()(lck);

		// check line cache
		auto * __restrict created_picture_row = &m_created_picture[y][0];
		auto * __restrict created_picture_targets_row = &m_created_picture_targets[y][0];

		auto cache_it = line_cache.find(lck);
		if (cache_it != line_cache.end())
		{
			// sweet! cache hit!!
			const auto& cached_line_result = cache_it->second;

			cached_line_result.new_state.apply();
			memcpy(created_picture_row, cached_line_result.color_row.data(), m_width);
			memcpy(created_picture_targets_row, cached_line_result.target_row.data(), m_width);
			memcpy(sprites_memory[y], cached_line_result.sprite_data, sizeof sprites_memory[y]);
			shift_start_array_dirty = true;
			continue;
		}

		if (shift_start_array_dirty)
		{
			shift_start_array_dirty = false;

			ResetSpriteShiftStartArray();
		}

		restart_line=false;
		ip=0;
		cycle=0;
		next_instr_offset=screen_cycles[cycle].offset;

		// on new line clear sprite shifts and wait to be taken from mem_regs
		memset(sprite_shift_regs,0,sizeof(sprite_shift_regs));

		if (!rastinsncnt)
			next_instr_offset = 1000;

		const auto *__restrict picture_row = &m_picture[y][0];
		const auto *__restrict error_row = &m_picture_all_errors[y][0];

		const int picture_row_index = m_width * y;

		for (x=-sprite_screen_color_cycle_start;x<176;++x)
		{
			// check position of sprites
			const int sprite_check_x = x + sprite_screen_color_cycle_start;

#if 1
			const unsigned char sprite_start_mask = sprite_shift_start_array[sprite_check_x];

			if (sprite_start_mask)
			{
				if (sprite_start_mask & 1) sprite_shift_regs[0] = mem_regs[E_HPOSP0];
				if (sprite_start_mask & 2) sprite_shift_regs[1] = mem_regs[E_HPOSP1];
				if (sprite_start_mask & 4) sprite_shift_regs[2] = mem_regs[E_HPOSP2];
				if (sprite_start_mask & 8) sprite_shift_regs[3] = mem_regs[E_HPOSP3];
			}
#else
			for (int spr=0;spr<4;++spr)
			{
//				if (x+sprite_screen_color_cycle_start==mem_regs[spr+E_HPOSP0])
				if (sprite_check_x == mem_regs[spr+E_HPOSP0])
					sprite_shift_regs[spr]=mem_regs[spr+E_HPOSP0];
			}
#endif

			while(next_instr_offset<x && ip<rastinsncnt) // execute instructions
			{
				// check position of sprites

				instr = &rastinsns[ip++];

				if (cycle<4) // in the previous line
					ExecuteInstruction(*instr,x+200);
				else
					ExecuteInstruction(*instr,x);

				cycle+=GetInstructionCycles(*instr);
				next_instr_offset=screen_cycles[cycle].offset;
				if (ip >= rastinsncnt)
					next_instr_offset = 1000;
			}

//			if (x>=0 && x<m_width)
			if ((unsigned)x < (unsigned)m_width)
			{
				// put pixel closest to one of the current color registers
//				rgb pixel = picture_row[x];
				e_target closest_register = FindClosestColorRegister<T_distance_function>(picture_row_index + x,x,y,restart_line);
				created_picture_row[x]=mem_regs[closest_register] >> 1;
				created_picture_targets_row[x]=closest_register;
			}
		}

		if (restart_line)
		{
			--y;
		}
		else
		{
			// add this to line cache
			if (line_cache_insn_pool_level + rastinsncnt > LINE_CACHE_INSN_POOL_SIZE)
			{
				// flush line cache
				line_cache.clear();
				line_cache_insn_pool_level = 0;
			}

			// relocate insns to pool
			auto *new_insns = line_cache_insn_pool + line_cache_insn_pool_level;
			lck.insns = new_insns;
			memcpy(new_insns, rastinsns, rastinsncnt * sizeof(rastinsns[0]));
			line_cache_insn_pool_level += rastinsncnt;

			auto& result_state = line_cache[lck];

			result_state.new_state.capture();
			result_state.color_row.assign(created_picture_row, created_picture_row + m_width);
			result_state.target_row.assign(created_picture_targets_row, created_picture_targets_row + m_width);
			memcpy(result_state.sprite_data, sprites_memory[y], sizeof result_state.sprite_data);
		}
	}
	return;
}

template<fn_rgb_distance& T_distance_function>
distance_accum_t RastaConverter::CalculateLineDistance(const screen_line &r, const screen_line &l)
{
	const int width = r.size();
	distance_accum_t distance=0;

	for (int x=0;x<width;++x)
	{
		rgb in_pixel = r[x];
		rgb out_pixel = l[x];
		distance += T_distance_function(in_pixel,out_pixel);
	}
	return distance;
};

double RastaConverter::EvaluateCreatedPicture(void)
{
	int y; // currently processed pixel
	distance_accum_t distance=0;
	++evaluations;

#if 1
	int error_index = 0;
	for (y=0;y<m_height;++y)
	{
		const auto *index_src_row = &m_created_picture[y][0];

		for(int x=0; x<m_width; ++x)
			distance += m_picture_all_errors[index_src_row[x]][error_index++];
	}
#else
	if (distance_function == RGByuvDistance)
	{
		for (y=0;y<m_height;++y)
		{
			const distance_t line_distance = CalculateLineDistance<RGByuvDistance>(m_picture[y],m_created_picture[y]);
			distance += line_distance;
		}
	}
	else
	{
		for (y=0;y<m_height;++y)
		{
			const distance_t line_distance = CalculateLineDistance<RGBEuclidianDistance>(m_picture[y],m_created_picture[y]);
			distance += line_distance;
		}
	}
#endif

	return distance;
}

void RastaConverter::FindPossibleColors()
{
	m_possible_colors_for_each_line.resize(m_height);
	set < unsigned char > set_of_colors;

	// For each screen line set the possible colors
	for (int l=m_height-1;l>=0 && !user_closed_app;--l)
	{
		vector < unsigned char > vector_of_colors;
#if 1
		if (desktop_width>=320*3)
			hline(screen,m_width*2,l,m_width*4,makecol(0xFF,0xFF,0xFF));
		else
			hline(screen,m_width,l,m_width*2,makecol(0xFF,0xFF,0xFF));

		for (int x=0;x<m_width;++x)
			set_of_colors.insert(FindAtariColorIndex(m_picture[l][x])*2);				

		// copy set to vector
		copy(set_of_colors.begin(), set_of_colors.end(), back_inserter(vector_of_colors));
#else
		vector_of_colors.push_back(0);
#endif
		m_possible_colors_for_each_line[l]=vector_of_colors;
	}
}

void RastaConverter::Init()
{
	init_finished=false;

	textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Choosing start point(s)");

	evaluations=0;
	last_best_evaluation=0;

	int init_solutions=solutions;
	double min_distance=DBL_MAX;

	if (cfg.init_type==E_INIT_LESS || cfg.init_type==E_INIT_SMART || cfg.init_type==E_INIT_EMPTY)
		init_solutions=1; // !!!
	else
	{
		if (init_solutions<500)
			init_solutions=500;
	}

	for (int i=0; i<init_solutions && !user_closed_app; ++i)
	{
		raster_picture m(m_height);
		if (cfg.init_type==E_INIT_RANDOM)
			CreateRandomRasterPicture(&m);
		else if (cfg.init_type==E_INIT_EMPTY)
			CreateEmptyRasterPicture(&m);
		else // LESS or SMART
			CreateSmartRasterPicture(&m);

		if (cfg.border)
		{
			m.mem_regs_init[E_HPOSP0]=sprite_screen_color_cycle_start-sprite_size;
			m.mem_regs_init[E_COLPM0]=0;
		}

		ExecuteRasterProgram(&m);
		double result = EvaluateCreatedPicture();
		if (result<min_distance)
		{
			ShowLastCreatedPicture();
			min_distance=result;
		}
		AddSolution(result,m);
	}
	if (!user_closed_app)
		init_finished=true;
}

void RastaConverter::AddSolution(double distance, raster_picture pic )
{
	m_solutions[distance]=pic;
}

void RastaConverter::MutateLine(raster_line &prog)
{
	//	hline(screen,width,m_currently_mutated_y,width*2,makecol(0xFF,0xFF,0xFF));

	int r=random(prog.instructions.size());
	for (int i=0;i<=r;++i) 
	{
		MutateOnce(prog);
	}
}

unsigned char LimitValueToColor(unsigned char value)
{
	if (value>127)
		value-=128;
	return value;
}

unsigned char LimitValueToPMGPosition(unsigned char value)
{
	// sprite position is 32-224, in quad size position is modulo 4
	if (value>224)
		value=224;
	else if (value<32)
		value=32;
	return value;
}

void RastaConverter::MutateOnce(raster_line &prog)
{
	int i1,i2,c,x;

	i1=random(prog.instructions.size());
	i2=i1;
	if (prog.instructions.size()>2)
	{
		do 
		{
			i2=random(prog.instructions.size());
		} while (i1!=i2);
	}

	SRasterInstruction temp;

	int mutation=random(E_MUTATION_MAX);
	switch(mutation)
	{
	case E_MUTATION_COPY_LINE_TO_NEXT_ONE:
		if (m_currently_mutated_y<m_height-1)
		{
			int next_y = m_currently_mutated_y+1;
			raster_line &next_line=m_pic->raster_lines[next_y];
			prog=next_line;
			m_current_mutations[E_MUTATION_COPY_LINE_TO_NEXT_ONE]++;
			break;
		}
	case E_MUTATION_PUSH_BACK_TO_PREV:
		if (m_currently_mutated_y>0)
		{
			int prev_y = m_currently_mutated_y-1;
			raster_line &prev_line=m_pic->raster_lines[prev_y];
			c = GetInstructionCycles(prog.instructions[i1]);
			if (prev_line.cycles+c<free_cycles)
			{
				// add it to prev line but do not remove it from the current
				prev_line.cycles+=c;
				prev_line.instructions.push_back(prog.instructions[i1]);
				m_current_mutations[E_MUTATION_PUSH_BACK_TO_PREV]++;
				break;
			}
		}
	case E_MUTATION_SWAP_LINE_WITH_PREV_ONE:
		if (m_currently_mutated_y>0)
		{
			int prev_y = m_currently_mutated_y-1;
			raster_line &prev_line=m_pic->raster_lines[prev_y];
			raster_line temp=prog;
			prog=prev_line;
			prev_line=temp;
			m_current_mutations[E_MUTATION_SWAP_LINE_WITH_PREV_ONE]++;
			break;
		}
	case E_MUTATION_ADD_INSTRUCTION:
		if (prog.cycles+2<free_cycles)
		{
			if (prog.cycles+4<free_cycles && random(2)) // 4 cycles instructions
			{
				temp.instruction=(e_raster_instruction) (E_RASTER_STA+random(3));
				temp.value=(random(128)*2);
				temp.target=(e_target) (random(E_TARGET_MAX));
				prog.instructions.insert(prog.instructions.begin()+i1,temp);
				prog.cycles+=4;
			}
			else
			{
				temp.instruction=(e_raster_instruction) (E_RASTER_LDA+random(4));
				if (random(2))
					temp.value=(random(128)*2);
				else
					temp.value=m_possible_colors_for_each_line[m_currently_mutated_y][random(m_possible_colors_for_each_line[m_currently_mutated_y].size())];

				temp.target=(e_target) (random(E_TARGET_MAX));
				c=random(m_picture[m_currently_mutated_y].size());
				temp.value=FindAtariColorIndex(m_picture[m_currently_mutated_y][c])*2;
				prog.instructions.insert(prog.instructions.begin()+i1,temp);
				prog.cycles+=2;
			}
			m_current_mutations[E_MUTATION_ADD_INSTRUCTION]++;
			break;
		}
	case E_MUTATION_REMOVE_INSTRUCTION:
		if (prog.cycles>4)
		{
			c = GetInstructionCycles(prog.instructions[i1]);
			if (prog.cycles-c>0)
			{
				prog.cycles-=c;
				prog.instructions.erase(prog.instructions.begin()+i1);
				assert(prog.cycles>0);
				m_current_mutations[E_MUTATION_REMOVE_INSTRUCTION]++;
				break;
			}
		}
	case E_MUTATION_SWAP_INSTRUCTION:
		if (prog.instructions.size()>2)
		{
			temp=prog.instructions[i1];
			prog.instructions[i1]=prog.instructions[i2];
			prog.instructions[i2]=temp;
			m_current_mutations[E_MUTATION_SWAP_INSTRUCTION]++;
			break;
		}
	case E_MUTATION_CHANGE_TARGET:
		prog.instructions[i1].target=(e_target) (random(E_TARGET_MAX));
		m_current_mutations[E_MUTATION_CHANGE_TARGET]++;
		break;
	case E_MUTATION_CHANGE_VALUE_TO_COLOR:
		if ((prog.instructions[i1].target>=E_HPOSP0 && prog.instructions[i1].target<=E_HPOSP3))
		{
			x=mem_regs[prog.instructions[i1].target]-sprite_screen_color_cycle_start;
			x+=random(sprite_size);
		}
		else
		{
			c=0;
			// find color in the next raster column
			for (x=0;x<i1-1;++x)
			{
				if (prog.instructions[x].instruction<=E_RASTER_NOP)
					c+=2;
				else
					c+=4; // cycles
			}
			while(random(5)==0)
				++c;

			if (c>=free_cycles)
				c=free_cycles-1;
			x=screen_cycles[c].offset;
			x+=random(screen_cycles[c].length);
		}
		if (x<0 || x>=m_width)
			x=random(m_width);
		i2=m_currently_mutated_y;
		// check color in next lines
		while(random(5)==0 && i2+1 < (int)m_picture.size())
			++i2;
		prog.instructions[i1].value=FindAtariColorIndex(m_picture[i2][x])*2;
		m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
		break;
	case E_MUTATION_CHANGE_VALUE:
		if (random(10)==0)
		{
			if (random(2))
				prog.instructions[i1].value=(random(128)*2);
			else
				prog.instructions[i1].value=m_possible_colors_for_each_line[m_currently_mutated_y][random(m_possible_colors_for_each_line[m_currently_mutated_y].size())];
		}
		else
		{
			c=1;
			if (random(2))
				c*=-1;
			if (random(2))
				c*=16;
			prog.instructions[i1].value+=c;
		}
		m_current_mutations[E_MUTATION_CHANGE_VALUE]++;
		break;
	}	
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
		pic->raster_lines[y].instructions[0].instruction=E_RASTER_LDA;
		if (y%2==0)
			pic->raster_lines[y].instructions[0].value=0xF;
		else
			pic->raster_lines[y].instructions[0].value=0x33;

		pic->raster_lines[y].instructions[1].instruction=E_RASTER_STA;
		pic->raster_lines[y].instructions[1].target=E_COLOR2;


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

void RastaConverter::MutateRasterProgram(raster_picture *pic)
{
	// find the best line to modify
	m_current_mutations.clear();

	if (random(10)==0) // mutate random init mem reg
	{
		int c=1;
		if (random(2))
			c*=-1;
		if (random(2))
			c*=16;

		pic->mem_regs_init[random(E_TARGET_MAX)]+=c;
		if (cfg.border)
		{
			pic->mem_regs_init[E_HPOSP0]=sprite_screen_color_cycle_start-sprite_size;
			pic->mem_regs_init[E_COLPM0]=0;
		}
	}

	if (m_currently_mutated_y>=(int) pic->raster_lines.size())
		m_currently_mutated_y=0;
	if (m_currently_mutated_y<0)
		m_currently_mutated_y=pic->raster_lines.size()-1;

	raster_line &current_line=pic->raster_lines[m_currently_mutated_y];
	MutateLine(current_line);

	if (random(20)==0)
	{
		for (int t=0;t<10;++t)
		{
			if (random(2) && m_currently_mutated_y>0)
				--m_currently_mutated_y;
			else
				m_currently_mutated_y=random(pic->raster_lines.size());

			raster_line &current_line=pic->raster_lines[m_currently_mutated_y];
			MutateLine(current_line);
		}
	}
}

void RastaConverter::ShowMutationStats()
{
	map < int, int >::iterator m,_m;
	for (m=m_current_mutations.begin(),_m=m_current_mutations.end();m!=_m;++m)
	{
		m_mutation_stats[m->first]+=m->second;
	}
	for (int i=0;i<E_MUTATION_MAX;++i)
	{
		textprintf_ex(screen, font, 0, 330+10*i, makecol(0xF0,0xF0,0xF0), 0, "%s  %d", mutation_names[i],m_mutation_stats[i]);
	}

}

void RastaConverter::SaveBestSolution()
{
	if (!init_finished)
		return;

	// execute raster program for the best created picture to set f.e. PMG data
	m_pic=&(m_solutions.begin()->second);
	//			SetSpriteBorders(&new_picture);
	ExecuteRasterProgram(m_pic);
	ShowLastCreatedPicture();
	SaveRasterProgram(string(cfg.output_file+".rp"));
	SavePMG(string(cfg.output_file+".pmg"));
	SaveScreenData  (string(cfg.output_file+".mic").c_str());
	SavePicture     (cfg.output_file,output_bitmap);
}

void Wait(int t)
{
	unsigned b;
	unsigned a=(unsigned)time( NULL );
	while (a==(unsigned)time( NULL ));
	if (t==2)
	{
		b=(unsigned)time( NULL );
		while (b==(unsigned)time( NULL ));
	}
}

void RastaConverter::FindBestSolution()
{
	m_created_picture.resize(m_height);
	for(int i=0; i<m_height; ++i)
		m_created_picture[i].resize(m_width, 0);

//	resize_rgb_picture(&m_created_picture,m_picture[0].size(),m_picture.size());
	resize_target_picture(&m_created_picture_targets,input_bitmap->w,input_bitmap->h);

	memset(m_mutation_stats,0,sizeof(m_mutation_stats));

	FindPossibleColors();

	if (!cfg.continue_processing)
		Init();

	map < double, raster_picture >::iterator m,_m;
	m_currently_mutated_y=0;

	textprintf_ex(screen, font, 0, 280, makecol(0xF0,0xF0,0xF0), 0, "Press 'S' to save.");
	textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %u", evaluations);

	unsigned last_eval = 0;

	while(!key[KEY_ESC] && !user_closed_app)
	{
		for (m=m_solutions.begin(),_m=m_solutions.end();m!=_m;++m)
		{
			if (key[KEY_S] || key[KEY_D])
			{
				SaveBestSolution();
				// wait 2 seconds
				Wait(2);
				Message("Saved.               ");
			}
			double current_distance=m->first;
			raster_picture new_picture(m_height);
			new_picture=m->second;

			m_pic=&new_picture;
			//			TestRasterProgram(&new_picture); // !!!
			MutateRasterProgram(&new_picture);
			ExecuteRasterProgram(&new_picture);

			double result = EvaluateCreatedPicture();

			if (evaluations%300==0)
			{
				last_eval = evaluations;
				textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %u  LastBest: %u", evaluations,last_best_evaluation);
				textprintf_ex(screen, font, 0, 310, makecol(0xF0,0xF0,0xF0), 0, "Norm. Dist: %f", m_solutions.begin()->first/ (((double)m_width*(double)m_height)*(MAX_COLOR_DISTANCE/10000)));
			}
			// store this solution (<= to make results more diverse)
			if ( ((solutions==1 && result<=current_distance) || 
				(solutions>1 && result<current_distance)) )
			{
				// show it only if mutation gives better picture and if a minimum amount of evals have gone by
				if (result<current_distance && evaluations - last_eval >= 20)
				{
					last_eval = evaluations;
					ShowLastCreatedPicture();
					textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %u  LastBest: %u  Solutions: %d  Cached insns: %8u", evaluations,last_best_evaluation,(int) m_solutions.size(), line_cache_insn_pool_level);
					textprintf_ex(screen, font, 0, 310, makecol(0xF0,0xF0,0xF0), 0, "Norm. Dist: %f", result/ (((double)m_width*(double)m_height)*(MAX_COLOR_DISTANCE/10000)));
					ShowMutationStats();
				}

				// if mutated solution is better than the best one, then merge the picture with the best one
				if (result<m_solutions.begin()->first)
				{
					last_best_evaluation=evaluations;
				}
				AddSolution(result,new_picture);
				break;
			}
			if (result>=current_distance) // move to the prev line even if result is equal
				--m_currently_mutated_y; 

		}

		while ((int)m_solutions.size() > solutions)
		{
			m=m_solutions.end();
			m--;
			m_solutions.erase(m);
		};
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
			rgb atari_color=atari_palette[m_created_picture[y][x]];
			int color=RGB2PIXEL(atari_color);
			putpixel(output_bitmap,x,y,color);
		}
	}


	if (desktop_width>=320*3)
		stretch_blit(output_bitmap,screen,0,0,output_bitmap->w,output_bitmap->h,input_bitmap->w*2,0,output_bitmap->w*2,destination_bitmap->h);
	else
		blit(output_bitmap,screen,0,0,m_width,0,m_width,m_height);
}

void RastaConverter::SavePMG(string name)
{
	size_t sprite,y,bit;
	unsigned char b;
	Message("Saving sprites (PMG)");

	FILE *fp=fopen(name.c_str(),"wt+");
	if (!fp)
		error("Error saving PMG handler");

	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; ---------------------------------- \n");

	if (cfg.border)
	{
		for (y=0;y<240;++y)
			for (bit=0;bit<8;++bit)
				sprites_memory[y][0][bit]=1;
	}

	fprintf(fp,"missiles\n");
	if (cfg.border)
	{
		fprintf(fp,"\t.he 00 00 00 00 00 00 00 00");

		for (y=0;y<240;++y)
		{
			fprintf(fp," 03");
			if (y%16==7)
				fprintf(fp,"\n\t.he");
		}

		fprintf(fp," 00 00 00 00 00 00 00 00\n");
	}
	else
		fprintf(fp,"\t.ds $100\n");


	for(sprite=0;sprite<4;++sprite)
	{
		fprintf(fp,"player%d\n",sprite);
		fprintf(fp,"\t.he 00 00 00 00 00 00 00 00");
		for (y=0;y<240;++y)
		{
			b=0;
			for (bit=0;bit<8;++bit)
			{
				if (y > (size_t)m_height)
					sprites_memory[y][sprite][bit]=0;

				b|=(sprites_memory[y][sprite][bit])<<(7-bit);
			}
			fprintf(fp," %02X",b);
			if (y%16==7)
				fprintf(fp,"\n\t.he");
		}
		fprintf(fp," 00 00 00 00 00 00 00 00\n");
	}
	fclose(fp);
}

bool GetInstructionFromString(string line, SRasterInstruction &instr)
{
	static char *load_names[3]=
	{
		"lda",
		"ldx",
		"ldy",
	};
	static char *store_names[3]=
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

	instr.instruction=E_RASTER_MAX;

	// check load instructions
	for (i=0;i<3;++i)
	{
		pos_instr=line.find(load_names[i]);
		if (pos_instr!=string::npos)
		{
			if (pos_instr<pos_comment)
			{
				instr.instruction= (e_raster_instruction) (E_RASTER_LDA+i);
				pos_value=line.find("$");
				if (pos_value==string::npos)
					error("Load instruction: No value for Load Register");
				++pos_value;
				string val_string=line.substr(pos_value,2);
				instr.value=String2HexValue<int>(val_string);
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
				instr.instruction=(e_raster_instruction) (E_RASTER_STA+i);
				// find target
				for (j=0;j<=E_TARGET_MAX;++j)
				{
					pos_target=line.find(mem_regs_names[j]);
					if (pos_target!=string::npos)
					{
						instr.target=(e_target) (E_COLOR0+j);
						if (instr.target==E_TARGET_MAX)
							instr.target=E_COLPM0; // !!! HACK until other sprites can be changed to HITCLR
						return true;
					}
				}
				error("Load instruction: Unknown target for store");
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
		error("Error loading reg inits");

	string line;
	SRasterInstruction instr;

	while( getline( f, line)) 
	{
		instr.target=E_TARGET_MAX;
		if (GetInstructionFromString(line,instr))
		{
			if (instr.target!=E_TARGET_MAX)
				m_pic->mem_regs_init[instr.target]=instr.value;			
		}
	}

}

void RastaConverter::LoadRasterProgram(string name)
{
	Message("Loading Raster Program");

	fstream f;
	f.open( name.c_str(), ios::in);
	if ( f.fail())
		error("Error loading Raster Program");

	string line;
	reg_a=0;

	SRasterInstruction instr;
	raster_line current_raster_line;
	current_raster_line.cycles=0;
	size_t pos;
	
	while( getline( f, line)) 
	{
		// skip filler
		if (line.find("; filler")!=string::npos)
			continue;

		// get info about the file
		pos=line.find("; Evaluations:");
		if (pos!=string::npos)
			evaluations=String2Value<unsigned int>(line.substr(pos+15));

		pos=line.find("; InputName:");
		if (pos!=string::npos)
			cfg.input_file=(line.substr(pos+13));

		pos=line.find("; CmdLine:");
		if (pos!=string::npos)
			cfg.command_line=(line.substr(pos+11));

		// if next raster line
		if (line.find("cmp byt2")!=string::npos && current_raster_line.cycles>0)
		{
			m_pic->raster_lines.push_back(current_raster_line);
			current_raster_line.cycles=0;
			current_raster_line.instructions.clear();
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

bool RastaConverter::Resume1()
{
	raster_picture pic;
	AddSolution(DBL_MAX,pic);
	m_pic=&m_solutions.begin()->second;
	LoadRegInits("output.png.rp.ini");
	LoadRasterProgram("output.png.rp");
	cfg.ProcessCmdLine();
	return true;
}

bool RastaConverter::Resume2()
{
	InitLocalStructure();
	return true;
}

void RastaConverter::SaveRasterProgram(string name)
{
	int y;
	if (m_solutions.empty())
		return;

	Message("Saving Raster Program");

	raster_picture &pic = m_solutions.begin()->second;

	FILE *fp=fopen(string(name+".ini").c_str(),"wt+");
	if (!fp)
		error("Error saving Raster Program");

	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; ---------------------------------- \n");

	fprintf(fp,"\n; Initial values \n");

	for(y=0;y<sizeof(pic.mem_regs_init);++y)
	{
		fprintf(fp,"\tlda ");
		fprintf(fp,"#$%02X\n",pic.mem_regs_init[y]);		
		fprintf(fp,"\tsta ");
		fprintf(fp,"%s\n",mem_regs_names[y]);		
	}

	// zero registers
	fprintf(fp,"\tlda #$0\n");
	fprintf(fp,"\tldx #$0\n");
	fprintf(fp,"\tldy #$0\n");

	fprintf(fp,"\n; Set proper count of wsyncs \n");
	fprintf(fp,"\n\t:2 sta wsync\n");

	fprintf(fp,"\n; Set proper picture height\n");
	fprintf(fp,"\n\nPIC_HEIGHT = %d\n",m_height);

	fclose(fp);

	fp=fopen(name.c_str(),"wt+");
	if (!fp)
		error("Error saving DLI handler");

	fprintf(fp,"; ---------------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; InputName: %s\n",cfg.input_file.c_str());
	fprintf(fp,"; CmdLine: %s\n",cfg.command_line.c_str());
	fprintf(fp,"; Evaluations: %u\n",evaluations);
	fprintf(fp,"; ---------------------------------- \n");

	fprintf(fp,"; Proper offset \n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tnop\n");
	fprintf(fp,"\tcmp byt2;\n");

	for(y=0;y<input_bitmap->h;++y)
	{
		fprintf(fp,"line%d\n",y);
		size_t prog_len=pic.raster_lines[y].instructions.size();
		for (size_t i=0;i<prog_len;++i)
		{
			SRasterInstruction instr=pic.raster_lines[y].instructions[i];
			bool save_target=false;
			bool save_value=false;
			fprintf(fp,"\t");
			switch (instr.instruction)
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
				error("Unknown instruction!");
			}
			if (save_value)
			{
				fprintf(fp,"#$%02X ; %d (spr=%d)",instr.value,instr.value,instr.value-48);
			}
			else if (save_target)
			{
				if (cfg.border)
				{
					if (instr.target==E_HPOSP0 || instr.target==E_COLPM0)
						instr.target=E_TARGET_MAX; // HITCLR
				}
				if (instr.target>E_TARGET_MAX)
					error("Unknown target in instruction!");
				fprintf(fp,"%s",mem_regs_names[instr.target]);
			}
			fprintf(fp,"\n");			
		}
		for (int cycle=pic.raster_lines[y].cycles;cycle<free_cycles;cycle+=2)
		{
			fprintf(fp,"\tnop ; filler\n");
		}
		fprintf(fp,"\tcmp byt2; on zero page so 3 cycles\n");
	}
	fprintf(fp,"; ---------------------------------- \n");
	fclose(fp);
}



void close_button_procedure()
{
	user_closed_app=true;
}


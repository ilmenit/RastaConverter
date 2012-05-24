// - warunek stopu
// - maska dokladnosci + scale
// - obszary gdzie poszczególne obiekty w³¹czone lub wy³¹czone

const char *program_version="Beta4";

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

class linear_allocator
{
public:
	linear_allocator()
		: chunk_list(NULL)
		, alloc_left(0)
	{
	}

	~linear_allocator()
	{
		clear();
	}

	void clear()
	{
		while(chunk_list)
		{
			chunk_node *next = chunk_list->next;

			free(chunk_list);

			chunk_list = next;
		}

		alloc_left = 0;
	}

	template<class T>
	T *allocate()
	{
		return new(allocate(sizeof(T))) T;
	}

	void *allocate(size_t n)
	{
		n = (n + 7) & ~7;

		if (alloc_left < n)
		{
			size_t to_alloc = 1048576 - 64 - sizeof(chunk_node);

			if (to_alloc < n)
				to_alloc = n;

			chunk_node *new_node = (chunk_node *)malloc(sizeof(chunk_node) + to_alloc);
			new_node->next = chunk_list;
			chunk_list = new_node;
			alloc_ptr = (char *)(new_node + 1);
			alloc_left = to_alloc;
		}

		void *p = alloc_ptr;
		alloc_ptr += n;
		alloc_left -= n;
		return p;
	}

private:
	struct chunk_node
	{
		chunk_node *next;
		void *align_pad;
	};

	chunk_node *chunk_list;
	char *alloc_ptr;
	size_t alloc_left;
};

linear_allocator line_cache_linear_allocator;

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
	line_machine_state entry_state;
	const SRasterInstruction *insns;
	unsigned insn_count;
	unsigned insn_hash;

	uint32_t hash()
	{
		uint32_t hash = 0;
		
		hash += (size_t)entry_state.reg_a;
		hash += (size_t)entry_state.reg_x << 8;
		hash += (size_t)entry_state.reg_y << 16;
		hash += insn_count << 24;

		for(int i=0; i<E_TARGET_MAX; ++i)
			hash += (size_t)entry_state.mem_regs[i] << (8*(i & 3));

		hash += insn_hash;

		return hash;
	}
};

bool operator==(const line_cache_key& key1, const line_cache_key& key2)
{
	if (key1.insn_hash != key2.insn_hash) return false;
	if (key1.entry_state.reg_a != key2.entry_state.reg_a) return false;
	if (key1.entry_state.reg_x != key2.entry_state.reg_x) return false;
	if (key1.entry_state.reg_y != key2.entry_state.reg_y) return false;
	if (memcmp(key1.entry_state.mem_regs, key2.entry_state.mem_regs, sizeof key1.entry_state.mem_regs)) return false;

	if (key1.insn_count != key2.insn_count) return false;

	uint32_t diff = 0;
	for(unsigned i=0; i<key1.insn_count; ++i)
		diff |= (key1.insns[i].packed ^ key2.insns[i].packed);

	if (diff)
		return false;

	return true;
}

struct line_cache_result
{
	distance_accum_t line_error;
	line_machine_state new_state;
	unsigned char *color_row;
	unsigned char *target_row;
	unsigned char sprite_data[4][8];
};

class line_cache
{
public:
	typedef pair<line_cache_key, line_cache_result> value_type;

	struct hash_node
	{
		uint32_t hash;
		value_type *value;
	};

	typedef vector<hash_node> hash_list;

	void clear()
	{
		for(int i=0; i<1024; ++i)
		{
			hash_table[i].clear();
		}
	}

	const line_cache_result *find(const line_cache_key& key, uint32_t hash) const
	{
		const hash_list& hl = hash_table[hash & 1023];

		for(hash_list::const_reverse_iterator it = hl.rbegin(), itEnd = hl.rend(); it != itEnd; ++it)
		{
			if (it->hash == hash && key == it->value->first)
				return &it->value->second;
		}

		return NULL;
	}

	line_cache_result& insert(const line_cache_key& key, uint32_t hash)
	{
		hash_list& hl = hash_table[hash & 1023];

		value_type *value = line_cache_linear_allocator.allocate<value_type>();
		value->first = key;

		hash_node node = { hash, value };
		hl.push_back(node);
		return value->second;
	}

private:

	hash_list hash_table[1024];
};

vector<line_cache> line_caches;
const size_t LINE_CACHE_INSN_POOL_SIZE = 1048576*4;
SRasterInstruction line_cache_insn_pool[LINE_CACHE_INSN_POOL_SIZE];
size_t line_cache_insn_pool_level = 0;

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
		fp=fopen((string("palettes/")+filename).c_str(),"rb");
		if (!fp)
		{
			fp=fopen((string("palettes/")+filename+string(".act")).c_str(),"rb");
			if (!fp)
			{
				fp=fopen((string("palettes/")+filename+string(".pal")).c_str(),"rb");
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

bool LoadAtariPalette(string filename);

unsigned char FindAtariColorIndex(const rgb &col)
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

void RastaConverter::SaveStatistics(const char *fn)
{
	FILE *f = fopen(fn, "w");
	if (!f)
		return;

	fprintf(f, "Iterations,Seconds,Score\n");
	for(statistics_list::const_iterator it(m_statistics.begin()), itEnd(m_statistics.end());
		it != itEnd;
		++it)
	{
		const statistics_point& pt = *it;

		fprintf(f, "%u,%u,%.6f\n", pt.evaluations, pt.seconds, NormalizeScore(pt.distance));
	}

	fclose(f);
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

	line_caches.resize(m_height);

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

}

void RastaConverter::LoadDetailsMap()
{
	Message("Loading details map");
	FIBITMAP *fbitmap = FreeImage_Load(FreeImage_GetFileType(cfg.details_file.c_str()), cfg.details_file.c_str(), 0);
	if (!fbitmap)
		error("Error loading details file");
	fbitmap=FreeImage_Rescale(fbitmap,cfg.width,cfg.height,FILTER_BOX);
	fbitmap = FreeImage_ConvertTo24Bits(fbitmap);	

	FreeImage_FlipVertical(fbitmap);

	RGBQUAD fpixel;

	int x,y;

	details_data.resize(m_height);	
	blit(input_bitmap,destination_bitmap,0,0,0,0,destination_bitmap->w,destination_bitmap->h);

	for (y=0;y<m_height;++y)
	{
		details_data[y].resize(m_width);

		for (x=0;x<m_width;++x)
		{
			FreeImage_GetPixelColor(fbitmap, x, y, &fpixel);
			// average as brightness
			details_data[y][x]=(unsigned char) ( (int) ( (int)fpixel.rgbRed + (int)fpixel.rgbGreen + (int)fpixel.rgbBlue)/3);
			if ((x+y)%2==0)
				putpixel( destination_bitmap,x,y,makecol(details_data[y][x],details_data[y][x],details_data[y][x]));
		}
		if (desktop_width>=320*3)
			stretch_blit(destination_bitmap,screen,0,0,destination_bitmap->w,destination_bitmap->h,0,0,destination_bitmap->w*2,destination_bitmap->h);
		else
			blit(destination_bitmap,screen,0,0,0,0,destination_bitmap->w,destination_bitmap->h);
	}
	FreeImage_Unload(fbitmap);
};

void RastaConverter::GeneratePictureErrorMap()
{
	if (!cfg.details_file.empty())
		LoadDetailsMap();

	unsigned int details_multiplier=255;

	for(int i=0; i<128; ++i)
	{
		m_picture_all_errors[i].resize(input_bitmap->w * input_bitmap->h);

		const rgb ref = atari_palette[i];

		distance_t *dst = &m_picture_all_errors[i][0];
		for (int y=0; y<input_bitmap->h; ++y)
		{
			const screen_line& srcrow = m_picture[y];

			for (int x=0;x<input_bitmap->w;++x)
			{
				if (!details_data.empty())
					details_multiplier=255+details_data[y][x]*cfg.details_strength;
				*dst++ = (distance_function(srcrow[x], ref)*details_multiplier)/255;
			}
		}
	}
}

void RastaConverter::OtherDithering()
{
	int i,y;

	const int w = input_bitmap->w;
	const int w1 = w - 1;

	for (y=0;y<input_bitmap->h;++y)
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
}



void RastaConverter::PrepareDestinationPicture()
{
	int x,y;
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
		for (y=0;y<m_height;++y)
		{
			for (x=0;x<m_width;++x)
			{
				rgb out_pixel=m_picture[y][x];
				out_pixel = atari_palette[FindAtariColorIndex(out_pixel)];
				int color=RGB2PIXEL(out_pixel);
				putpixel(destination_bitmap,x,y,color);
			}
		}
	}
	if (user_closed_app)
		return;

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

	if (user_closed_app)
		return false;

	SavePicture(cfg.output_file+"-dst.png",destination_bitmap);

	GeneratePictureErrorMap();
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

MixingPlan RastaConverter::DeviseBestMixingPlan(rgb color)
{
	MixingPlan result = { {0} };
	const double X = cfg.dither_strength/10; // Error multiplier
	rgb src=color;
	rgb_error e;
	e.zero(); // Error accumulator
	for(unsigned c=0; c<64; ++c)
	{
		// Current temporary value
		rgb_error temp;
		temp.r = src.r + e.r * X;
		temp.g = src.g + e.g * X;
		temp.b = src.b + e.b * X ;
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

void RastaConverter::KnollDithering()
{
	Message("Knoll Dithering             ");

	for(unsigned c=0; c<128; ++c)
	{
		luma[c] = atari_palette[c].r*299 + atari_palette[c].g*587 + atari_palette[c].b*114;
	}
	for(unsigned y=0; y<m_height; ++y)
	{
		if (desktop_width>=320*3)
			hline(screen,m_width*2,y,m_width*4,makecol(0xFF,0xFF,0x0));
		else
			hline(screen,m_width,y,m_width*2,makecol(0xFF,0xFF,0x0));

		for(unsigned x=0; x<m_width; ++x)
		{
			if (user_closed_app)
				return;

			rgb r_color = m_picture[y][x];
			unsigned map_value = threshold_map[(x & 7) + ((y & 7) << 3)];
			MixingPlan plan = DeviseBestMixingPlan(r_color);
			rgb out_pixel = atari_palette[plan.colors[ map_value ]];

			//			out_pixel = atari_palette[FindAtariColorIndex(out_pixel)];
			int color=RGB2PIXEL(out_pixel);
			putpixel(destination_bitmap,x,y,color);
		}
	}
}

/*
struct MixingPlan
{
	static const unsigned n_colors = 128;
	unsigned colors[n_colors];
};

MixingPlan DeviseBestMixingPlan(rgb color)
{
	MixingPlan result = { {0} };
	const unsigned src[3] = { color.b, color.g, color.r };
	unsigned proportion_total = 0;

	unsigned so_far[3] = {0,0,0};

	while(proportion_total < MixingPlan::n_colors)
	{
		unsigned chosen_amount = 1;
		unsigned chosen        = 0;

		const unsigned max_test_count = std::max(1u, proportion_total);

		double least_penalty = -1;
		for(unsigned index=0; index<128; ++index)
		{
			const rgb color = atari_palette[index];
			unsigned sum[3] = { so_far[0], so_far[1], so_far[2] };
			unsigned add[3] = { color.b, color.g, color.r };
			for(unsigned p=1; p<=max_test_count; p*=2)
			{
				for(unsigned c=0; c<3; ++c) sum[c] += add[c];
				for(unsigned c=0; c<3; ++c) add[c] += add[c];
				unsigned t = proportion_total + p;
				unsigned test[3] = { sum[0] / t, sum[1] / t, sum[2] / t };
				double penalty = ColorCompare(src[0],src[1],src[2],
					test[0],test[1],test[2]);
				if(penalty < least_penalty || least_penalty < 0)
				{
					least_penalty = penalty;
					chosen        = index;
					chosen_amount = p;
				}
			}
		}
		for(unsigned p=0; p<chosen_amount; ++p)
		{
			if(proportion_total >= MixingPlan::n_colors) break;
			result.colors[proportion_total++] = chosen;
		}

		const rgb color = atari_palette[chosen];
		unsigned palcolor[3] = { color.b, color.g, color.r };

		for(unsigned c=0; c<3; ++c)
			so_far[c] += palcolor[c] * chosen_amount;
	}
	// Sort the colors according to luminance
	std::sort(result.colors, result.colors+MixingPlan::n_colors, PaletteCompareLuma);
	return result;
}

void RastaConverter::YliluomaDithering()
{
	for(unsigned c=0; c<128; ++c)
	{
		luma[c] = atari_palette[c].r*299 + atari_palette[c].g*587 + atari_palette[c].b*114;
	}
	for(unsigned y=0; y<m_height; ++y)
	{
		if (desktop_width>=320*3)
			hline(screen,m_width*2,y,m_width*4,makecol(0xFF,0xFF,0x0));
		else
			hline(screen,m_width,y,m_width*2,makecol(0xFF,0xFF,0x0));

		for(unsigned x=0; x<m_width; ++x)
		{
			rgb r_color = m_picture[y][x];;
			unsigned map_value = threshold_map[(x & 7) + ((y & 7) << 3)];
			MixingPlan plan = DeviseBestMixingPlan(r_color);
			map_value = map_value * MixingPlan::n_colors / 64;
			rgb out_pixel = atari_palette[plan.colors[ map_value ]];

//			out_pixel = atari_palette[FindAtariColorIndex(out_pixel)];
			int color=RGB2PIXEL(out_pixel);
			putpixel(destination_bitmap,x,y,color);
		}
	}
}
*/

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
	i.loose.instruction=E_RASTER_NOP;
	i.loose.target=E_COLBAK;
	i.loose.value=0;
	int size = FreeImage_GetWidth(fbitmap);
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
			i.loose.instruction=(e_raster_instruction) (E_RASTER_LDA+k%3); // k%3 to cycle through A,X,Y regs
			if (k>E_COLBAK && y%2==1)
				i.loose.value=(e_target) color_position[k]+sprite_screen_color_cycle_start; // sprite position
			else
				i.loose.value=FindAtariColorIndex(color)*2;
			i.loose.target=E_COLOR0;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=2;

			// sta 
			i.loose.instruction=(e_raster_instruction) (E_RASTER_STA+k%3); // k%3 to cycle through A,X,Y regs
			i.loose.value=(random(128)*2);

			if (k>E_COLBAK && y%2==1)
				i.loose.target=(e_target) (k+4); // position
			else
				i.loose.target=(e_target) k;
			r->raster_lines[y].instructions.push_back(i);
			r->raster_lines[y].cycles+=4;	

			assert(r->raster_lines[y].cycles<free_cycles);
		}

		r->raster_lines[y].rehash();

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


inline int RastaConverter::GetInstructionCycles(const SRasterInstruction &instr)
{
	switch(instr.loose.instruction)
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
	p.r += e_r * quant_error*cfg.dither_strength;
	p.g += e_g * quant_error*cfg.dither_strength;
	p.b += e_b * quant_error*cfg.dither_strength;
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

e_target RastaConverter::FindClosestColorRegister(int index, int x,int y, bool &restart_line, distance_t& best_error)
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

		unsigned x_offset = (unsigned)(x - sprite_x);
		if (x_offset < sprite_size)		// (x>=sprite_x && x<sprite_x+sprite_size)
		{
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

	best_error = min_distance;

	return result;
}


inline void RastaConverter::ExecuteInstruction(const SRasterInstruction &instr, int x)
{
	int reg_value=-1;
	switch(instr.loose.instruction)
	{
	case E_RASTER_LDA:
		reg_a=instr.loose.value;
		break;
	case E_RASTER_LDX:
		reg_x=instr.loose.value;
		break;
	case E_RASTER_LDY:
		reg_y=instr.loose.value;
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
		if (!cfg.border || (instr.loose.target!=E_HPOSP0 && instr.loose.target!=E_COLPM0))
		{
			const unsigned hpos_index = (unsigned)(instr.loose.target - E_HPOSP0);
			if (hpos_index < 4) {
				sprite_shift_start_array[mem_regs[instr.loose.target]] &= ~(1 << hpos_index);

				mem_regs[instr.loose.target]=reg_value;

				sprite_shift_start_array[mem_regs[instr.loose.target]] |= (1 << hpos_index);
			} else {
				mem_regs[instr.loose.target]=reg_value;
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
		i.loose.instruction=E_RASTER_NOP;
		i.loose.target=E_COLBAK;
		i.loose.value=0;
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

distance_accum_t RastaConverter::ExecuteRasterProgram(raster_picture *pic)
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
	distance_accum_t total_error = 0;

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
		const SRasterInstruction *__restrict rastinsns = &pic->raster_lines[y].instructions[0];
		const int rastinsncnt = pic->raster_lines[y].instructions.size();

		line_cache_key lck;
		lck.entry_state.capture();
		lck.insns = rastinsns;
		lck.insn_count = rastinsncnt;
		lck.insn_hash = pic->raster_lines[y].hash;

		const uint32_t lck_hash = lck.hash();

		// check line cache
		unsigned char * __restrict created_picture_row = &m_created_picture[y][0];
		unsigned char * __restrict created_picture_targets_row = &m_created_picture_targets[y][0];

		const line_cache_result *cached_line_result = line_caches[y].find(lck, lck_hash);
		if (cached_line_result)
		{
			// sweet! cache hit!!
			cached_line_result->new_state.apply();
			memcpy(created_picture_row, cached_line_result->color_row, m_width);
			memcpy(created_picture_targets_row, cached_line_result->target_row, m_width);
			memcpy(sprites_memory[y], cached_line_result->sprite_data, sizeof sprites_memory[y]);
			shift_start_array_dirty = true;

			total_error += cached_line_result->line_error;
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

		const int picture_row_index = m_width * y;

		distance_accum_t total_line_error = 0;

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

			if ((unsigned)x < (unsigned)m_width)		// x>=0 && x<m_width
			{
				// put pixel closest to one of the current color registers
				distance_t closest_dist;
				e_target closest_register = FindClosestColorRegister(picture_row_index + x,x,y,restart_line,closest_dist);
				total_line_error += closest_dist;
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
			total_error += total_line_error;

			// add this to line cache
			if (line_cache_insn_pool_level + rastinsncnt > LINE_CACHE_INSN_POOL_SIZE)
			{
				// flush line cache
				for(int y2=0; y2<m_height; ++y2)
					line_caches[y2].clear();

				line_cache_linear_allocator.clear();
				line_cache_insn_pool_level = 0;
			}

			// relocate insns to pool
			SRasterInstruction *new_insns = line_cache_insn_pool + line_cache_insn_pool_level;
			lck.insns = new_insns;
			memcpy(new_insns, rastinsns, rastinsncnt * sizeof(rastinsns[0]));
			line_cache_insn_pool_level += rastinsncnt;

			line_cache_result& result_state = line_caches[y].insert(lck, lck_hash);

			result_state.line_error = total_line_error;
			result_state.new_state.capture();
			result_state.color_row = (unsigned char *)line_cache_linear_allocator.allocate(m_width);
			memcpy(result_state.color_row, created_picture_row, m_width);

			result_state.target_row = (unsigned char *)line_cache_linear_allocator.allocate(m_width);
			memcpy(result_state.target_row, created_picture_targets_row, m_width);

			memcpy(result_state.sprite_data, sprites_memory[y], sizeof result_state.sprite_data);
		}
	}

	return total_error;
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

	int error_index = 0;
	for (y=0;y<m_height;++y)
	{
		const unsigned char *index_src_row = &m_created_picture[y][0];

		for(int x=0; x<m_width; ++x)
			distance += m_picture_all_errors[index_src_row[x]][error_index++];
	}

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

		double result = ExecuteRasterProgram(&m); ++evaluations;
//		double result = EvaluateCreatedPicture();
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

	prog.rehash();
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
		} while (i1==i2);
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
			prog.swap(prev_line);
			m_current_mutations[E_MUTATION_SWAP_LINE_WITH_PREV_ONE]++;
			break;
		}
	case E_MUTATION_ADD_INSTRUCTION:
		if (prog.cycles+2<free_cycles)
		{
			if (prog.cycles+4<free_cycles && random(2)) // 4 cycles instructions
			{
				temp.loose.instruction=(e_raster_instruction) (E_RASTER_STA+random(3));
				temp.loose.value=(random(128)*2);
				temp.loose.target=(e_target) (random(E_TARGET_MAX));
				prog.instructions.insert(prog.instructions.begin()+i1,temp);
				prog.cycles+=4;
			}
			else
			{
				temp.loose.instruction=(e_raster_instruction) (E_RASTER_LDA+random(4));
				if (random(2))
					temp.loose.value=(random(128)*2);
				else
					temp.loose.value=m_possible_colors_for_each_line[m_currently_mutated_y][random(m_possible_colors_for_each_line[m_currently_mutated_y].size())];

				temp.loose.target=(e_target) (random(E_TARGET_MAX));
				c=random(m_picture[m_currently_mutated_y].size());
				temp.loose.value=FindAtariColorIndex(m_picture[m_currently_mutated_y][c])*2;
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
		prog.instructions[i1].loose.target=(e_target) (random(E_TARGET_MAX));
		m_current_mutations[E_MUTATION_CHANGE_TARGET]++;
		break;
	case E_MUTATION_CHANGE_VALUE_TO_COLOR:
		if ((prog.instructions[i1].loose.target>=E_HPOSP0 && prog.instructions[i1].loose.target<=E_HPOSP3))
		{
			x=mem_regs[prog.instructions[i1].loose.target]-sprite_screen_color_cycle_start;
			x+=random(sprite_size);
		}
		else
		{
			c=0;
			// find color in the next raster column
			for (x=0;x<i1-1;++x)
			{
				if (prog.instructions[x].loose.instruction<=E_RASTER_NOP)
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
		prog.instructions[i1].loose.value=FindAtariColorIndex(m_picture[i2][x])*2;
		m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
		break;
	case E_MUTATION_CHANGE_VALUE:
		if (random(10)==0)
		{
			if (random(2))
				prog.instructions[i1].loose.value=(random(128)*2);
			else
				prog.instructions[i1].loose.value=m_possible_colors_for_each_line[m_currently_mutated_y][random(m_possible_colors_for_each_line[m_currently_mutated_y].size())];
		}
		else
		{
			c=1;
			if (random(2))
				c*=-1;
			if (random(2))
				c*=16;
			prog.instructions[i1].loose.value+=c;
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

		int targ;
		do {
			targ = random(E_TARGET_MAX);
		} while (targ == E_COLBAK);

		pic->mem_regs_init[targ]+=c;
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
	SaveStatistics((cfg.output_file+".csv").c_str());
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

RastaConverter::RastaConverter()
	: last_best_evaluation(0)
	, evaluations(0)
	, m_currently_mutated_y(0)
	, init_finished(false)
{
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
	else
		init_finished = true;

	const time_t time_start = time(NULL);
	map < double, raster_picture >::iterator m,_m;
	m_currently_mutated_y=0;

	textprintf_ex(screen, font, 0, 280, makecol(0xF0,0xF0,0xF0), 0, "Press 'S' to save.");
	textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %u", evaluations);

	unsigned last_eval = 0;
	bool clean_first_evaluation = cfg.continue_processing;
	clock_t last_rate_check_time = clock();
	
	while(!key[KEY_ESC] && !user_closed_app && evaluations < cfg.max_evals)
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

			// If this is the first evaluation after a continue, we don't do any mutations in order to
			// get the correct statistic. This avoids taking the first mutation unconditionally.
			if (clean_first_evaluation)
				clean_first_evaluation = false;
			else
				MutateRasterProgram(&new_picture);

			double result = ExecuteRasterProgram(&new_picture); ++evaluations;

//			double result = EvaluateCreatedPicture();

			if (evaluations%1000==0)
			{
				double rate = 0;
				clock_t next_rate_check_time = clock();

				if (next_rate_check_time > last_rate_check_time)
				{
					double clock_delta = (double)(next_rate_check_time - last_rate_check_time);
					rate = 1000.0 * (double)CLOCKS_PER_SEC / clock_delta;

					// clamp the rate if it is ridiculous... Allegro uses an unbounded sprintf(). :(
					if (rate < 0 || rate > 10000000.0)
						rate = 0;

					last_rate_check_time = next_rate_check_time;
				}

				last_eval = evaluations;
				textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %8u  LastBest: %8u  Solutions: %d  Cached insns: %8u Rate: %6.1f", evaluations,last_best_evaluation,(int) m_solutions.size(), line_cache_insn_pool_level, rate);
				textprintf_ex(screen, font, 0, 310, makecol(0xF0,0xF0,0xF0), 0, "Norm. Dist: %f", NormalizeScore(m_solutions.begin()->first));
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
					textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %8u  LastBest: %8u  Solutions: %d  Cached insns: %8u", evaluations,last_best_evaluation,(int) m_solutions.size(), line_cache_insn_pool_level);
					textprintf_ex(screen, font, 0, 310, makecol(0xF0,0xF0,0xF0), 0, "Norm. Dist: %f", NormalizeScore(result));
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

			if (evaluations % 10000 == 0)
			{
				statistics_point stats;
				stats.evaluations = evaluations;
				stats.seconds = (unsigned)(time(NULL) - time_start);
				stats.distance = current_distance;

				m_statistics.push_back(stats);
			}

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

bool GetInstructionFromString(const string& line, SRasterInstruction &instr)
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
					error("Load instruction: No value for Load Register");
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
						if (instr.loose.target==E_TARGET_MAX)
							instr.loose.target=E_COLPM0; // !!! HACK until other sprites can be changed to HITCLR

						instr.loose.value = 0;
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
						m_pic->mem_regs_init[instr.loose.target] = a;
					break;
				case E_RASTER_STX:
					if (instr.loose.target != E_TARGET_MAX)
						m_pic->mem_regs_init[instr.loose.target] = x;
					break;
				case E_RASTER_STY:
					if (instr.loose.target != E_TARGET_MAX)
						m_pic->mem_regs_init[instr.loose.target] = y;
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
		error("Error loading Raster Program");

	string line;
	reg_a=0;

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
			evaluations=String2Value<unsigned int>(line.substr(pos+15));

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
			m_pic->raster_lines.push_back(current_raster_line);
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
	fprintf(fp,"; Score: %g\n",NormalizeScore(m_solutions.begin()->first));
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
				error("Unknown instruction!");
			}
			if (save_value)
			{
				fprintf(fp,"#$%02X ; %d (spr=%d)",instr.loose.value,instr.loose.value,instr.loose.value-48);
			}
			else if (save_target)
			{
				if (cfg.border)
				{
					if (instr.loose.target==E_HPOSP0 || instr.loose.target==E_COLPM0)
						instr.loose.target=E_TARGET_MAX; // HITCLR
				}
				if (instr.loose.target>E_TARGET_MAX)
					error("Unknown target in instruction!");
				fprintf(fp,"%s",mem_regs_names[instr.loose.target]);
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

double RastaConverter::NormalizeScore(double raw_score)
{
	return raw_score / (((double)m_width*(double)m_height)*(MAX_COLOR_DISTANCE/10000));
}


void close_button_procedure()
{
	user_closed_app=true;
}


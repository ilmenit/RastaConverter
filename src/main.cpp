const char *program_version="0.9";

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

extern "C" {
#include "cycle_map.h"
};

// Cycle where WSYNC starts - 105?
#define WSYNC_START 104
// Normal screen CPU cycle 24-104 = 80 cycles = 160 color cycles

// global variables
int solutions=1;
const int sprite_screen_color_cycle_start=48;
const int sprite_size=32;

void QuitFunc(void)
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

int screen_color_depth;
bool user_closed_app=false;

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

typedef int color_index;
typedef int pixel_number;

// CPU registers

enum e_raster_instruction {
// DO NOT CHANGE ORDER OF THOSE. A LOT OF THINGS DEPEND ON THE ORDER. ADD STH AT THE END IF YOU NEED!
	// 2 bytes instruction
	E_RASTER_LDA,
	E_RASTER_LDX,
	E_RASTER_LDY,
	E_RASTER_NOP,
	// 4 bytes intructions
	E_RASTER_STA,
	E_RASTER_STX,
	E_RASTER_STY,

	E_RASTER_MAX,
}; 

enum e_target {
	E_COLOR0,
	E_COLOR1,
	E_COLOR2,
	E_COLBAK,
	E_COLPM0,
	E_COLPM1,
	E_COLPM2,
	E_COLPM3,
	E_HPOSP0,
	E_HPOSP1,
	E_HPOSP2,
	E_HPOSP3,
	E_TARGET_MAX,
};

unsigned char reg_a, reg_x, reg_y;

unsigned char mem_regs[E_TARGET_MAX];
unsigned char sprite_shift_regs[4];

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

BITMAP *input_bitmap;	
BITMAP *output_bitmap;

// we limit PMG memory to visible 240 bytes
unsigned char sprites_memory[4][240][8]; // we convert it to 240 bytes of PMG memory at the end of processing.

enum e_mutation_type {
	E_MUTATION_PUSH_BACK_TO_PREV, 
	E_MUTATION_COPY_LINE_TO_NEXT_ONE, 
	E_MUTATION_SWAP_LINE_WITH_PREV_ONE, 
	E_MUTATION_ADD_INSTRUCTION,
	E_MUTATION_REMOVE_INSTRUCTION,
	E_MUTATION_SWAP_INSTRUCTION,
	E_MUTATION_CHANGE_TARGET, 
	E_MUTATION_CHANGE_VALUE, // -1,+1,-16,+16
	E_MUTATION_CHANGE_VALUE_TO_COLOR, 
	E_MUTATION_MAX,
};

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

struct SRasterInstruction {
	e_raster_instruction instruction;
	e_target target;
	unsigned char value;
};

class screen_line {
private:
	vector < rgb > pixels;
public:
	void Resize(size_t i)
	{
		pixels.resize(i);
	}
	rgb& operator[](size_t i)
	{
		return pixels[i];
	}
	size_t size()
	{
		return pixels.size();
	}
};

class line_target {
private:
	vector < e_target > pixels; // target of the pixel f.e. COLBAK
public:
	void Resize(size_t i)
	{
		pixels.resize(i);
	}
	e_target& operator[](size_t i)
	{
		return pixels[i];
	}
	size_t size()
	{
		return pixels.size();
	}
};

// Tables of cycles

struct ScreenCycle {
	int offset; // position on the screen (can be <0 - previous line)
	int length; // length in pixels for 2 CPU cycles
};

#define CYCLES_MAX 114
int free_cycles=0; // must be set depending on the mode, PMG, LMS etc.
ScreenCycle screen_cycles[CYCLES_MAX];

void RGBtoYUV(double r, double g, double b, double &y, double &u, double &v)
{
	y = 0.299*r + 0.587*g + 0.114*b;
	u= (b-y)*0.565;
	v= (r-y)*0.713;
}

#define MAX_COLOR_DISTANCE (255*255*3)
double RGBDistance(rgb &col1, rgb &col2)
{
	double distance=0;
	
	// euclidian distance
#if 0
	distance+=((double)col1.r-(double)col2.r)*((double)col1.r-(double)col2.r);
	distance+=((double)col1.g-(double)col2.g)*((double)col1.g-(double)col2.g);
	distance+=((double)col1.b-(double)col2.b)*((double)col1.b-(double)col2.b);
	
#else
	// this one is better :)
	double y1,u1,v1;
	double y2,u2,v2;

	RGBtoYUV(col1.r,col1.g,col1.b,y1,u1,v1);
	RGBtoYUV(col2.r,col2.g,col2.b,y2,u2,v2);
	distance+=(y2-y1)*(y2-y1);
	distance+=(u2-u1)*(u2-u1);
	distance+=(v2-v1)*(v2-v1);
#endif
	return distance;
}


struct raster_line {
	vector < SRasterInstruction > instructions;
	raster_line()
	{
		cycles=0;
	}
	int cycles; // cache, to chech if we can add/remove new instructions
};

struct raster_picture {
	unsigned char mem_regs_init[E_TARGET_MAX];
	vector < raster_line > raster_lines;
	raster_picture::raster_picture()
	{
	}

	raster_picture(size_t height)
	{
		raster_lines.resize(height);
	}
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

void resize_target_picture(vector < line_target > *picture, size_t width, size_t height)
{
	size_t y;
	picture->resize(height);
	for (y=0;y<height;++y)
	{
		(*picture)[y].Resize(width);
	}
}

class RastaConverter {
private:
	FILE *out, *in;
	PALETTE palette;
	BITMAP *expected_bitmap;
	FIBITMAP *fbitmap; 

	// configuration
	Configuration cfg;

	vector < screen_line > m_picture; 

	// private functions
	void InitLocalStructure();

	vector < screen_line > *m_original_picture;
	int width,height; // picture size

	vector < screen_line > m_created_picture;
	vector < line_target > m_created_picture_targets;
	vector < screen_line > m_best_created_picture;
	map < double, raster_picture > m_solutions;
	vector < vector < unsigned char > > m_possible_colors_for_each_line;
	vector < vector < rgb_error > > error_map;

	bool init_finished;
	void Init();

	void ClearErrorMap();
	void CreateEmptyRasterPicture(raster_picture *);
	void CreateSmartRasterPicture(raster_picture *);
	void CreateRandomRasterPicture(raster_picture *);
	void DiffuseError( int x, int y, double quant_error, double e_r,double e_g,double e_b);

	void ExecuteInstruction(SRasterInstruction &instr, int x);
	int GetInstructionCycles(SRasterInstruction &instr);
	void ExecuteRasterProgram(raster_picture *, bool use_dither);
	void SetSpriteBorders(raster_picture *);
	double EvaluateCreatedPicture(void);
	double CalculateLineDistance(screen_line &r, screen_line &l);

	void AddSolution(double,raster_picture);

	raster_picture *m_pic;
	void MutateRasterProgram(raster_picture *pic);
	void TestRasterProgram(raster_picture *pic);

	int m_currently_mutated_y;

	unsigned int evaluations;
	unsigned int last_best_evaluation;
	void MutateLine(raster_line &);
	void MutateOnce(raster_line &);

	int m_mutation_stats[E_MUTATION_MAX];
	map < int,int > m_current_mutations;
	void ShowMutationStats();
	e_target FindClosestColorRegister(rgb pixel,int x,int y, bool &restart_line);

	void SaveRasterProgram(string name);
	void SavePMG(string name);
	bool SaveScreenData(const char *filename);
	bool SavePicture(string filename);

public:
	void FindBestSolution();
	void SaveBestSolution();
	void ShowLastCreatedPicture();

	void ShowBestPicture();
	RastaConverter(Configuration &c);
	bool ProcessInit();
	bool LoadInputBitmap();
};

vector < rgb > atari_palette; // 128 colors in mode 15!
unsigned char FindAtariColorIndex(rgb &col);
bool LoadAtariPalette(string filename);


bool RastaConverter::SavePicture(string filename)
{
	Message("Saving picture");

	stretch_blit(output_bitmap,expected_bitmap,0,0,output_bitmap->w,output_bitmap->h,0,0,expected_bitmap->w,expected_bitmap->h);
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
	if (cfg.width<=0 || cfg.height<=0)
	{
		cfg.width = FreeImage_GetWidth(fbitmap);
		cfg.height = FreeImage_GetHeight(fbitmap);
	}
	else
		fbitmap=FreeImage_Rescale(fbitmap,cfg.width,cfg.height,cfg.rescale_filter);

	if (screen_color_depth==32)
		fbitmap=FreeImage_ConvertTo32Bits(fbitmap);
	else
		fbitmap=FreeImage_ConvertTo24Bits(fbitmap);

	FreeImage_FlipVertical(fbitmap);

	set_palette(palette);
	input_bitmap  = create_bitmap_ex(screen_color_depth,cfg.width,cfg.height);
	output_bitmap  = create_bitmap_ex(screen_color_depth,cfg.width,cfg.height);
	expected_bitmap = create_bitmap_ex(screen_color_depth,cfg.width*2,cfg.height);
	return true;
}

void RastaConverter::InitLocalStructure()
{
	int x,y;

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
		}
	}
	clear_bitmap(screen);
	// Show our picture
	draw_sprite(screen, input_bitmap, 0, 0);
};

void RastaConverter::ShowBestPicture()
{
	int x,y;
	// Draw new picture on the screen
	for (y=0;y<input_bitmap->h;++y)
	{
		for (x=0;x<input_bitmap->w;++x)
		{
			rgb atari_color=atari_palette[FindAtariColorIndex(m_picture[y][x])];
			int color=RGB2PIXEL(atari_color);
			putpixel(output_bitmap,x,y,color);
		}
	}
	blit(output_bitmap,screen,0,0,input_bitmap->w*2,0,output_bitmap->w,output_bitmap->h);
}

bool RastaConverter::ProcessInit()
{
	InitLocalStructure();
	ShowBestPicture();
	return true;
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
			atari_palette.push_back(col);
	}
	fclose(fp);
	return true;
}

unsigned char FindAtariColorIndex(rgb &col)
{
	unsigned char i;
	// Find the most similar color in the Atari Palette
	unsigned char most_similar=0;
	double distance;
	double min_distance=DBL_MAX;
	for(i=0;i<128;++i)
	{
		distance=RGBDistance(col,atari_palette[i]);
		if (distance<min_distance)
		{
			min_distance=distance;
			most_similar=i;
		}
	}
	return most_similar;
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
	for(y=0;y<output_bitmap->h;++y)
	{
		// encode 4 pixel colors in byte

		for (x=0;x<output_bitmap->w;x+=4)
		{
			unsigned char pix=0;
			a=ConvertColorRegisterToRawData(m_created_picture_targets[y][x]);
			b=ConvertColorRegisterToRawData(m_created_picture_targets[y][x+1]);
			c=ConvertColorRegisterToRawData(m_created_picture_targets[y][x+2]);
			d=ConvertColorRegisterToRawData(m_created_picture_targets[y][x+3]);
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


RastaConverter::RastaConverter(Configuration &a_c)
:
cfg(a_c)
{
}

void RastaConverter::ClearErrorMap()
{
	// set proper size if empty
	if (error_map.empty())
	{
		error_map.resize(height+1);
		for (int y=0;y<height+1;++y)
		{
			error_map[y].resize(width+1);
		}
	}
	// clear the map
	for (size_t y=0;y<height;++y)
	{
		for (size_t x=0;x<width;++x)
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

	r->mem_regs_init[E_HPOSP0]=random(width);
	r->mem_regs_init[E_HPOSP1]=random(width);
	r->mem_regs_init[E_HPOSP2]=random(width);
	r->mem_regs_init[E_HPOSP3]=random(width);

	x=r->mem_regs_init[E_HPOSP0]; 
	r->mem_regs_init[E_COLPM0]=FindAtariColorIndex((*m_original_picture)[0][x])*2;
	x=r->mem_regs_init[E_HPOSP1]; 
	r->mem_regs_init[E_COLPM1]=FindAtariColorIndex((*m_original_picture)[0][x])*2;
	x=r->mem_regs_init[E_HPOSP2]; 
	r->mem_regs_init[E_COLPM2]=FindAtariColorIndex((*m_original_picture)[0][x])*2;
	x=r->mem_regs_init[E_HPOSP3]; 
	r->mem_regs_init[E_COLPM3]=FindAtariColorIndex((*m_original_picture)[0][x])*2;

	dest_regs=8;
	dest_colors=dest_regs+4;

	FreeImage_FlipVertical(fbitmap);

	int size = FreeImage_GetWidth(fbitmap);
	// in line 0 we set init registers
	for (y=0;y<r->raster_lines.size();++y)
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
		for (int k=0;k<dest_regs && k<sorted_colors.size();++k,++m)
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

	r->mem_regs_init[E_HPOSP0]=random(width);
	r->mem_regs_init[E_HPOSP1]=random(width);
	r->mem_regs_init[E_HPOSP2]=random(width);
	r->mem_regs_init[E_HPOSP3]=random(width);

	x=r->mem_regs_init[E_HPOSP0]; 
	r->mem_regs_init[E_COLPM0]=FindAtariColorIndex((*m_original_picture)[0][x])*2;
	x=r->mem_regs_init[E_HPOSP1]; 
	r->mem_regs_init[E_COLPM1]=FindAtariColorIndex((*m_original_picture)[0][x])*2;
	x=r->mem_regs_init[E_HPOSP2]; 
	r->mem_regs_init[E_COLPM2]=FindAtariColorIndex((*m_original_picture)[0][x])*2;
	x=r->mem_regs_init[E_HPOSP3]; 
	r->mem_regs_init[E_COLPM3]=FindAtariColorIndex((*m_original_picture)[0][x])*2;

	for (size_t y=0;y<r->raster_lines.size();++y)
	{
		// lda random
		i.instruction=E_RASTER_LDA;
		r->raster_lines[y].cycles+=2;
		x=screen_cycles[r->raster_lines[y].cycles].offset; if (x<0) x=0; x+=random(screen_cycles[r->raster_lines[y].cycles].length);
		i.value=FindAtariColorIndex((*m_original_picture)[y][x])*2;
//		i.value=FindAtariColorIndex((*m_original_picture)[y][random((*m_original_picture)[y].size())]);;
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
		x=screen_cycles[r->raster_lines[y].cycles].offset; if (x<0) x=0; x+=random(screen_cycles[r->raster_lines[y].cycles].length);
		i.value=FindAtariColorIndex((*m_original_picture)[y][x])*2;
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
		x=screen_cycles[r->raster_lines[y].cycles].offset; if (x<0) x=0; x+=random(screen_cycles[r->raster_lines[y].cycles].length);
		i.value=FindAtariColorIndex((*m_original_picture)[y][x])*2;
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
		x=screen_cycles[r->raster_lines[y].cycles].offset; if (x<0) x=0; x+=random(screen_cycles[r->raster_lines[y].cycles].length);
		i.value=FindAtariColorIndex((*m_original_picture)[y][x])*2;
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


int RastaConverter::GetInstructionCycles(SRasterInstruction &instr)
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


e_target RastaConverter::FindClosestColorRegister(rgb pixel, int x,int y, bool &restart_line)
{
	double distance;
	int sprite_bit;
	int best_sprite_bit;
	e_target result=E_COLBAK;
	double min_distance=DBL_MAX;
	bool sprite_covers_colbak=false;

	// check sprites

	// Sprites priority is 0,1,2,3

	for (int temp=E_COLPM0;temp<=E_COLPM3;++temp)
	{
		int sprite_pos=sprite_shift_regs[temp-E_COLPM0];

		int sprite_x=sprite_pos-sprite_screen_color_cycle_start;
		if (x>=sprite_x && x<sprite_x+sprite_size)
		{
			sprite_bit=(x-sprite_x)/4; // bit of this sprite memory
			assert(sprite_bit<8);

			sprite_covers_colbak=true;

			distance = RGBDistance(pixel,atari_palette[mem_regs[temp]/2]);
			if (sprites_memory[temp-E_COLPM0][y][sprite_bit])
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
#if 1
	int last_color_register;

	if (sprite_covers_colbak)
		last_color_register=E_COLOR2; // COLBAK is not used
	else
		last_color_register=E_COLBAK;

	for (int temp=E_COLOR0;temp<=last_color_register;++temp)
	{
		distance = RGBDistance(pixel,atari_palette[mem_regs[temp]/2]);
		if (distance<min_distance)
		{
			min_distance=distance;
			result=(e_target) temp;
		}
	}
#endif

	// the best color is in sprite, then set the proper bit of the sprite memory and then restart this line
	if (result>=E_COLPM0 && result<=E_COLPM3)
	{
		// if PMG bit has been modified, then restart this line, because previous pixels of COLBAK may be covered
		if (sprites_memory[result-E_COLPM0][y][best_sprite_bit]==false)
		{
			restart_line=true;
			sprites_memory[result-E_COLPM0][y][best_sprite_bit]=true;
		}

	}
	return result;
}


void RastaConverter::ExecuteInstruction(SRasterInstruction &instr, int x)
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
		if (cfg.border)
		{
			// make write to sprite0 4 cycle nop
			if (instr.target!=E_HPOSP0 && instr.target!=E_COLPM0)
				mem_regs[instr.target]=reg_value;
		}
		else
			mem_regs[instr.target]=reg_value;
	}
}



void RastaConverter::SetSpriteBorders(raster_picture *pic)
{
	int y;
	SRasterInstruction i;
	for (y=0;y<height;++y)
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

void RastaConverter::ExecuteRasterProgram(raster_picture *pic, bool use_dither)
{
	int x,y; // currently processed pixel

	int cycle;
	int next_instr_offset;
	int ip; // instruction pointer

	SRasterInstruction *instr;

	reg_a=0;
	reg_x=0;
	reg_y=0;
	memset(sprite_shift_regs,0,sizeof(sprite_shift_regs));
	memcpy(mem_regs,pic->mem_regs_init,sizeof(pic->mem_regs_init));
	memset(sprites_memory,0,sizeof(sprites_memory));

	if (use_dither)
		ClearErrorMap();

	bool restart_line=false;
	for (y=0;y<height;++y)
	{
		if (restart_line)
			RestoreLineRegs();
		else
			StoreLineRegs();

		restart_line=false;
		ip=0;
		cycle=0;
		next_instr_offset=screen_cycles[cycle].offset;

		// on new line clear sprite shifts and wait to be taken from mem_regs
		memset(sprite_shift_regs,0,sizeof(sprite_shift_regs));

		for (x=-sprite_screen_color_cycle_start;x<176;++x)
		{
			// check position of sprites
			for (int spr=0;spr<4;++spr)
			{
				if (x+sprite_screen_color_cycle_start==mem_regs[spr+E_HPOSP0])
					sprite_shift_regs[spr]=mem_regs[spr+E_HPOSP0];
			}

			while(next_instr_offset<x && ip<pic->raster_lines[y].instructions.size()) // execute instructions
			{
				// check position of sprites

				instr = &pic->raster_lines[y].instructions[ip++];
				if (cycle<4) // in the previous line
					ExecuteInstruction(*instr,x+200);
				else
					ExecuteInstruction(*instr,x);

				cycle+=GetInstructionCycles(*instr);
				next_instr_offset=screen_cycles[cycle].offset;
			}
			if (x>=0 && x<width)
			{
				// put pixel closest to one of the current color registers
				rgb pixel = (*m_original_picture)[y][x];
				e_target closest_register = FindClosestColorRegister(pixel,x,y,restart_line);
				rgb out_pixel = atari_palette[mem_regs[closest_register]/2];

				if (use_dither)
				{
					// Error diffusion
					// 1. Calculate current error for the pixel
					// 2(3). Diffuse current error from current pixel right and down 
					// 3(2). Add error from error map to current pixel

					// 1.
					rgb in_pixel = (*m_original_picture)[y][x];
					rgb_error current_error;
					current_error.r=(int)in_pixel.r-(int)out_pixel.r;
					current_error.g=(int)in_pixel.g-(int)out_pixel.g;
					current_error.b=(int)in_pixel.b-(int)out_pixel.b;

					// 2.
					if ((x+y)%2==0)
					{
						DiffuseError(x+1,y,cfg.dither_level,current_error.r,current_error.g,current_error.b);
						DiffuseError(x,y+1,cfg.dither_level,current_error.r,current_error.g,current_error.b);
					}

					// 3.
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
						p.g=255;
					else if (p.g<0)
						p.g=0;

					pixel.r=(unsigned char) p.r;
					pixel.g=(unsigned char) p.g;
					pixel.b=(unsigned char) p.b;

					closest_register = FindClosestColorRegister(pixel,x,y,restart_line);
					out_pixel = atari_palette[mem_regs[closest_register]/2];
				} // end if dither
				m_created_picture[y][x]=out_pixel;
				m_created_picture_targets[y][x]=closest_register;
			}
		}
		if (restart_line)
			--y;
	}
	return;
}

double RastaConverter::CalculateLineDistance(screen_line &r, screen_line &l)
{
	int width=r.size();
	double distance=0;

	for (int x=0;x<width;++x)
	{
		rgb in_pixel = r[x];
		rgb out_pixel = l[x];
		distance+=RGBDistance(in_pixel,out_pixel);
	}
	return distance;
};

double RastaConverter::EvaluateCreatedPicture(void)
{
	int y; // currently processed pixel
	double distance=0;
	++evaluations;

	for (y=0;y<height;++y)
	{
		double line_distance = CalculateLineDistance((*m_original_picture)[y],m_created_picture[y]);
		distance+=line_distance;
	}
	return distance;
}

void RastaConverter::Init()
{
	init_finished=false;
	m_original_picture=&m_picture;
	height=(int) m_original_picture->size();
	width=(int) (*m_original_picture)[0].size();

	memset(m_mutation_stats,0,sizeof(m_mutation_stats));

	m_possible_colors_for_each_line.resize(height);
	set < unsigned char > set_of_colors;

	// For each screen line set the possible colors
	for (int l=height-1;l>=0 && !user_closed_app;--l)
	{
		vector < unsigned char > vector_of_colors;
#if 1
		hline(screen,width,l,width*2,makecol(0xFF,0xFF,0xFF));

		for (int x=0;x<width;++x)
			set_of_colors.insert(FindAtariColorIndex((*m_original_picture)[l][x])*2);				

		// copy set to vector
		copy(set_of_colors.begin(), set_of_colors.end(),back_inserter(vector_of_colors));
#else
		vector_of_colors.push_back(0);
#endif
		m_possible_colors_for_each_line[l]=vector_of_colors;
	}
	textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Choosing start point(s)");

	evaluations=0;
	last_best_evaluation=0;

	int init_solutions=solutions;
	double min_distance=DBL_MAX;

	if (cfg.init_type==E_INIT_SMART || cfg.init_type==E_INIT_EMPTY)
		init_solutions=1; // !!!
	else
	{
		if (init_solutions<500)
			init_solutions=500;
	}

	for (size_t i=0;i<init_solutions && !user_closed_app;++i)
	{
		raster_picture m(m_original_picture->size());
		if (cfg.init_type==E_INIT_RANDOM)
			CreateRandomRasterPicture(&m);
		else if (cfg.init_type==E_INIT_EMPTY)
			CreateEmptyRasterPicture(&m);
		else
			CreateSmartRasterPicture(&m);

		if (cfg.border)
		{
			m.mem_regs_init[E_HPOSP0]=sprite_screen_color_cycle_start-sprite_size;
			m.mem_regs_init[E_COLPM0]=0;
		}

		ExecuteRasterProgram(&m,false);
		double result = EvaluateCreatedPicture();
		if (result<min_distance)
		{
			ShowLastCreatedPicture();
			min_distance=result;
			m_best_created_picture=m_created_picture;
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
	int i1,i2,c,x,value;

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
			if (m_currently_mutated_y<height-1)
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
					c=random((*m_original_picture)[m_currently_mutated_y].size());
					temp.value=FindAtariColorIndex((*m_original_picture)[m_currently_mutated_y][c]);
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
			if (!(prog.instructions[i1].target>=E_HPOSP0 && prog.instructions[i1].target<=E_HPOSP3))
			{
				if (random(5)==0)
				{
					// random color from the line
					x=random((*m_original_picture)[m_currently_mutated_y].size());
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
					if (c>=free_cycles)
						c=free_cycles-1;
					x=screen_cycles[c].offset;
					if (x<0)
						x=0;
					x+=random(screen_cycles[c].length);
				}
				if (x<width)
				{
					i2=m_currently_mutated_y;
					while(random(2) && i2+1<(*m_original_picture).size())
						++i2;
					prog.instructions[i1].value=FindAtariColorIndex((*m_original_picture)[i2][x]);
					m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
					break;
				}
			}
			// no break
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

	for (y=0;y<height;++y)
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


		for (x=0;x<width;++x)
			m_picture[y][x]=black;
		for (int i=0;i<CYCLES_MAX;++i)
		{
			x=screen_cycles[i].offset;
			if (x>=0 && x<width)
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
	ExecuteRasterProgram(m_pic,cfg.dither);
	ShowLastCreatedPicture();
	SaveRasterProgram(string(cfg.output_file+".rp"));
	SavePMG(string(cfg.output_file+".pmg"));
	SaveScreenData  (string(cfg.output_file+".mic").c_str());
	SavePicture     (cfg.output_file);
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
	resize_rgb_picture(&m_created_picture,m_picture[0].size(),m_picture.size());
	resize_target_picture(&m_created_picture_targets,input_bitmap->w,input_bitmap->h);

	Init();

	map < double, raster_picture >::iterator m,_m;
	m_currently_mutated_y=0;

	textprintf_ex(screen, font, 0, 280, makecol(0xF0,0xF0,0xF0), 0, "Press 'S' to save. Press 'D' to save with dithering. '+', '-' dither level.");
	textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %u", evaluations);
	textprintf_ex(screen, font, 0, 290, makecol(0xF0,0xF0,0xF0), 0, "Dither: %.1f",cfg.dither_level);

	while(!key[KEY_ESC] && !user_closed_app)
	{
		for (m=m_solutions.begin(),_m=m_solutions.end();m!=_m;++m)
		{
			if (key[KEY_EQUALS] || key[KEY_PLUS_PAD])
			{
				if (cfg.dither_level<0.9)
					cfg.dither_level+=0.1;
				textprintf_ex(screen, font, 0, 290, makecol(0xF0,0xF0,0xF0), 0, "Dither: %.1f",cfg.dither_level);
				Wait(1);
			}
			else if (key[KEY_MINUS] || key[KEY_MINUS_PAD])
			{
				if (cfg.dither_level>0)
					cfg.dither_level-=0.1;
				textprintf_ex(screen, font, 0, 290, makecol(0xF0,0xF0,0xF0), 0, "Dither: %.1f",cfg.dither_level);
				Wait(1);
			}
			else if (key[KEY_S] || key[KEY_D])
			{
				if (key[KEY_D])
					cfg.dither=true;
				else
					cfg.dither=false;

				SaveBestSolution();
				// wait 2 seconds
				Wait(2);
				Message("Saved.               ");
			}
			double current_distance=m->first;
			raster_picture new_picture(m_original_picture->size());
			new_picture=m->second;
			
			m_pic=&new_picture;
//			TestRasterProgram(&new_picture); // !!!
			MutateRasterProgram(&new_picture);
			ExecuteRasterProgram(&new_picture,false);

			double result = EvaluateCreatedPicture();

			if (evaluations%300==0)
			{
				textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %u  LastBest: %u", evaluations,last_best_evaluation);
				textprintf_ex(screen, font, 0, 310, makecol(0xF0,0xF0,0xF0), 0, "Norm. Dist: %f", result/ (((double)width*(double)height)*(MAX_COLOR_DISTANCE/10000)));
			}
			// store this solution (<= to make results more diverse)
			if ( ((solutions==1 && result<=current_distance) || 
			   (solutions>1 && result<current_distance)) )
			{
				// show it only if mutation gives better picture
				if (result<current_distance)
				{
					ShowLastCreatedPicture();
					textprintf_ex(screen, font, 0, 300, makecol(0xF0,0xF0,0xF0), 0, "Evaluations: %u  LastBest: %u  Solutions: %d", evaluations,last_best_evaluation,(int) m_solutions.size());
					textprintf_ex(screen, font, 0, 310, makecol(0xF0,0xF0,0xF0), 0, "Norm. Dist: %f", result/ (((double)width*(double)height)*(MAX_COLOR_DISTANCE/10000)));
					ShowMutationStats();
				}
	
				// if mutated solution is better than the best one, then merge the picture with the best one
				if (result<m_solutions.begin()->first)
				{
					m_best_created_picture=m_created_picture;
				}
				AddSolution(result,new_picture);
				break;
			}
			if (result>=current_distance) // move to the prev line even if result is equal
				--m_currently_mutated_y; 

		}
		while (m_solutions.size()>solutions)
		{
			m=m_solutions.end();
			m--;
			m_solutions.erase(m);
		};
	}
}

void RastaConverter::ShowLastCreatedPicture()
{
	size_t x,y;
	// Draw new picture on the screen
	for (y=0;y<height;++y)
	{
		for (x=0;x<width;++x)
		{
			rgb atari_color=m_created_picture[y][x];
			int color=RGB2PIXEL(atari_color);
			putpixel(output_bitmap,x,y,color);
		}
	}
	blit(output_bitmap,screen,0,0,input_bitmap->w,0,output_bitmap->w,output_bitmap->h);
}

void RastaConverter::SavePMG(string name)
{
	size_t sprite,y,bit;
	unsigned char b;
	Message("Saving sprites (PMG)");

	FILE *fp=fopen(name.c_str(),"wt+");
	if (!fp)
		error("Error saving PMG handler");

	fprintf(fp,"; ------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; ------------------------- \n");

	if (cfg.border)
	{
		for (y=0;y<240;++y)
			for (bit=0;bit<8;++bit)
				sprites_memory[0][y][bit]=1;
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
				if (y>height)
					sprites_memory[sprite][y][bit]=0;

				b|=(sprites_memory[sprite][y][bit])<<(7-bit);
			}
			fprintf(fp," %02X",b);
			if (y%16==7)
				fprintf(fp,"\n\t.he");
		}
		fprintf(fp," 00 00 00 00 00 00 00 00\n");
	}
	fclose(fp);
}

void RastaConverter::SaveRasterProgram(string name)
{
	size_t y;
	if (m_solutions.empty())
		return;

	Message("Saving Raster Program");

	raster_picture &pic = m_solutions.begin()->second;

	FILE *fp=fopen(string(name+".ini").c_str(),"wt+");
	if (!fp)
		error("Error saving Raster Program");

	fprintf(fp,"; ------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; ------------------------- \n");

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

	fclose(fp);

	fp=fopen(name.c_str(),"wt+");
	if (!fp)
		error("Error saving DLI handler");

	fprintf(fp,"; ------------------------- \n");
	fprintf(fp,"; RastaConverter by Ilmenit v.%s\n",program_version);
	fprintf(fp,"; %u evaluations\n",evaluations);
	fprintf(fp,"; ------------------------- \n");

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
	fprintf(fp,"; ------------------------- \n");
	fclose(fp);
}


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

void close_button_procedure()
{
	user_closed_app=true;
}

int main(int argc, char *argv[])
{
	srand( (unsigned)time( NULL ) );

	//////////////////////////////////////////////////////////////////////////
	allegro_init(); // Initialize Allegro
	install_keyboard();
	set_close_button_callback(QuitFunc);
	FreeImage_Initialise(TRUE);
	screen_color_depth = desktop_color_depth();
	set_color_depth(screen_color_depth);
	set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640,480,0,0); // Change our graphics mode to 640x480
	set_display_switch_mode(SWITCH_BACKGROUND);
	set_window_close_hook(close_button_procedure);

	create_cycles_table();

	Configuration cfg(argc, argv);

	RastaConverter q(cfg);

	LoadAtariPalette(cfg.palette_file);
	q.LoadInputBitmap();

	q.ProcessInit();
	q.FindBestSolution();
	q.SaveBestSolution();
	return 0; // Exit with no errors
}

END_OF_MAIN() // This must be called right after the closing bracket of your MAIN function.
// It is Allegro specific.
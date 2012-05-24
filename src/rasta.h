#ifndef RASTA_H
#define RASTA_H

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
#include <assert.h>
#include "config.h"
#include <float.h>

using namespace std;

#define DISTANCE_MAX 0xffffffff
typedef long long distance_accum_t;
typedef unsigned int distance_t;
typedef distance_t (fn_rgb_distance)(const rgb &col1, const rgb &col2);
typedef fn_rgb_distance *f_rgb_distance;

struct MixingPlan;

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

union SRasterInstruction {
	struct {
		/*e_raster_instruction*/ unsigned short instruction;
		/*e_target*/ unsigned char target;
		unsigned char value;
	} loose;

	uint32_t packed;

	bool operator==(const SRasterInstruction& other) const
	{
		return packed == other.packed;
	}

	size_t hash() const
	{
		return packed;
	}
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

	const rgb& operator[](size_t i) const
	{
		return pixels[i];
	}

	size_t size() const
	{
		return pixels.size();
	}
};

class line_target {
private:
	vector < unsigned char > pixels; // target of the pixel f.e. COLBAK
public:
	void Resize(size_t i)
	{
		pixels.resize(i);
	}

	unsigned char& operator[](size_t i)
	{
		return pixels[i];
	}

	size_t size() const
	{
		return pixels.size();
	}
};

typedef vector<unsigned char> color_index_line;

// Tables of cycles

struct ScreenCycle {
	int offset; // position on the screen (can be <0 - previous line)
	int length; // length in pixels for 2 CPU cycles
};

#define CYCLE_MAP_SIZE (114 + 9)
#define CYCLES_MAX 114

struct raster_line {
	vector < SRasterInstruction > instructions;
	raster_line()
	{
		cycles=0;
	}

	void rehash()
	{
		unsigned h = 0;

		for(vector<SRasterInstruction>::const_iterator it = instructions.begin(), itEnd = instructions.end(); it != itEnd; ++it)
		{
			h += it->hash();

			h = (h >> 27) + (h << 5);
		}

		this->hash = h;
	}

	void swap(raster_line& other)
	{
		instructions.swap(other.instructions);
		std::swap(cycles, other.cycles);
		std::swap(hash, other.hash);
	}

	int cycles; // cache, to chech if we can add/remove new instructions
	unsigned hash;
};

struct raster_picture {
	unsigned char mem_regs_init[E_TARGET_MAX];
	vector < raster_line > raster_lines;
	raster_picture()
	{
	}

	raster_picture(size_t height)
	{
		raster_lines.resize(height);
	}
};

struct statistics_point {
	unsigned evaluations;
	unsigned seconds;
	double distance;
};

class RastaConverter {
private:
	FILE *out, *in;
	PALETTE palette;
	FIBITMAP *fbitmap; 

	// picture
	BITMAP *input_bitmap;	
	BITMAP *output_bitmap;
	BITMAP *destination_bitmap;

	vector < vector <unsigned char> > details_data;	

	vector < screen_line > m_picture; 
	vector<distance_t> m_picture_all_errors[128]; 
	int m_width,m_height; // picture size

	typedef vector<statistics_point> statistics_list;
	statistics_list m_statistics;

	// private functions
	void InitLocalStructure();
	void GeneratePictureErrorMap();


	vector < color_index_line > m_created_picture;
	vector < line_target > m_created_picture_targets;
	map < double, raster_picture > m_solutions;
	vector < vector < unsigned char > > m_possible_colors_for_each_line;
	vector < vector < rgb_error > > error_map;

	bool init_finished;
	void Init();
	void FindPossibleColors();

	void ClearErrorMap();
	void CreateEmptyRasterPicture(raster_picture *);
	void CreateSmartRasterPicture(raster_picture *);
	void CreateRandomRasterPicture(raster_picture *);
	void DiffuseError( int x, int y, double quant_error, double e_r,double e_g,double e_b);
	void KnollDithering();
	void OtherDithering();
	MixingPlan DeviseBestMixingPlan(rgb color);

	inline void ExecuteInstruction(const SRasterInstruction &instr, int x);
	inline int GetInstructionCycles(const SRasterInstruction &instr);

	distance_accum_t ExecuteRasterProgram(raster_picture *);

	void LoadDetailsMap();

	void SetSpriteBorders(raster_picture *);
	double EvaluateCreatedPicture(void);

	template<fn_rgb_distance& T_distance_function>
	distance_accum_t CalculateLineDistance(const screen_line &r, const screen_line &l);

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

	e_target FindClosestColorRegister(int index, int x,int y, bool &restart_line, distance_t& error);

	void SaveRasterProgram(string name);
	void SavePMG(string name);
	bool SaveScreenData(const char *filename);
	bool SavePicture(string filename, BITMAP *to_save);
	void SaveStatistics(const char *filename);

	void LoadRegInits(string name);
	void LoadRasterProgram(string name);
	void LoadPMG(string name);

	double NormalizeScore(double raw_score);

public:
	// configuration
	Configuration cfg;

	RastaConverter();

	void FindBestSolution();
	void SaveBestSolution();
	void ShowLastCreatedPicture();

	void PrepareDestinationPicture();
	void SetConfig(Configuration &c);
	bool ProcessInit();
	bool LoadInputBitmap();
	bool Resume1();
};

#endif

#ifndef RASTA_H
#define RASTA_H

#ifdef _MSC_VER
#pragma warning (disable: 4312)
#pragma warning (disable: 4996)
#endif

#include <math.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <string>
#include "FreeImage.h"
#include "CommandLineParser.h"
#include <assert.h>
#include "config.h"
#include <float.h>
#include "Distance.h"
#include "Program.h"
#include "Evaluator.h"

using namespace std;

struct MixingPlan;

// Tables of cycles

#define CYCLE_MAP_SIZE (114 + 9)

class RastaConverter {
private:
	FILE *out, *in;
	FIBITMAP *fbitmap; 

	// picture
	BITMAP *input_bitmap;	
	BITMAP *output_bitmap;
	BITMAP *destination_bitmap;

	vector < vector <unsigned char> > details_data;		

	vector < screen_line > m_picture; 
	vector<distance_t> m_picture_all_errors[128]; 
	const distance_t *m_picture_all_errors_array[128];
	int m_width,m_height; // picture size

	EvalGlobalState m_eval_gstate;

	vector<Evaluator> m_evaluators;

	// private functions
	void InitLocalStructure();
	void GeneratePictureErrorMap();

	vector < vector < rgb_error > > error_map;

	bool init_finished;
	void Init();
	void FindPossibleColors();
	void ClearErrorMap();
	void CreateEmptyRasterPicture(raster_picture *);
	void CreateLowColorRasterPicture(raster_picture *);
	void CreateSmartRasterPicture(raster_picture *);
	void CreateRandomRasterPicture(raster_picture *);
	void DiffuseError( int x, int y, double quant_error, double e_r,double e_g,double e_b);
	void KnollDithering();
	void OtherDithering();
	MixingPlan DeviseBestMixingPlan(rgb color);

	distance_accum_t ExecuteRasterProgram(raster_picture *);
	void OptimizeRasterProgram(raster_picture *);

	void LoadDetailsMap();

	double EvaluateCreatedPicture(void);

	template<fn_rgb_distance& T_distance_function>
	distance_accum_t CalculateLineDistance(const screen_line &r, const screen_line &l);

	void TestRasterProgram(raster_picture *pic);

	void ShowMutationStats();

	void LoadOnOffFile(const char *filename);
	void SaveRasterProgram(string name, raster_picture *pic);
	void SavePMG(string name);
	bool SaveScreenData(const char *filename);
	bool SavePicture(string filename, BITMAP *to_save);
	void SaveStatistics(const char *filename);
	void SaveLAHC(const char *filename);

	void LoadRegInits(string name);
	void LoadRasterProgram(string name);
	void LoadPMG(string name);
	void LoadLAHC(string name);

	double NormalizeScore(double raw_score);

	struct parallel_for_arg_t {
		int from;
		int to;
		void *this_ptr;
	};
	void KnollDitheringParallel(int from, int to);
	static void *KnollDitheringParallelHelper(void *arg);
	void ParallelFor(int from, int to, void *(*start_routine)(void*));

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
	void LoadAtariPalette();
	bool LoadInputBitmap();
	bool Resume();

	void ShowDestinationBitmap();
	void ShowDestinationLine(int y);
	void ShowInputBitmap();
};

#endif

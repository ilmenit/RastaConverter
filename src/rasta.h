#ifndef RASTA_H
#define RASTA_H

#ifdef _MSC_VER
#pragma warning (disable: 4312)
#pragma warning (disable: 4996)
#endif

#include <assert.h>
#include <float.h>
#include <math.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <algorithm>
#include <string>

#include "FreeImage.h"
#include "CommandLineParser.h"
#include "config.h"
#include "Distance.h"
#include "Program.h"
#include "Evaluator.h"

#ifdef NO_GUI
#include "RastaConsole.h"
#else
#include "RastaSDL.h"
#endif

using namespace std;

struct MixingPlan;

// Tables of cycles

#define CYCLE_MAP_SIZE (114 + 9)

class RastaConverter {
private:

#ifdef NO_GUI
	RastaConsole gui
#else;
	RastaSDL gui;
#endif

	FILE *out, *in;

	// picture
	FIBITMAP* input_bitmap;
	FIBITMAP* output_bitmap;
	FIBITMAP* destination_bitmap;

	vector < vector <unsigned char> > details_data;		

	vector < screen_line > m_picture; 
	vector<distance_t> m_picture_all_errors[128]; 
	const distance_t *m_picture_all_errors_array[128];
	int m_width,m_height; // picture size
	double m_rate = 0;

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
	bool SavePicture(const std::string& filename, FIBITMAP* to_save);
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
	bool GetInstructionFromString(const string& line, SRasterInstruction& instr);

public:
	// configuration
	Configuration cfg;

	RastaConverter();

	void MainLoop();
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

	void Message(std::string message);
	void Error(std::string e);
};

#endif

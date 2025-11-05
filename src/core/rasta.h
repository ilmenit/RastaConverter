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
#include <sys/timeb.h>
#include <mutex>
#include <atomic>
#include <memory>
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
	RastaConsole gui;
#else
	RastaSDL gui;
#endif

	FILE *out, *in;

	// picture
	FIBITMAP* input_bitmap;
	FIBITMAP* output_bitmap;
	FIBITMAP* destination_bitmap;
	// Dual-mode output buffers (allocated only when dual_mode is on)
	FIBITMAP* output_bitmap_A = nullptr;
	FIBITMAP* output_bitmap_B = nullptr;
	FIBITMAP* output_bitmap_blended = nullptr;

	vector < vector <unsigned char> > details_data;		

	vector < screen_line > m_picture; 
	vector < screen_line > m_picture_original; // original input before palette quantization
	vector<distance_t> m_picture_all_errors[128]; 
	const distance_t *m_picture_all_errors_array[128];
	int m_width,m_height; // picture size
	double m_rate = 0;
	std::chrono::time_point<std::chrono::steady_clock> m_previous_save_time;

	EvalGlobalState m_eval_gstate;
	std::mutex m_color_set_mutex;
	// Dual-mode state
	raster_picture m_best_pic_B; // best B program
	std::vector< std::vector<unsigned char> > m_created_picture_B; // lines of color indices for B
	std::vector< std::vector<unsigned char> > m_created_picture_targets_B; // target rows for B
	unsigned long long m_genA = 0, m_genB = 0; // local counters for A/B (deprecated; use m_eval_gstate m_dual_generation_*)
	bool m_dual_tables_ready = false; // pair tables ready
	float m_palette_y[128] = {0}, m_palette_u[128] = {0}, m_palette_v[128] = {0};
	std::vector<float> m_target_y, m_target_u, m_target_v; // per-pixel target YUV (float)
	// Input-based targets for post-bootstrap dual optimization
	std::vector<float> m_input_target_y, m_input_target_u, m_input_target_v; // per-pixel input YUV (float)
	std::vector<float> m_pair_Ysum, m_pair_Usum, m_pair_Vsum; // 128x128 tables (float)
	// Temporal diffs between pairs (absolute component deltas)
	std::vector<float> m_pair_Ydiff, m_pair_Udiff, m_pair_Vdiff; // 128x128 tables (float)
	// Quantized 8-bit variants for LUT-based dual distance
	std::vector<unsigned char> m_target_y8, m_target_u8, m_target_v8; // per-pixel target YUV (uint8)
	// Input-based 8-bit targets
	std::vector<unsigned char> m_input_target_y8, m_input_target_u8, m_input_target_v8; // per-pixel input YUV (uint8)
	std::vector<unsigned char> m_pair_Ysum8, m_pair_Usum8, m_pair_Vsum8; // 128x128 tables (uint8)
	std::vector<unsigned char> m_pair_Ydiff8, m_pair_Udiff8, m_pair_Vdiff8; // 128x128 tables (uint8)
	std::vector<unsigned char> m_pair_srgb; // 128x128x3 blended sRGB pairs (active blending mode)
	enum class DualDisplayMode { A, B, MIX };
	DualDisplayMode m_dual_display = DualDisplayMode::MIX;
	sprites_memory_t m_sprites_memory_B{}; // sprites for B

	// Knoll dithering progress (for live UI updates)
	std::unique_ptr<std::atomic<unsigned char>[]> m_knoll_line_ready; // size=m_height, 0=not ready, 1=ready
	std::vector<unsigned char> m_knoll_line_drawn; // 0 = not drawn, 1 = drawn

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
	void SaveOptimizerState(const char *filename);

	void LoadRegInits(string name);
	void LoadRasterProgram(string name);
	// Helpers to load A/B into appropriate members for dual resume
	bool LoadRasterProgramInto(raster_picture& dst, const std::string& rp_path, const std::string& ini_path);
	void LoadPMG(string name);
	void LoadOptimizerState(string name);

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

    // (removed) legacy dual acceptance helper – logic centralized in Evaluator::ApplyAcceptanceCore

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
	void reconfigureAcceptanceHistory();
	bool m_needs_history_reconfigure = false;

	void ShowDestinationBitmap();
	void ShowDestinationLine(int y);
	void ShowInputBitmap();
	void ShowDualBitmap();

	// Dual-mode helpers
	void PrecomputeDualTables();
	// Build input-based per-pixel YUV targets from original input
	void PrecomputeInputTargets();
	void MainLoopDual();
	void UpdateCreatedFromResults(const std::vector<const line_cache_result*>& results,
		std::vector< std::vector<unsigned char> >& out_created);
	void UpdateTargetsFromResults(const std::vector<const line_cache_result*>& results,
		std::vector< std::vector<unsigned char> >& out_targets);
	void ShowLastCreatedPictureDual();
	bool SaveScreenDataFromTargets(const char *filename, const std::vector< std::vector<unsigned char> >& targets);
	void SavePMGWithSprites(std::string name, const sprites_memory_t& sprites);

	void Message(std::string message);
	void Error(std::string e);
};

#endif

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
#include <chrono>
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
#include "DetailsMask.h"

#ifdef NO_GUI
#include "RastaConsole.h"
#else
#include "RastaSDL.h"
#endif

using namespace std;

struct MixingPlan;

// Clears process-global conversion state so a second run in the same process
// starts clean. Call before configuring each run.
void ResetProcessGlobalsForNewRun();

class RastaConverter {
private:

#ifdef NO_GUI
	RastaConsole gui;
#else
	RastaSDL gui;
#endif

	FILE *out, *in;

	// picture
	// Initialized here rather than left to chance: these used to be zeroed only
	// because the single RastaConverter was a global with static storage. A
	// heap-allocated instance (one per run) gets whatever was on the heap, and
	// ShowInputBitmap's "if (destination_bitmap)" guard happily passes garbage
	// straight to FreeImage.
	FIBITMAP* input_bitmap = nullptr;
	FIBITMAP* output_bitmap = nullptr;
	FIBITMAP* destination_bitmap = nullptr;
	// Dual-mode output buffers (allocated only when dual_mode is on)
	FIBITMAP* output_bitmap_A = nullptr;
	FIBITMAP* output_bitmap_B = nullptr;
	FIBITMAP* output_bitmap_blended = nullptr;

	DetailsMask details_mask;
	std::vector<double> details_line_priorities;
	std::vector<unsigned char> m_destination_indices;
	std::string m_saved_details_effective_hash;
	std::string m_saved_target_hash;
	std::string m_target_hash;
	bool m_mask_edited = false;
	bool m_destination_edited = false;
	bool m_editor_paused = false;
	bool m_editor_destination = false;
	bool m_mask_edited_since_save = false;
	unsigned m_snapshot_count = 0;
	int m_last_retarget_ms = 0;

	vector < screen_line > m_picture; 
	vector < screen_line > m_picture_original; // original input before palette quantization
	vector<distance_t> m_picture_all_errors[128]; 
	const distance_t *m_picture_all_errors_array[128];
	int m_width = 0, m_height = 0; // picture size
	double m_rate = 0;
	// Wall-clock start of the search, for the dashboard's elapsed readout.
	std::chrono::steady_clock::time_point m_run_started = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point m_last_save_time{};
	bool m_ever_saved = false;
	std::string m_last_message;
	std::chrono::time_point<std::chrono::steady_clock> m_previous_save_time;

	EvalGlobalState m_eval_gstate;
	// Save/report scoring must never borrow worker cache rows or instruction
	// identities. It owns a separate evaluator and global cache lock/state so an
	// active worker can clear its allocators without invalidating report data.
	EvalGlobalState m_reporting_eval_gstate;
	std::unique_ptr<Evaluator> m_reporting_evaluator;
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
	std::atomic<bool> m_knoll_should_stop{false}; // signal worker threads to stop early

	vector<Evaluator> m_evaluators;

	// private functions
	void InitLocalStructure();
	void GeneratePictureErrorMap();
	// One editor session: BeginEditorSession stops the workers at a safe point,
	// ApplyEditorSession commits pixels and parameters together and restarts
	// them, DiscardEditorSession just restarts them.
	void BeginEditorSession(bool destination);
	void ApplyEditorSession(const GuiEditorApply& request);
	void DiscardEditorSession();
	void PauseWorkers(std::unique_lock<std::mutex>& lock);
	void ResumeWorkers(std::unique_lock<std::mutex>& lock);
	bool ApplyMaskEditLocked(const GuiEditorApply& request);
	bool ApplyDestinationEditLocked(const GuiEditorApply& request);
	void RetargetLocked(bool full_rebuild);
	void SaveEditedMaskArtifact();
	bool SnapshotBeforeMaskEdit();
	void BranchCurrentRun();
	void SaveEditedTargetArtifact();
	void RenderCreatedPicture(raster_picture& picture);

	bool init_finished;
	void Init();
	void ApplyInternalStructuredInitializer();
	void ApplyInternalStructuredPass(const char* profile,
		const char* label, bool publish_result);
	void FindPossibleColors();
	void CreateEmptyRasterPicture(raster_picture *);
	void CreateLowColorRasterPicture(raster_picture *);
	void CreateSmartRasterPicture(raster_picture *);
	void CreateRandomRasterPicture(raster_picture *);
	bool KnollDithering(); // returns true if cancelled by user
	bool OtherDithering(); // returns true if cancelled by user
	MixingPlan DeviseBestMixingPlan(rgb color);
	// Apply input dithering to m_picture_original (for dual mode)
	void ApplyDualInputDithering();

	distance_accum_t ExecuteRasterProgram(raster_picture *);
	void OptimizeRasterProgram(raster_picture *);

	void LoadDetailsMap();

	double EvaluateCreatedPicture(void);

	template<fn_rgb_distance& T_distance_function>
	distance_accum_t CalculateLineDistance(const screen_line &r, const screen_line &l);

	void TestRasterProgram(raster_picture *pic);

	void ShowMutationStats();
	// Fills the live dashboard's snapshot from the current run state and hands
	// it to the frontend. Everything it reads already exists; nothing is added
	// to the hot path.
	void PublishLiveStats(bool preprocessing, bool finished);
	std::string BuildConfigRecap() const;

	void LoadOnOffFile(const char *filename);
	void SaveRasterProgram(string name, raster_picture *pic);
	void SavePMG(string name);
	bool SaveScreenData(const char *filename);
	bool SaveAntic4Data(const std::string& screenFilename,
		const std::string& fontFilename, const raster_picture& picture);
	bool SavePicture(const std::string& filename, FIBITMAP* to_save);
	void SaveStatistics(const char *filename);
	void SaveOptimizerState(const char *filename, const raster_picture* picture = nullptr);

	void LoadRegInits(string name);
	void LoadRasterProgram(string name);
	// Helpers to load A/B into appropriate members for dual resume
	bool LoadRasterProgramInto(raster_picture& dst, const std::string& rp_path, const std::string& ini_path);
	void LoadPMG(string name);
	void LoadOptimizerState(string name);

	double NormalizeScore(double raw_score);
	double UnweightedSourceOklabMean(raster_picture* pic);

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
	void ApplyInternalStructuredFinalizer();
	void SaveBestSolution();
	void ShowLastCreatedPicture();

	bool PrepareDestinationPicture(); // returns true if cancelled by user
	void SetConfig(Configuration &c);
	// True when the run was ended with Abort rather than Stop and save.
	bool AbortedWithoutSave() const { return gui.AbortRequested(); }
	bool ProcessInit();
	void LoadAtariPalette();
	bool LoadInputBitmap();
	bool Resume();
	bool RunStructuredFixtureScreen(const std::string& csv_path,
		const std::string& profile_label);
	bool RunPhase7RetainedWindowScreen(const std::string& csv_path,
		const std::string& profile_label);
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

#ifndef EVALUATOR_H
#define EVALUATOR_H

// Retired line-order tracking can be restored for regression diagnostics with
// -DRASTA_TRACK_LINE_LRU=1. Result eviction is generation-wide and does not
// consume this ordering in production.
#ifndef RASTA_TRACK_LINE_LRU
#define RASTA_TRACK_LINE_LRU 0
#endif

#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>

#include "Distance.h"
#include "VisualObjective.h"

struct StructuredBeamOptions;
struct StructuredPairedWindowResult;
struct StructuredPairedWindowProblem;
#include "Program.h"
#include "LinearAllocator.h"
#include "LineCache.h"
#include "OptimizerState.h"
#include <atomic>
#include <cfloat>
#include <memory>
#if RASTA_TRACK_LINE_LRU
#include <deque>
#include <unordered_set>
#endif

typedef std::vector<unsigned char> color_index_line;
typedef std::vector<unsigned char> line_target;		// target of the pixel f.e. COLBAK

struct Configuration;

class RasterMutationTransaction
{
public:
	void Begin(raster_picture& picture, unsigned long long allocatorEpoch);
	void SaveMemory();
	void SaveLine(int y);
	void SaveMutationNeighborhood(int y);
	void Restore(unsigned long long allocatorEpoch);
	unsigned long long SavedLineCount() const;

private:
	raster_picture* m_picture = nullptr;
	std::vector<raster_line> m_snapshots;
	std::vector<unsigned char> m_saved;
	std::vector<unsigned> m_touched;
	unsigned char m_memory_snapshot[E_TARGET_MAX]{};
	bool m_memory_saved = false;
	unsigned long long m_allocator_epoch = 0;
};

struct OnOffMap
{
	bool on_off[240][E_TARGET_MAX]; // global for speed-up
};

struct statistics_point {
	unsigned long long evaluations;
	unsigned seconds;
	double distance;
};

typedef std::vector<statistics_point> statistics_list;

struct EvalGlobalState
{
	struct PublishedBestSnapshot
	{
		raster_picture picture;
		double cost = DBL_MAX;
		unsigned long long version = 0;
	};
	std::vector < std::vector < unsigned char > > m_possible_colors_for_each_line;

	std::mutex m_mutex;
	std::condition_variable m_condvar_update;

	bool m_update_tick;
	bool m_update_autosave;
	bool m_update_improvement;
	bool m_update_initialized;
	std::atomic<bool> m_initialized;
	std::atomic<bool> m_finished;
	// Cooperative stop-the-world barrier for live objective edits. Workers
	// acknowledge only between complete evaluations, never while an error row
	// or cache entry is borrowed.
	std::atomic<bool> m_pause_requested{false};
	int m_threads_paused = 0;
	std::atomic<unsigned long long> m_objective_generation{0};

	int m_threads_active;

	unsigned long long m_save_period;
	unsigned long long m_max_evals;
	std::atomic<unsigned long long> m_evaluations;
	std::atomic<unsigned long long> m_last_best_evaluation;

	raster_picture m_best_pic;
	std::atomic<double> m_best_result;

	sprites_memory_t m_sprites_memory;

	std::vector < color_index_line > m_created_picture;
	std::vector < line_target > m_created_picture_targets;

	int m_mutation_stats[E_MUTATION_MAX];

	// Number of threads configured (add this)
	int m_thread_count;
	// Mutex for coordinating cache clearing
	std::mutex m_cache_mutex;

	time_t m_time_start;

	statistics_list m_statistics;

	// DLAS specific fields
	double m_cost_max;                    // Maximum cost threshold
	int m_N;                              // Count of cost_max entries
	std::vector<double> m_previous_results; // History list for DLAS
	size_t m_previous_results_index;      // Current index in history
	double m_current_cost;                // Current accepted cost

	// Add tracking of previous costs per thread for DLAS
	std::vector<double> m_thread_previous_costs;

	// REMOVED: Old expensive shared_ptr snapshots - replaced by efficient fixed frame system

	// Dual-mode generation counters for cache invalidation across threads
	std::atomic<unsigned long long> m_dual_generation_A{0};
	std::atomic<unsigned long long> m_dual_generation_B{0};

	// Dual-mode stage coordination atomics (for high-performance alternation)
	std::atomic<bool> m_dual_stage_focus_B{false}; // true = focus on B, false = focus on A
	std::atomic<unsigned long long> m_dual_stage_counter{0}; // evaluations within current stage

	// Dual-mode phases for clearer UI: bootstrap A, bootstrap B, alternating
	enum DualPhase { DUAL_PHASE_NONE=0, DUAL_PHASE_BOOTSTRAP_A=1, DUAL_PHASE_BOOTSTRAP_B=2, DUAL_PHASE_ALTERNATING=3 };
	std::atomic<DualPhase> m_dual_phase{DUAL_PHASE_NONE};
	// True when B bootstrap is via copy-from-A; false when generated randomly
	std::atomic<bool> m_dual_bootstrap_b_copied{false};

	// SIMPLE: Fixed frame snapshots - updated only on phase switches, not improvements
	std::vector<std::vector<unsigned char>> m_dual_fixed_frame_A; // Fixed A pixel data  
	std::vector<std::vector<unsigned char>> m_dual_fixed_frame_B; // Fixed B pixel data
	// Double-buffered pointer arrays to fixed frame rows to avoid copying and races
	std::vector<const unsigned char*> m_dual_fixed_rows_buf[2];   // [2][height]
	std::atomic<int> m_dual_fixed_rows_active_index{0};           // which buffer readers see
	std::atomic<bool> m_dual_fixed_frame_is_A{false};             // true = A is fixed, false = B is fixed
	std::mutex m_dual_fixed_frame_mutex;                          // Protects fixed frame updates

	// Versioning for best-state updates to avoid per-iteration locks
	std::atomic<unsigned long long> m_best_state_version{0};
	std::shared_ptr<const PublishedBestSnapshot> m_best_snapshot;

	// Island-optimizer diagnostics. The historical m_single_* names are kept
	// for output compatibility; dual-frame workers also publish these counters.
	// Worker-local counters are aggregated at shutdown where practical.
	std::atomic<unsigned long long> m_single_accepted{0};
	std::atomic<unsigned long long> m_single_global_improvements{0};
	std::atomic<unsigned long long> m_single_migrations{0};
	std::atomic<unsigned long long> m_single_state_lock_samples{0};
	std::atomic<unsigned long long> m_single_state_lock_wait_ns{0};
	std::atomic<unsigned long long> m_single_state_lock_hold_ns{0};
	std::atomic<unsigned long long> m_single_copy_samples{0};
	std::atomic<unsigned long long> m_single_copy_ns{0};
	std::atomic<unsigned long long> m_publication_copy_events{0};
	std::atomic<unsigned long long> m_publication_copy_ns{0};
	std::atomic<unsigned long long> m_migration_copy_events{0};
	std::atomic<unsigned long long> m_migration_copy_ns{0};
	std::atomic<unsigned long long> m_migration_lines_copied{0};
	std::atomic<unsigned long long> m_migration_lines_reused{0};
	std::atomic<unsigned long long> m_single_cache_partial_clears{0};
	std::atomic<unsigned long long> m_single_cache_full_clears{0};
	std::atomic<unsigned long long> m_single_candidate_full_copies{0};
	std::atomic<unsigned long long> m_single_undo_candidates{0};
	std::atomic<unsigned long long> m_single_undo_line_snapshots{0};
	std::atomic<unsigned long long> m_single_undo_restores{0};
	std::atomic<unsigned long long> m_cache_lookups{0};
	std::atomic<unsigned long long> m_cache_hits{0};
	std::atomic<unsigned long long> m_cache_misses{0};
	std::atomic<unsigned long long> m_cache_lookup_probes{0};
	std::atomic<unsigned long long> m_cache_max_lookup_probes{0};
	std::atomic<unsigned long long> m_cache_inserts{0};
	std::atomic<unsigned long long> m_cache_hash_blocks{0};
	std::atomic<unsigned long long> m_cache_entry_bytes{0};
	std::atomic<unsigned long long> m_cache_hash_block_bytes{0};
	std::atomic<unsigned long long> m_cache_color_row_bytes{0};
	std::atomic<unsigned long long> m_cache_target_row_bytes{0};
	std::atomic<unsigned long long> m_insn_cache_hash_block_bytes{0};
	std::atomic<unsigned long long> m_insn_cache_data_bytes{0};
	std::atomic<unsigned long long> m_cache_evaluations{0};
	std::atomic<unsigned long long> m_cache_recomputed_lines{0};
	std::atomic<unsigned long long> m_cache_max_recomputed_lines{0};
	std::atomic<unsigned long long> m_cache_propagation_span{0};
	std::atomic<unsigned long long> m_cache_max_propagation_span{0};
	std::atomic<unsigned long long> m_cache_pmg_restarts{0};
	std::atomic<unsigned long long> m_lru_updates{0};
	std::atomic<unsigned long long> m_lru_search_steps{0};
	std::vector<unsigned long long> m_cache_hits_by_line;
	std::vector<unsigned long long> m_cache_misses_by_line;
	std::atomic<unsigned long long> m_mutation_attempted[E_MUTATION_MAX]{};
	std::atomic<unsigned long long> m_mutation_applied[E_MUTATION_MAX]{};
	std::atomic<unsigned long long> m_mutation_accepted[E_MUTATION_MAX]{};
	std::atomic<unsigned long long> m_mutation_improving[E_MUTATION_MAX]{};
	std::atomic<unsigned long long> m_improvement_events{0};
	std::atomic<double> m_improvement_total{0.0};
	std::atomic<double> m_improvement_max{0.0};
	std::atomic<double> m_mutation_improvement_credit_total[E_MUTATION_MAX]{};
	std::atomic<double> m_mutation_improvement_credit_max[E_MUTATION_MAX]{};

	// Optimizer selector (DLAS, LAHC, or Legacy)
	enum Optimizer { OPT_DLAS, OPT_LAHC, OPT_LEGACY };
	Optimizer m_optimizer = OPT_LAHC;

	// Aggressive search trigger threshold (0 = never)
	unsigned long long m_unstuck_after = 1000000ULL;
	// Normalized drift per evaluation added to acceptance thresholds when stuck
	double m_unstuck_drift_norm = 0.0;
	// Current normalized drift applied (for UI/reporting)
	std::atomic<double> m_current_norm_drift{0.0};


	EvalGlobalState();
	~EvalGlobalState();
};

class Evaluator
{
public:
	struct StructuredWindowComparison
	{
		bool feasible = false;
		bool accepted = false;
		distance_accum_t baseline_score = 0;
		distance_accum_t structured_score = 0;
		distance_accum_t baseline_source_oklab = 0;
		distance_accum_t structured_source_oklab = 0;
	};
	struct DualStructuredWindowComparison
	{
		bool feasible = false;
		bool legal_a = false;
		bool legal_b = false;
		DualFrameScore baseline;
		DualFrameScore structured;
	};

	Evaluator();

	void Init(unsigned width, unsigned height, const distance_t* const* errmap,
		const screen_line* picture, const OnOffMap* onoff, EvalGlobalState* gstate,
		int solutions, unsigned long long randseed, size_t cache_size, int thread_id=0,
		const screen_line* scoring_picture=nullptr,
		const std::vector<double>* allocation_line_weights=nullptr,
		unsigned allocation_global_period=5);

	void Start();

	void Run();

	e_target FindClosestColorRegister(sprites_row_memory_t& spriterow, int index, int x,int y, bool &restart_line, distance_t& error);
	e_target FindClosestColorRegisterDual(sprites_row_memory_t& spriterow,
		const unsigned char* other_row, unsigned picture_row_index, int x,
		bool& restart_line, distance_t& error);
	void TurnOffRegisters(raster_picture *pic);
	distance_accum_t ExecuteRasterProgram(raster_picture *pic, const line_cache_result **results);

	template<fn_rgb_distance& T_distance_function>
	distance_accum_t CalculateLineDistance(const screen_line &r, const screen_line &l);

	//inline void ExecuteInstruction(const SRasterInstruction &instr, int x);
	inline void ExecuteInstruction(const SRasterInstruction &instr, int sprite_check_x, sprites_row_memory_t &spriterow, distance_accum_t &total_line_error);

	void MutateRasterProgram(raster_picture *pic, RasterMutationTransaction* transaction = nullptr);
	void BeginMutationTransaction(raster_picture& pic, RasterMutationTransaction& transaction);
	void RestoreMutationTransaction(RasterMutationTransaction& transaction);
	void MutateLine(raster_line &, raster_picture &pic);
	void MutateOnce(raster_line &, raster_picture &pic);

	int Random(int range);

	// Ensure a picture's instruction sequences are cached in this evaluator's cache
	void RecachePicture(raster_picture* pic, bool force = false);

	// Access sprites memory for saving exports (read-only)
	const sprites_memory_t& GetSpritesMemory() const { return m_sprites_memory; }
	const std::vector<color_index_line>& GetCreatedPicture() const { return m_created_picture; }
	const std::vector<line_target>& GetCreatedPictureTargets() const { return m_created_picture_targets; }

	// --- Dual-mode evaluation (YUV-only blended distance) ---
	// Evaluate picture 'pic' selecting registers against blended(A,B) objective using
	// other_rows[y] color index row as the fixed opposite frame.
	// other_rows must have size m_height; entries may be nullptr (treated as zeros).
	// Pair tables and target YUV pointers must be set via SetDualTables before calling.
	distance_accum_t ExecuteRasterProgramDual(raster_picture* pic,
		const line_cache_result** results_array,
		const std::vector<const unsigned char*>& other_rows,
		bool mutateB);

	void SetDualTables(const float* paletteY, const float* paletteU, const float* paletteV,
		const float* pairYsum, const float* pairUsum, const float* pairVsum,
		const float* pairYdiff, const float* pairUdiff, const float* pairVdiff,
		const float* targetY, const float* targetU, const float* targetV);

	// Optional: set 8-bit quantized tables for accelerated dual distance
	void SetDualTables8(
		const unsigned char* pairYsum8,
		const unsigned char* pairUsum8,
		const unsigned char* pairVsum8,
		const unsigned char* pairYdiff8,
		const unsigned char* pairUdiff8,
		const unsigned char* pairVdiff8,
		const unsigned char* targetY8,
		const unsigned char* targetU8,
		const unsigned char* targetV8
	);

	// Configure temporal penalty weights
	void SetDualTemporalWeights(float luma, float chroma);

	// Flush this evaluator's current mutation counters into the shared
	// global-best contribution stats. Intended to be called only on
	// genuine improvements to minimize overhead.
	void FlushMutationStatsToGlobal();

	// Shared acceptance core (LAHC/DLAS + /s history) used by single and dual modes.
	// Assumes caller holds m_gstate->m_mutex and has incremented m_evaluations.
	struct AcceptanceOutcome {
		bool accepted;
		bool improved;
		double previousCost;
	};
	// Credit the operators actually applied to the current candidate only
	// after its evaluated acceptance outcome is known.
	void RecordMutationOutcome(const AcceptanceOutcome& outcome, double result);
	// Publish cumulative per-worker mutation diagnostics without double counting.
	void FlushMutationDiagnosticsToGlobal();
	void FlushCacheDiagnosticsToGlobal();
	// Return the current absolute acceptance drift for a worker-local optimizer
	// state. The normalized value remains published for UI/reporting.
	double CalculateAcceptanceDrift();
	AcceptanceOutcome ApplyAcceptanceCore(double result, bool force_best = false, 
		const raster_picture* new_picture = nullptr, const line_cache_result** line_results = nullptr);

	// Thin wrappers for clarity (no extra runtime cost expected)
	distance_accum_t EvaluateSingle(raster_picture* pic, const line_cache_result** line_results);
	distance_accum_t EvaluateUnweightedSource(raster_picture* pic);
	StructuredWindowComparison CompareStructuredWindow(
		const raster_picture& baseline,
		size_t first_line,
		const std::vector<raster_line>& structured_lines);
	StructuredWindowComparison CompareStructuredSourceWindow(
		const raster_picture& baseline,
		size_t first_line,
		size_t line_count,
		size_t alternate_count,
		const StructuredBeamOptions& options);
	StructuredWindowComparison ApplyStructuredSourceWindowIfBetter(
		raster_picture& baseline,
		size_t first_line,
		size_t line_count,
		size_t alternate_count,
		const StructuredBeamOptions& options,
		bool require_source_oklab_improvement = false);
	DualStructuredWindowComparison CompareDualStructuredWindow(
		const raster_picture& baselineA,
		const raster_picture& baselineB,
		size_t first_line,
		const StructuredPairedWindowResult& window);
	bool PopulateDualStructuredWindowCosts(
		const raster_picture& baselineA,
		const raster_picture& baselineB,
		size_t first_line,
		StructuredPairedWindowProblem& problem,
		const StructuredBeamOptions& options);
	inline distance_accum_t EvaluateDual(raster_picture* pic, const line_cache_result** line_results, const std::vector<const unsigned char*>& other_rows, bool mutateB) {
		return ExecuteRasterProgramDual(pic, line_results, other_rows, mutateB);
	}

	// Provide other-frame rows for dual-aware mutations (non-owning, valid during mutation call only)
	inline void SetDualMutationOtherRows(const std::vector<const unsigned char*>& rows) { m_dual_mutation_other_rows = &rows; }

	// Sync thread-local best with global best (for post-reseed alignment in dual mode)
	void SyncLocalBestToGlobal();

	// Clear all caches (for phase transitions, e.g., bootstrap -> alternating in dual mode)
	void ClearAllCaches();
	// The opposite member of a worker-local dual pair changed through migration.
	// Its cache keys do not encode the opposite pixel row, so invalidate them.
	void InvalidateDualCache();

private:
	int m_thread_id;
	// The retired ordering implementation remains build-selectable for regression
	// diagnosis, but production calls compile to no-ops.
#if RASTA_TRACK_LINE_LRU
	std::deque<int> m_lru_lines;
	std::unordered_set<int> m_lru_set;
	void UpdateLRU(int line_index);
	void ClearLineActivity();
#else
	inline void UpdateLRU(int) {}
	inline void ClearLineActivity() {}
#endif
	void ClearLineCacheGeneration();
	void RecordCacheEvaluation(unsigned recomputedLines, int firstMissLine, int lastMissLine);

	unsigned long long m_mutation_accepted_count[E_MUTATION_MAX];
	unsigned long long m_mutation_attempt_count[E_MUTATION_MAX];
	unsigned long long m_mutation_applied_count[E_MUTATION_MAX];
	unsigned long long m_mutation_improving_count[E_MUTATION_MAX];
	unsigned long long m_improvement_event_count = 0;
	double m_improvement_total = 0.0;
	double m_improvement_max = 0.0;
	double m_mutation_improvement_credit_total[E_MUTATION_MAX];
	double m_mutation_improvement_credit_max[E_MUTATION_MAX];
	// Preserve the established selector's fallback-chain feasibility policy.
	// These are deliberately separate from evaluated outcome diagnostics.
	unsigned long long m_selector_attempt_count[E_MUTATION_MAX];
	unsigned long long m_selector_applied_count[E_MUTATION_MAX];
	unsigned long long m_mutation_diag_flushed_attempted[E_MUTATION_MAX];
	unsigned long long m_mutation_diag_flushed_applied[E_MUTATION_MAX];
	unsigned long long m_mutation_diag_flushed_accepted[E_MUTATION_MAX];
	unsigned long long m_mutation_diag_flushed_improving[E_MUTATION_MAX];
	unsigned long long m_improvement_events_flushed = 0;
	double m_improvement_total_flushed = 0.0;
	double m_mutation_improvement_credit_total_flushed[E_MUTATION_MAX];
	int SelectMutation(); 

	// Cached selection weights to reduce per-call FP work
	double m_cached_weights[E_MUTATION_MAX] = {0};
	double m_cached_total_weight = 0.0;
	unsigned long long m_weights_valid_until_eval = 0ULL; // recompute after TTL or when stuck changes
	static constexpr unsigned long long k_weights_ttl_evals = 128ULL;
	bool m_last_dual_ok = false; // track dual availability changes for weight cache


	void CaptureRegisterState(register_state& rs) const;
	void ApplyRegisterState(const register_state& rs);
	AcceptanceOutcome ApplyIslandAcceptance(double result, OptimizerState& state, double drift);

	void StoreLineRegs();
	void RestoreLineRegs();
	void ResetSpriteShiftStartArray();
	
	void StartSpriteShift(int mem_reg);

	unsigned m_width;
	unsigned m_height;
	const distance_t *const *m_picture_all_errors;
	bool m_use_dual_neon = false;
	const screen_line *m_picture;
	const screen_line *m_scoring_picture = nullptr;
	// Kept for the source-referenced OKLab readouts (EvaluateUnweightedSource,
	// CompareStructuredWindow). It is no longer part of scoring: the objectives
	// that added a full-frame term to every evaluation are gone.
	DisplayFilteredObjective m_visual_objective;
	int m_currently_mutated_y;
	int SelectAllocatedLine(int first, int last);
	std::vector<double> m_allocation_line_weights;
	unsigned m_allocation_global_period = 5;
	unsigned long long m_primary_mutation_count = 0;
	int m_solutions;
	size_t m_cache_size;
	static constexpr size_t k_instruction_cache_budget_divisor = 8;

	unsigned long long m_randseed;
	unsigned long long m_cache_partial_clears = 0;
	unsigned long long m_cache_full_clears = 0;
	unsigned long long m_allocator_epoch = 0;
	unsigned long long m_local_cache_lookups = 0;
	unsigned long long m_local_cache_hits = 0;
	unsigned long long m_local_cache_misses = 0;
	unsigned long long m_local_cache_lookup_probes = 0;
	unsigned long long m_local_cache_max_lookup_probes = 0;
	unsigned long long m_local_cache_inserts = 0;
	unsigned long long m_local_cache_hash_blocks = 0;
	unsigned long long m_local_cache_evaluations = 0;
	unsigned long long m_local_cache_recomputed_lines = 0;
	unsigned long long m_local_cache_max_recomputed_lines = 0;
	unsigned long long m_local_cache_propagation_span = 0;
	unsigned long long m_local_cache_max_propagation_span = 0;
	unsigned long long m_local_cache_pmg_restarts = 0;
	unsigned long long m_local_lru_updates = 0;
	unsigned long long m_local_lru_search_steps = 0;
	std::vector<unsigned long long> m_local_cache_hits_by_line;
	std::vector<unsigned long long> m_local_cache_misses_by_line;

	raster_picture m_best_pic;
	double m_best_result;

	// Instruction identities outlive ordinary result-generation eviction. The
	// two arenas share one resident-byte budget and cumulative attribution.
	linear_allocator::statistics m_cache_allocator_stats;
	linear_allocator m_insn_allocator{linear_allocator::BLOCK_SIZE, &m_cache_allocator_stats};
	linear_allocator m_line_allocator{linear_allocator::BLOCK_SIZE, &m_cache_allocator_stats};

	std::vector<line_cache> m_line_caches;
	// Dual-mode dedicated caches (separate from single-frame caches)
	std::vector<line_cache> m_line_caches_dual;
	// Dual-mode: generation snapshot of other frame for cache invalidation
	unsigned long long m_dual_gen_other_snapshot = 0ULL;
	unsigned long long m_dual_last_other_generation = 0ULL;

	unsigned char m_reg_a, m_reg_x, m_reg_y;
	unsigned char m_mem_regs[E_TARGET_MAX+1]; // +1 for HITCLR

	register_state m_old_reg_state;

	unsigned char m_sprite_shift_regs[4];
	unsigned char m_sprite_shift_emitted[4];
	unsigned char m_sprite_shift_start_array[256];

	insn_sequence_cache m_insn_seq_cache;

	// we limit PMG memory to visible 240 bytes
	sprites_memory_t m_sprites_memory;

	std::vector < color_index_line > m_created_picture;
	std::vector < line_target > m_created_picture_targets;

	int m_current_mutations[E_MUTATION_MAX];

	EvalGlobalState *m_gstate;

	const OnOffMap *m_onoff;

	// Dual-mode pointers (non-owning) set by SetDualTables
	const float* m_dual_paletteY = nullptr;
	const float* m_dual_paletteU = nullptr;
	const float* m_dual_paletteV = nullptr;
	const float* m_dual_pairYsum = nullptr;
	const float* m_dual_pairUsum = nullptr;
	const float* m_dual_pairVsum = nullptr;
	const float* m_dual_pairYdiff = nullptr;
	const float* m_dual_pairUdiff = nullptr;
	const float* m_dual_pairVdiff = nullptr;
	const float* m_dual_targetY = nullptr;
	const float* m_dual_targetU = nullptr;
	const float* m_dual_targetV = nullptr;

	// Optional LUTs for 8-bit dual distance (accelerated path)
	const unsigned char* m_dual_pairYsum8 = nullptr;
	const unsigned char* m_dual_pairUsum8 = nullptr;
	const unsigned char* m_dual_pairVsum8 = nullptr;
	const unsigned char* m_dual_pairYdiff8 = nullptr;
	const unsigned char* m_dual_pairUdiff8 = nullptr;
	const unsigned char* m_dual_pairVdiff8 = nullptr;
	const unsigned char* m_dual_targetY8 = nullptr;
	const unsigned char* m_dual_targetU8 = nullptr;
	const unsigned char* m_dual_targetV8 = nullptr;
	unsigned short m_sq_lut[256]; // squared difference LUT (0..255)
	std::vector<distance_t> m_dual_temporal_penalty8;
	float m_dual_lambda_luma = 1.0f;
	float m_dual_lambda_chroma = 0.25f;
	void RebuildDualTemporalPenalty8();

	// Precomputed scale to convert normalized drift to raw distance units
	double m_drift_scale = 0.0;

	// Pointer to the other frame rows for dual-aware mutation (lifetime: around MutateRasterProgram call)
	const std::vector<const unsigned char*>* m_dual_mutation_other_rows = nullptr;

	// Cached stuck flag with TTL to avoid recomputing on every call outside acceptance
	bool m_cached_stuck = false;
	unsigned long long m_stuck_valid_until_eval = 0ULL; // recompute after TTL
	static constexpr unsigned long long k_stuck_ttl_evals = 1024ULL;
};

#endif

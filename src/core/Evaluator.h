#ifndef EVALUATOR_H
#define EVALUATOR_H

#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>

#include "Distance.h"
#include "Program.h"
#include "LinearAllocator.h"
#include "LineCache.h"
#include <deque>
#include <unordered_set>
#include <memory>
#include <atomic>

typedef std::vector<unsigned char> color_index_line;
typedef std::vector<unsigned char> line_target;		// target of the pixel f.e. COLBAK

struct Configuration;

struct OnOffMap
{
	bool on_off[240][E_TARGET_MAX]; // global for speed-up
};

struct statistics_point {
	unsigned evaluations;
	unsigned seconds;
	double distance;
};

typedef std::vector<statistics_point> statistics_list;

struct EvalGlobalState
{
	std::vector < std::vector < unsigned char > > m_possible_colors_for_each_line;

	std::mutex m_mutex;
	std::condition_variable m_condvar_update;

	bool m_update_tick;
	bool m_update_autosave;
	bool m_update_improvement;
	bool m_update_initialized;
	bool m_initialized;
	bool m_finished;

	int m_threads_active;

	unsigned long long m_save_period;
	unsigned long long m_max_evals;
	unsigned long long m_evaluations;
	unsigned long long m_last_best_evaluation;

	raster_picture m_best_pic;
	double m_best_result;

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

	// Optimizer selector (DLAS or LAHC)
	enum Optimizer { OPT_DLAS, OPT_LAHC };
	Optimizer m_optimizer = OPT_LAHC;


	EvalGlobalState();
	~EvalGlobalState();
};

class Evaluator
{
public:
	Evaluator();

	void Init(unsigned width, unsigned height, const distance_t* const* errmap, const screen_line* picture, const OnOffMap* onoff, EvalGlobalState* gstate, int solutions, unsigned long long randseed, size_t cache_size, int thread_id=0);

	void Start();

	void Run();

	e_target FindClosestColorRegister(sprites_row_memory_t& spriterow, int index, int x,int y, bool &restart_line, distance_t& error);
	void TurnOffRegisters(raster_picture *pic);
	distance_accum_t ExecuteRasterProgram(raster_picture *pic, const line_cache_result **results);

	template<fn_rgb_distance& T_distance_function>
	distance_accum_t CalculateLineDistance(const screen_line &r, const screen_line &l);

	//inline void ExecuteInstruction(const SRasterInstruction &instr, int x);
	inline void ExecuteInstruction(const SRasterInstruction &instr, int sprite_check_x, sprites_row_memory_t &spriterow, distance_accum_t &total_line_error);

	void MutateRasterProgram(raster_picture *pic);
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
	void SetDualTemporalWeights(float luma, float chroma) { m_dual_lambda_luma = luma; m_dual_lambda_chroma = chroma; }

	// Flush this evaluator's current mutation counters into the shared
	// global stats and clear the local counters. Intended to be called
	// only on accepted improvements to minimize overhead.
	void FlushMutationStatsToGlobal();

private:
	int m_thread_id;
	// LRU tracking
	std::deque<int> m_lru_lines; // Queue of lines ordered by recent use
	std::unordered_set<int> m_lru_set; // For fast lookups
	void UpdateLRU(int line_index); // Move a line to "most recently used" position

	int m_mutation_success_count[E_MUTATION_MAX];
	int m_mutation_attempt_count[E_MUTATION_MAX];
	int SelectMutation(); 

	void BatchMutateLine(raster_line& prog, raster_picture& pic, int count);

	void CaptureRegisterState(register_state& rs) const;
	void ApplyRegisterState(const register_state& rs);

	void StoreLineRegs();
	void RestoreLineRegs();
	void ResetSpriteShiftStartArray();
	
	void StartSpriteShift(int mem_reg);

	unsigned m_width;
	unsigned m_height;
	const distance_t *const *m_picture_all_errors;
	const screen_line *m_picture;
	int m_currently_mutated_y;
	int m_solutions;
	size_t m_cache_size;

	unsigned long long m_randseed;

	raster_picture m_best_pic;
	double m_best_result;

	linear_allocator m_linear_allocator;

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
	float m_dual_lambda_luma = 1.0f;
	float m_dual_lambda_chroma = 0.25f;
};

#endif

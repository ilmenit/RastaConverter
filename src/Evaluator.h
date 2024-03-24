#ifndef EVALUATOR_H
#define EVALUATOR_H

#include <vector>
#include <map>
#include <pthread.h>

#include "Distance.h"
#include "Program.h"
#include "LinearAllocator.h"
#include "LineCache.h"

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

	pthread_mutex_t m_mutex;
	pthread_cond_t m_condvar_update;

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

	std::vector < double > m_previous_results; // for Late Acceptance Hill Climbing
	size_t m_previous_results_index; // for Late Acceptance Hill Climbing

	sprites_memory_t m_sprites_memory;

	std::vector < color_index_line > m_created_picture;
	std::vector < line_target > m_created_picture_targets;

	int m_mutation_stats[E_MUTATION_MAX];

	time_t m_time_start;

	statistics_list m_statistics;

	EvalGlobalState();
	~EvalGlobalState();
};

class Evaluator
{
public:
	Evaluator();

	void Init(unsigned width, unsigned height, const distance_t *const *errmap, const screen_line *picture, const OnOffMap *onoff, EvalGlobalState *gstate, int solutions, unsigned long long randseed);

	void Start();

	static void *RunStatic(void *p);
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

private:
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

	unsigned long long m_randseed;

	raster_picture m_best_pic;
	double m_best_result;

	linear_allocator m_linear_allocator;

	std::vector<line_cache> m_line_caches;

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
};

#endif

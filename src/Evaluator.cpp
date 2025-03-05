#include <assert.h>
#include <functional>
#include <thread>
#include "Evaluator.h"
#include "Program.h"
#include "RegisterState.h"
#include "LinearAllocator.h"
#include "LineCache.h"
#include "TargetPicture.h"
#include "mt19937int.h"
#include <cfloat>

EvalGlobalState::EvalGlobalState()
	: m_update_autosave(false)
	, m_update_improvement(false)
	, m_update_initialized(false)
	, m_initialized(false)
	, m_finished(false)
	, m_threads_active(0)
	, m_save_period(0)
	, m_max_evals(0)
	, m_evaluations(0)
	, m_last_best_evaluation(0)
	, m_best_result(DBL_MAX)
	, m_previous_results_index(0)
	, m_cost_max(DBL_MAX)
	, m_N(0)
	, m_current_cost(DBL_MAX)
	, m_time_start(0)
	, m_mutex()
	, m_condvar_update()
{
	memset(m_mutation_stats, 0, sizeof(m_mutation_stats));
}

EvalGlobalState::~EvalGlobalState()
{
}

Evaluator::Evaluator()
	: m_currently_mutated_y(0)
	, m_best_result(DBL_MAX)
{
}

void Evaluator::Init(unsigned width, unsigned height, const distance_t *const *errmap, const screen_line *picture, const OnOffMap *onoff, EvalGlobalState *gstate, int solutions, unsigned long long randseed, size_t cache_size)
{
	m_randseed = randseed;
	m_width = width;
	m_height = height;
	m_picture_all_errors = errmap;
	m_picture = picture;
	m_onoff = onoff;
	m_gstate = gstate;
	m_solutions = solutions;
	m_cache_size = cache_size;

	m_currently_mutated_y=0;

	memset(m_current_mutations, 0, sizeof(m_current_mutations));

	m_line_caches.resize(m_height);

	m_created_picture.resize(m_height);
	for(int i=0; i<(int)m_height; ++i)
		m_created_picture[i].resize(m_width, 0);

	m_created_picture_targets.resize(height);
	for (size_t y=0;y<height;++y)
	{
		m_created_picture_targets[y].resize(width);
	}
}

void Evaluator::Start()
{
	++m_gstate->m_threads_active;

	std::thread thread{ std::bind( &Evaluator::Run, this ) };
	thread.detach();
}

void Evaluator::Run() {
	m_best_pic = m_gstate->m_best_pic;
	m_best_pic.recache_insns(m_insn_seq_cache, m_linear_allocator);

	unsigned last_eval = 0;
	bool clean_first_evaluation = true;
	clock_t last_rate_check_time = clock();

	raster_picture new_picture;
	std::vector<const line_cache_result*> line_results(m_height);

	for (;;) {
		if (m_linear_allocator.size() > m_cache_size) {
			m_insn_seq_cache.clear();
			for (int y2 = 0; y2 < (int)m_height; ++y2)
				m_line_caches[y2].clear();
			m_linear_allocator.clear();
			m_best_pic.recache_insns(m_insn_seq_cache, m_linear_allocator);
		}

		new_picture = m_best_pic;

		bool force_best = false;
		if (clean_first_evaluation) {
			clean_first_evaluation = false;
			force_best = true;
		}
		else {
			MutateRasterProgram(&new_picture);
		}

		double result = (double)ExecuteRasterProgram(&new_picture, line_results.data());

		std::unique_lock<std::mutex> lock{ m_gstate->m_mutex };

		++m_gstate->m_evaluations;

		// Initialize DLAS on first evaluation
		if (!m_gstate->m_initialized) {
			if (m_gstate->m_previous_results.empty()) {
				const double init_margin = result * 0.1; // 10% margin
				m_gstate->m_cost_max = result + init_margin;
				m_gstate->m_current_cost = result;
				m_gstate->m_previous_results.resize(m_solutions, m_gstate->m_cost_max);
				m_gstate->m_N = m_solutions;
			}
			m_gstate->m_initialized = true;
			m_gstate->m_update_initialized = true;
			m_gstate->m_condvar_update.notify_one();
		}

		// Store previous cost before potential update 
		double prev_cost = m_gstate->m_current_cost;

		// Calculate index for circular array
		size_t l = m_gstate->m_previous_results_index % m_solutions;

		// DLAS acceptance criteria
		if (result == m_gstate->m_current_cost || result < m_gstate->m_cost_max) {
			// Accept the candidate solution
			m_gstate->m_current_cost = result;

			// Update best solution if better 
			if (result < m_gstate->m_best_result) {
				m_gstate->m_last_best_evaluation = m_gstate->m_evaluations;
				m_gstate->m_best_pic = new_picture;
				m_gstate->m_best_pic.uncache_insns();
				m_gstate->m_best_result = result;

				// Update visualization state
				m_gstate->m_created_picture.resize(m_height);
				m_gstate->m_created_picture_targets.resize(m_height);

				for (int y = 0; y < (int)m_height; ++y) {
					const line_cache_result& lcr = *line_results[y];
					m_gstate->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_width);
					m_gstate->m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_width);
				}

				memcpy(&m_gstate->m_sprites_memory, m_sprites_memory, sizeof m_gstate->m_sprites_memory);
				m_gstate->m_update_improvement = true;

				for (int i = 0; i < E_MUTATION_MAX; ++i) {
					if (m_current_mutations[i]) {
						m_gstate->m_mutation_stats[i] += m_current_mutations[i];
						m_current_mutations[i] = 0;
					}
				}

				m_gstate->m_condvar_update.notify_one();
			}

			// DLAS replacement strategy 
			if (m_gstate->m_current_cost > m_gstate->m_previous_results[l]) {
				m_gstate->m_previous_results[l] = m_gstate->m_current_cost;
			}
			else if (m_gstate->m_current_cost < m_gstate->m_previous_results[l] &&
				m_gstate->m_current_cost < prev_cost) {

				// Track if we're removing a max value 
				if (m_gstate->m_previous_results[l] == m_gstate->m_cost_max) {
					--m_gstate->m_N;
				}

				// Replace the value 
				m_gstate->m_previous_results[l] = m_gstate->m_current_cost;

				// Recompute max and N if needed 
				if (m_gstate->m_N <= 0) {
					// Find new cost_max
					m_gstate->m_cost_max = *std::max_element(
						m_gstate->m_previous_results.begin(),
						m_gstate->m_previous_results.end()
					);

					// Recount occurrences of max
					m_gstate->m_N = std::count(
						m_gstate->m_previous_results.begin(),
						m_gstate->m_previous_results.end(),
						m_gstate->m_cost_max
					);
				}
			}
		}

		// Always increment index
		++m_gstate->m_previous_results_index;

		// Handle saving and termination checks
		if (m_gstate->m_save_period && m_gstate->m_evaluations % m_gstate->m_save_period == 0) {
			m_gstate->m_update_autosave = true;
			m_gstate->m_condvar_update.notify_one();
		}

		if (m_gstate->m_evaluations >= m_gstate->m_max_evals) {
			m_gstate->m_finished = true;
			m_gstate->m_condvar_update.notify_one();
		}

		if (m_best_result != m_gstate->m_best_result) {
			m_best_result = m_gstate->m_best_result;
			m_best_pic = m_gstate->m_best_pic;
			m_best_pic.recache_insns(m_insn_seq_cache, m_linear_allocator);
		}

		if (m_gstate->m_evaluations % 10000 == 0) {
			statistics_point stats;
			stats.evaluations = (unsigned)m_gstate->m_evaluations;
			stats.seconds = (unsigned)(time(NULL) - m_gstate->m_time_start);
			stats.distance = m_gstate->m_current_cost;

			m_gstate->m_statistics.push_back(stats);
		}

		if (m_gstate->m_finished)
			break;
	}

	std::unique_lock<std::mutex> lock{ m_gstate->m_mutex };
	--m_gstate->m_threads_active;
	m_gstate->m_condvar_update.notify_one();
}

e_target Evaluator::FindClosestColorRegister(sprites_row_memory_t& spriterow, int index, int x,int y, bool &restart_line, distance_t& best_error)
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
		int sprite_pos=m_sprite_shift_regs[temp-E_COLPM0];

		int sprite_x=sprite_pos-sprite_screen_color_cycle_start;

		unsigned x_offset = (unsigned)(x - sprite_x);
		if (x_offset < sprite_size)		// (x>=sprite_x && x<sprite_x+sprite_size)
		{
			sprite_bit=x_offset >> 2; // bit of this sprite memory
			assert(sprite_bit<8);

			sprite_covers_colbak=true;

			// never shifted out remaining sprite pixels combine with sprite memory
			int sprite_leftover_pixel = 0;
			int sprite_leftover = x_offset + m_sprite_shift_emitted[temp - E_COLPM0];
			if (sprite_leftover < sprite_size)
			{
				int sprite_leftover_bit = sprite_leftover >> 2;
				sprite_leftover_pixel = spriterow[temp - E_COLPM0][sprite_leftover_bit];
			}

//			distance = T_distance_function(pixel,atari_palette[mem_regs[temp]/2]);
			distance = m_picture_all_errors[m_mem_regs[temp]/2][index];
			if (spriterow[temp-E_COLPM0][sprite_bit] || sprite_leftover_pixel)
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
		distance = m_picture_all_errors[m_mem_regs[temp]/2][index];
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
		if (spriterow[result-E_COLPM0][best_sprite_bit]==false)
		{
			restart_line=true;
			spriterow[result-E_COLPM0][best_sprite_bit]=true;
		}

	}

	best_error = min_distance;

	return result;
}

void Evaluator::TurnOffRegisters(raster_picture *pic)
{
	for (size_t i=0;i<E_TARGET_MAX;++i)
	{
		if (m_onoff->on_off[0][i]==false)
			pic->mem_regs_init[i]=0;
	}

	for (int y=0; y<(int)m_height;++y)
	{
		size_t size=pic->raster_lines[y].instructions.size();
		SRasterInstruction *__restrict rastinsns = &pic->raster_lines[y].instructions[0];
		for (size_t i=0;i<size;++i)
		{
			unsigned char target=rastinsns[i].loose.target;
			if (target<E_TARGET_MAX && m_onoff->on_off[y][target]==false)
				rastinsns[i].loose.target=E_TARGET_MAX;
		}		
	}
}

distance_accum_t Evaluator::ExecuteRasterProgram(raster_picture *pic, const line_cache_result **results_array)
{
	int x,y; // currently processed pixel

	int cycle;
	int next_instr_offset;
	int ip; // instruction pointer

	const SRasterInstruction *__restrict instr;

	m_reg_a=0;
	m_reg_x=0;
	m_reg_y=0;

	if (m_onoff)
		TurnOffRegisters(pic);

	memset(m_sprite_shift_regs,0,sizeof(m_sprite_shift_regs));
	memcpy(m_mem_regs,pic->mem_regs_init,sizeof(pic->mem_regs_init));
	memset(m_sprites_memory,0,sizeof(m_sprites_memory));
	
	bool restart_line=false;
	bool shift_start_array_dirty = true;
	distance_accum_t total_error = 0;

	for (y=0; y<(int)m_height; ++y)
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
		raster_line& rline = pic->raster_lines[y];

		line_cache_key lck;
		CaptureRegisterState(lck.entry_state);
		lck.insn_seq = rline.cache_key;

		const uint32_t lck_hash = lck.hash();

		// check line cache
		unsigned char * __restrict created_picture_row = &m_created_picture[y][0];
		unsigned char * __restrict created_picture_targets_row = &m_created_picture_targets[y][0];

		const line_cache_result *cached_line_result = m_line_caches[y].find(lck, lck_hash);
		if (cached_line_result)
		{
			// sweet! cache hit!!
			results_array[y] = cached_line_result;
			ApplyRegisterState(cached_line_result->new_state);
			memcpy(m_sprites_memory[y], cached_line_result->sprite_data, sizeof m_sprites_memory[y]);
			shift_start_array_dirty = true;

			total_error += cached_line_result->line_error;
			continue;
		}

		if (shift_start_array_dirty)
		{
			shift_start_array_dirty = false;

			ResetSpriteShiftStartArray();
		}

		const SRasterInstruction *__restrict rastinsns = &rline.instructions[0];
		const int rastinsncnt = rline.instructions.size();

		restart_line=false;
		ip=0;
		cycle=0;
		next_instr_offset=screen_cycles[cycle].offset;

		// on new line clear sprite shifts and wait to be taken from mem_regs
		memset(m_sprite_shift_regs,0,sizeof(m_sprite_shift_regs));

		if (!rastinsncnt)
			next_instr_offset = 1000;

		const int picture_row_index = m_width * y;

		distance_accum_t total_line_error = 0;

		sprites_row_memory_t& spriterow = m_sprites_memory[y];

		for (x=-sprite_screen_color_cycle_start;x<176;++x)
		{
			// check position of sprites
			const int sprite_check_x = x + sprite_screen_color_cycle_start;

			const unsigned char sprite_start_mask = m_sprite_shift_start_array[sprite_check_x];

			if (sprite_start_mask)
			{
				//if (sprite_start_mask & 1) m_sprite_shift_regs[0] = m_mem_regs[E_HPOSP0];
				//if (sprite_start_mask & 2) m_sprite_shift_regs[1] = m_mem_regs[E_HPOSP1];
				//if (sprite_start_mask & 4) m_sprite_shift_regs[2] = m_mem_regs[E_HPOSP2];
				//if (sprite_start_mask & 8) m_sprite_shift_regs[3] = m_mem_regs[E_HPOSP3];
				if (sprite_start_mask & 1) StartSpriteShift(E_HPOSP0);
				if (sprite_start_mask & 2) StartSpriteShift(E_HPOSP1);
				if (sprite_start_mask & 4) StartSpriteShift(E_HPOSP2);
				if (sprite_start_mask & 8) StartSpriteShift(E_HPOSP3);
			}

			while(next_instr_offset<x && ip<rastinsncnt) // execute instructions
			{
				// check position of sprites

				instr = &rastinsns[ip++];

				//if (cycle<4) // in the previous line
				//	ExecuteInstruction(*instr,x+200);
				//else
				//	ExecuteInstruction(*instr,x);
				ExecuteInstruction(*instr, sprite_check_x, spriterow, total_line_error);

				cycle+=GetInstructionCycles(*instr);
				next_instr_offset=screen_cycles[cycle].offset;
				if (ip >= rastinsncnt)
					next_instr_offset = 1000;
			}

			if ((unsigned)x < (unsigned)m_width)		// x>=0 && x<m_width
			{
				// put pixel closest to one of the current color registers
				distance_t closest_dist;
				e_target closest_register = FindClosestColorRegister(spriterow, picture_row_index + x,x,y,restart_line,closest_dist);
				total_line_error += closest_dist;
				created_picture_row[x]=m_mem_regs[closest_register] >> 1;
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
			line_cache_result& result_state = m_line_caches[y].insert(lck, lck_hash, m_linear_allocator);

			result_state.line_error = total_line_error;
			CaptureRegisterState(result_state.new_state);
			result_state.color_row = (unsigned char *)m_linear_allocator.allocate(m_width);
			memcpy(result_state.color_row, created_picture_row, m_width);

			result_state.target_row = (unsigned char *)m_linear_allocator.allocate(m_width);
			memcpy(result_state.target_row, created_picture_targets_row, m_width);

			memcpy(result_state.sprite_data, m_sprites_memory[y], sizeof result_state.sprite_data);

			results_array[y] = &result_state;
		}
	}

	return total_error;
}

template<fn_rgb_distance& T_distance_function>
distance_accum_t Evaluator::CalculateLineDistance(const screen_line &r, const screen_line &l)
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

//inline void Evaluator::ExecuteInstruction(const SRasterInstruction &instr, int x)
inline void Evaluator::ExecuteInstruction(const SRasterInstruction &instr, int sprite_check_x, sprites_row_memory_t &spriterow, distance_accum_t &total_line_error)
{
	int reg_value=-1;
	switch(instr.loose.instruction)
	{
	case E_RASTER_LDA:
		m_reg_a=instr.loose.value;
		break;
	case E_RASTER_LDX:
		m_reg_x=instr.loose.value;
		break;
	case E_RASTER_LDY:
		m_reg_y=instr.loose.value;
		break;
	case E_RASTER_STA:
		reg_value=m_reg_a;
		break;
	case E_RASTER_STX:
		reg_value=m_reg_x;
		break;
	case E_RASTER_STY:
		reg_value=m_reg_y;
		break;
	}

	if (reg_value!=-1)
	{
		const unsigned hpos_index = (unsigned)(instr.loose.target - E_HPOSP0);
		if (hpos_index < 4) 
		{
			// Check for unemulated 5 to 6 colour clock latency issues on player hpos changes.
			// Unexpected horizontal lines appear in pictures otherwise when viewed on real
			// hardware and modern emulators.
			// This change strongly discourages the use of the solution for the line but does not
			// make the problem horizontal lines show on screen when RastaConverter is running.
			const int sprite_old_x = m_mem_regs[instr.loose.target];
			const int sprite_new_x = reg_value;
			const int sprites_visible_left = sprite_screen_color_cycle_start - sprite_size;
			const int sprites_visible_right = sprite_screen_color_cycle_start + 160-1;
			if (sprite_old_x != sprite_new_x && sprite_new_x >= sprites_visible_left && sprite_new_x <= sprites_visible_right)
			{
				// check if anything to display
				int sprite_bits;
				const int sprite = hpos_index;
				for (sprite_bits = 7; sprite_bits >= 0; --sprite_bits)
				{
					if (spriterow[sprite][sprite_bits])
						break;
				}
				if (sprite_bits >= 0 && sprite_old_x - sprite_check_x <= 6 && sprite_old_x - sprite_check_x > 0)
					// too late to prevent display at old position
					total_line_error += 100000;
				if (sprite_bits >= 0 && sprite_new_x - sprite_check_x <= 6 && sprite_new_x - sprite_check_x > 0)
					// too late to change display to new position
					total_line_error += 100000;
			}

			m_sprite_shift_start_array[m_mem_regs[instr.loose.target]] &= ~(1 << hpos_index);
			m_mem_regs[instr.loose.target]=reg_value;
			m_sprite_shift_start_array[m_mem_regs[instr.loose.target]] |= (1 << hpos_index);
		} 
		else 
		{
			m_mem_regs[instr.loose.target]=reg_value;
		}
	}
}

void Evaluator::StartSpriteShift(int mem_reg)
{
	unsigned char sprite_self_overlap = m_mem_regs[mem_reg] - m_sprite_shift_regs[mem_reg-E_HPOSP0];
	if (sprite_self_overlap > 0 && sprite_self_overlap < sprite_size)
		// number of sprite bits shifted out from the old position
		m_sprite_shift_emitted[mem_reg-E_HPOSP0] = sprite_self_overlap;
	else
		// default is all sprite bits shifted out, no leftover
		m_sprite_shift_emitted[mem_reg-E_HPOSP0] = sprite_size;

	// new shift out starting now at this position
	m_sprite_shift_regs[mem_reg-E_HPOSP0] = m_mem_regs[mem_reg];
}

void Evaluator::MutateLine(raster_line &prog, raster_picture &pic)
{
	// hline(screen,m_width*2,m_currently_mutated_y,m_width*4,makecol(0xFF,0xFF,0xFF));

	int r=Random(prog.instructions.size());
	for (int i=0;i<=r;++i) 
	{
		MutateOnce(prog, pic);
	}

	prog.rehash();
}

void Evaluator::MutateOnce(raster_line &prog, raster_picture &pic)
{
	int i1,i2,c,x;

	i1=Random(prog.instructions.size());
	i2=i1;
	if (prog.instructions.size()>2)
	{
		do 
		{
			i2=Random(prog.instructions.size());
		} while (i1==i2);
	}

	SRasterInstruction temp;

	int mutation=Random(E_MUTATION_MAX);
	switch(mutation)
	{
	case E_MUTATION_COPY_LINE_TO_NEXT_ONE:
		if (m_currently_mutated_y<(int)m_height-1)
		{
			int next_y = m_currently_mutated_y+1;
			raster_line &next_line=pic.raster_lines[next_y];
			prog=next_line;
			m_current_mutations[E_MUTATION_COPY_LINE_TO_NEXT_ONE]++;
			break;
		}
	case E_MUTATION_PUSH_BACK_TO_PREV:
		if (m_currently_mutated_y>0)
		{
			int prev_y = m_currently_mutated_y-1;
			raster_line &prev_line=pic.raster_lines[prev_y];
			c = GetInstructionCycles(prog.instructions[i1]);
			if (prev_line.cycles+c<free_cycles)
			{
				// add it to prev line but do not remove it from the current
				prev_line.cycles+=c;
				prev_line.instructions.push_back(prog.instructions[i1]);
				prev_line.cache_key = NULL;
				m_current_mutations[E_MUTATION_PUSH_BACK_TO_PREV]++;
				break;
			}
		}
	case E_MUTATION_SWAP_LINE_WITH_PREV_ONE:
		if (m_currently_mutated_y>0)
		{
			int prev_y = m_currently_mutated_y-1;
			raster_line &prev_line=pic.raster_lines[prev_y];
			prog.swap(prev_line);
			m_current_mutations[E_MUTATION_SWAP_LINE_WITH_PREV_ONE]++;
			break;
		}
	case E_MUTATION_ADD_INSTRUCTION:
		if (prog.cycles+2<free_cycles)
		{
			if (prog.cycles+4<free_cycles && Random(2)) // 4 cycles instructions
			{
				temp.loose.instruction=(e_raster_instruction) (E_RASTER_STA+Random(3));
				temp.loose.value=(Random(128)*2);
				temp.loose.target=(e_target) (Random(E_TARGET_MAX));
				prog.instructions.insert(prog.instructions.begin()+i1,temp);
				prog.cache_key = NULL;
				prog.cycles+=4;
			}
			else
			{
				temp.loose.instruction=(e_raster_instruction) (E_RASTER_LDA+Random(4));
				if (Random(2))
					temp.loose.value=(Random(128)*2);
				else
				{
					const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
					temp.loose.value=possible_colors[Random(possible_colors.size())];
				}

				temp.loose.target=(e_target) (Random(E_TARGET_MAX));
				c=Random(m_picture[m_currently_mutated_y].size());
				temp.loose.value=FindAtariColorIndex(m_picture[m_currently_mutated_y][c])*2;
				prog.instructions.insert(prog.instructions.begin()+i1,temp);
				prog.cache_key = NULL;
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
				prog.cache_key = NULL;
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
			prog.cache_key = NULL;
			m_current_mutations[E_MUTATION_SWAP_INSTRUCTION]++;
			break;
		}
	case E_MUTATION_CHANGE_TARGET:
		prog.instructions[i1].loose.target=(e_target) (Random(E_TARGET_MAX));
		prog.cache_key = NULL;
		m_current_mutations[E_MUTATION_CHANGE_TARGET]++;
		break;
	case E_MUTATION_CHANGE_VALUE_TO_COLOR:
		if ((prog.instructions[i1].loose.target>=E_HPOSP0 && prog.instructions[i1].loose.target<=E_HPOSP3))
		{
			x=m_mem_regs[prog.instructions[i1].loose.target]-sprite_screen_color_cycle_start;
			x+=Random(sprite_size);
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
			while(Random(5)==0)
				++c;

			if (c>=free_cycles)
				c=free_cycles-1;
			x=screen_cycles[c].offset;
			x+=Random(screen_cycles[c].length);
		}
		if (x<0 || x>=(int)m_width)
			x=Random(m_width);
		i2=m_currently_mutated_y;
		// check color in next lines
		while(Random(5)==0 && i2+1 < (int)m_height)
			++i2;
		prog.instructions[i1].loose.value=FindAtariColorIndex(m_picture[i2][x])*2;
		prog.cache_key = NULL;
		m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
		break;
	case E_MUTATION_CHANGE_VALUE:
		if (Random(10)==0)
		{
			if (Random(2))
				prog.instructions[i1].loose.value=(Random(128)*2);
			else
			{
				const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
				prog.instructions[i1].loose.value=possible_colors[Random(possible_colors.size())];
			}
		}
		else
		{
			c=1;
			if (Random(2))
				c*=-1;
			if (Random(2))
				c*=16;
			prog.instructions[i1].loose.value+=c;
		}
		prog.cache_key = NULL;
		m_current_mutations[E_MUTATION_CHANGE_VALUE]++;
		break;
	}	
}

void Evaluator::MutateRasterProgram(raster_picture *pic)
{
	memset(m_current_mutations, 0, sizeof m_current_mutations);

	// find the best line to modify
	if (Random(10)==0) // mutate random init mem reg
	{
		int c=1;
		if (Random(2))
			c*=-1;
		if (Random(2))
			c*=16;

		int targ;
		do {
			targ = Random(E_TARGET_MAX);
		} while (targ == E_COLBAK);

		pic->mem_regs_init[targ]+=c;
	}

	if (m_currently_mutated_y>=(int) pic->raster_lines.size())
		m_currently_mutated_y=0;
	if (m_currently_mutated_y<0)
		m_currently_mutated_y=pic->raster_lines.size()-1;

	raster_line &current_line=pic->raster_lines[m_currently_mutated_y];
	MutateLine(current_line, *pic);

	if (Random(20)==0)
	{
		for (int t=0;t<10;++t)
		{
			if (Random(2) && m_currently_mutated_y>0)
				--m_currently_mutated_y;
			else
				m_currently_mutated_y=Random(pic->raster_lines.size());

			raster_line &current_line=pic->raster_lines[m_currently_mutated_y];
			MutateLine(current_line, *pic);
		}
	}

	// recache any lines that have changed
	for(int y=0; y<(int)m_height; ++y)
	{
		raster_line& rline = pic->raster_lines[y];
		if (rline.cache_key == NULL)
			rline.recache_insns(m_insn_seq_cache, m_linear_allocator);
	}
}

void Evaluator::CaptureRegisterState(register_state& rs) const
{
	rs.reg_a = m_reg_a;
	rs.reg_x = m_reg_x;
	rs.reg_y = m_reg_y;

	memcpy(rs.mem_regs, m_mem_regs, sizeof rs.mem_regs);
}

void Evaluator::ApplyRegisterState(const register_state& rs)
{
	m_reg_a = rs.reg_a;
	m_reg_x = rs.reg_x;
	m_reg_y = rs.reg_y;
	memcpy(m_mem_regs, rs.mem_regs, sizeof m_mem_regs);
}

void Evaluator::StoreLineRegs()
{
	CaptureRegisterState(m_old_reg_state);
}

void Evaluator::RestoreLineRegs()
{
	ApplyRegisterState(m_old_reg_state);
}

void Evaluator::ResetSpriteShiftStartArray()
{
	memset(m_sprite_shift_start_array, 0, sizeof m_sprite_shift_start_array);

	for(int i=0; i<4; ++i)
		m_sprite_shift_start_array[m_mem_regs[i+E_HPOSP0]] |= (1 << i);
}

int Evaluator::Random(int range)
{
	if (range==0)
		return 0;

	// Mersenne Twister is used for longer period
//	return genrand_int32()%range;

	m_randseed = (m_randseed << 32) + (((m_randseed >> (63-32)) ^ (m_randseed >> (62-32))) & ((1ull << 32) - 1));
	return ((int)m_randseed & 0x7fffffff) % range;
}

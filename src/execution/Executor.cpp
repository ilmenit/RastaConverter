#include "Executor.h"
#include "../optimization/EvaluationContext.h"
#include "TargetPicture.h"
#include "../mutation/Mutator.h"
#include <algorithm>
#include <thread>
#include <functional>

Executor::Executor()
    : m_gstate(nullptr)
    , m_thread_id(0)
    , m_reg_a(0)
    , m_reg_x(0)
    , m_reg_y(0)
    , m_linear_allocator_ptr(nullptr)
    , m_insn_seq_cache_ptr(nullptr)
    , m_best_result(DBL_MAX)
    , m_randseed(0)
    , m_mutator(nullptr)
{
    memset(m_mem_regs, 0, sizeof(m_mem_regs));
    memset(m_sprites_memory, 0, sizeof(m_sprites_memory));
    memset(m_sprite_shift_regs, 0, sizeof(m_sprite_shift_regs));
    memset(m_sprite_shift_emitted, 0, sizeof(m_sprite_shift_emitted));
    memset(m_sprite_shift_start_array, 0, sizeof(m_sprite_shift_start_array));
    memset(m_picture_all_errors, 0, sizeof(m_picture_all_errors));
}

void Executor::Init(unsigned width, unsigned height, 
                   const std::vector<distance_t>* pictureAllErrors[128],  // Changed to match RastaConverter
                   const screen_line* picture, const OnOffMap* onoff, 
                   EvaluationContext* gstate, int solutions, 
                   unsigned long long randseed, size_t cache_size, 
                   Mutator* mutator,
                   int thread_id)
{
    m_randseed = randseed;
    m_width = width;
    m_height = height;
    
    // Copy the array of pointers to vectors
    for (int i = 0; i < 128; i++) {
        m_picture_all_errors[i] = pictureAllErrors[i];
    }
    
    m_picture = picture;
    m_onoff = onoff;
    m_gstate = gstate;
    m_solutions = solutions;
    m_cache_size = cache_size;
    m_thread_id = thread_id;
    m_mutator = mutator;

    // Set up caching
    m_linear_allocator_ptr = &m_thread_local_allocator;
    m_insn_seq_cache_ptr = &m_thread_local_cache;

    // Initialize caches
    m_line_caches.resize(m_height);

    // Initialize output storage
    m_created_picture.resize(m_height);
    for (int i = 0; i < (int)m_height; ++i)
        m_created_picture[i].resize(m_width, 0);

    m_created_picture_targets.resize(height);
    for (size_t y = 0; y < height; ++y)
    {
        m_created_picture_targets[y].resize(width);
    }

    // Clear LRU tracking
    m_lru_lines.clear();
    m_lru_set.clear();
}

void Executor::UpdateLRU(int line_index) {
    // If already in the set, remove it from current position in the queue
    if (m_lru_set.find(line_index) != m_lru_set.end()) {
        auto it = std::find(m_lru_lines.begin(), m_lru_lines.end(), line_index);
        if (it != m_lru_lines.end()) {
            m_lru_lines.erase(it);
        }
    }
    else {
        // Add to the set
        m_lru_set.insert(line_index);
    }

    // Add to back of queue (most recently used)
    m_lru_lines.push_back(line_index);

    // Keep the LRU queue at a reasonable size
    while (m_lru_lines.size() > m_height * 2) {
        m_lru_set.erase(m_lru_lines.front());
        m_lru_lines.pop_front();
    }
}

void Executor::Start()
{
    if (m_gstate) {
        ++m_gstate->m_threads_active;

        std::thread thread{ std::bind(&Executor::Run, this) };
        thread.detach();
    }
}

void Executor::Run() {
    if (!m_gstate) return;

    m_best_pic = m_gstate->m_best_pic;
    m_best_pic.recache_insns(*m_insn_seq_cache_ptr, *m_linear_allocator_ptr);

    unsigned last_eval = 0;
    bool clean_first_evaluation = true;
    clock_t last_rate_check_time = clock();

    raster_picture new_picture;
    std::vector<const line_cache_result*> line_results(m_height);

    for (;;) {
        if (m_linear_allocator_ptr->size() > m_cache_size) {
            // Acquire a mutex to coordinate cache clearing
            std::unique_lock<std::mutex> cache_lock(m_gstate->m_cache_mutex);

            // Check again after acquiring the lock (another thread might have cleared)
            if (m_linear_allocator_ptr->size() > m_cache_size) {
                // First, try clearing least recently used lines (25% of height)
                size_t lines_to_clear = std::max((size_t)m_height / 4, (size_t)1);
                size_t cleared = 0;

                // Clear the least recently used lines
                while (cleared < lines_to_clear && !m_lru_lines.empty()) {
                    int y = m_lru_lines.front();
                    m_lru_lines.pop_front();
                    m_lru_set.erase(y);

                    // Clear just this line's cache
                    m_line_caches[y].clear();
                    cleared++;
                }

                // If we're still using too much memory, do a full clear
                if (m_linear_allocator_ptr->size() > m_cache_size * 0.9) {
                    m_insn_seq_cache_ptr->clear();
                    for (int y2 = 0; y2 < (int)m_height; ++y2)
                        m_line_caches[y2].clear();
                    m_linear_allocator_ptr->clear();
                    m_best_pic.recache_insns(*m_insn_seq_cache_ptr, *m_linear_allocator_ptr);

                    // Reset LRU tracking
                    m_lru_lines.clear();
                    m_lru_set.clear();
                }
            }
        }

        new_picture = m_best_pic;

        bool force_best = false;
        if (clean_first_evaluation) {
            clean_first_evaluation = false;
            force_best = true;
        }
        else {
            // Use the mutator to perform mutations
            m_mutator->MutateProgram(&new_picture);
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

                // Get mutation statistics from the mutator
                const int* current_mutations = m_mutator->GetCurrentMutations();
                for (int i = 0; i < E_MUTATION_MAX; ++i) {
                    if (current_mutations[i]) {
                        m_gstate->m_mutation_stats[i] += current_mutations[i];
                    }
                }

                m_gstate->m_condvar_update.notify_one();
            }
        }

        // DLAS replacement strategy - MOVED OUTSIDE acceptance condition
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
            m_best_pic.recache_insns(*m_insn_seq_cache_ptr, *m_linear_allocator_ptr);
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

distance_accum_t Executor::ExecuteRasterProgram(raster_picture* pic, const line_cache_result** results_array)
{
    int x, y; // currently processed pixel

    int cycle;
    int next_instr_offset;
    int ip; // instruction pointer

    const SRasterInstruction* __restrict instr;

    m_reg_a = 0;
    m_reg_x = 0;
    m_reg_y = 0;

    if (m_onoff)
        TurnOffRegisters(pic);

    memset(m_sprite_shift_regs, 0, sizeof(m_sprite_shift_regs));
    memset(m_sprite_shift_emitted, 0, sizeof(m_sprite_shift_emitted));
    memcpy(m_mem_regs, pic->mem_regs_init, sizeof(pic->mem_regs_init));
    memset(m_sprites_memory, 0, sizeof(m_sprites_memory));
    
    bool restart_line = false;
    bool shift_start_array_dirty = true;
    distance_accum_t total_error = 0;

    for (y = 0; y < (int)m_height; ++y)
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
        unsigned char* __restrict created_picture_row = &m_created_picture[y][0];
        unsigned char* __restrict created_picture_targets_row = &m_created_picture_targets[y][0];

        const line_cache_result* cached_line_result = m_line_caches[y].find(lck, lck_hash);
        if (cached_line_result)
        {
            // sweet! cache hit!!
            results_array[y] = cached_line_result;
            ApplyRegisterState(cached_line_result->new_state);
            memcpy(m_sprites_memory[y], cached_line_result->sprite_data, sizeof m_sprites_memory[y]);
            shift_start_array_dirty = true;

            // Update LRU status for this line
            UpdateLRU(y);

            total_error += cached_line_result->line_error;
            continue;
        }

        if (shift_start_array_dirty)
        {
            shift_start_array_dirty = false;
            ResetSpriteShiftStartArray();
        }

        const SRasterInstruction* __restrict rastinsns = &rline.instructions[0];
        const int rastinsncnt = (int)rline.instructions.size();

        restart_line = false;
        ip = 0;
        cycle = 0;
        next_instr_offset = screen_cycles[cycle].offset;

        // on new line clear sprite shifts and wait to be taken from mem_regs
        memset(m_sprite_shift_regs, 0, sizeof(m_sprite_shift_regs));
        memset(m_sprite_shift_emitted, 0, sizeof(m_sprite_shift_emitted));

        if (!rastinsncnt)
            next_instr_offset = 1000;

        const int picture_row_index = m_width * y;

        distance_accum_t total_line_error = 0;

        sprites_row_memory_t& spriterow = m_sprites_memory[y];

        for (x = -sprite_screen_color_cycle_start; x < 176; ++x)
        {
            // check position of sprites
            const int sprite_check_x = x + sprite_screen_color_cycle_start;

            const unsigned char sprite_start_mask = m_sprite_shift_start_array[sprite_check_x];

            if (sprite_start_mask)
            {
                if (sprite_start_mask & 1) StartSpriteShift(E_HPOSP0);
                if (sprite_start_mask & 2) StartSpriteShift(E_HPOSP1);
                if (sprite_start_mask & 4) StartSpriteShift(E_HPOSP2);
                if (sprite_start_mask & 8) StartSpriteShift(E_HPOSP3);
            }

            while (next_instr_offset < x && ip < rastinsncnt) // execute instructions
            {
                instr = &rastinsns[ip++];
                ExecuteInstruction(*instr, sprite_check_x, spriterow, total_line_error);

                cycle += GetInstructionCycles(*instr);
                next_instr_offset = screen_cycles[cycle].offset;
                if (ip >= rastinsncnt)
                    next_instr_offset = 1000;
            }

            if ((unsigned)x < (unsigned)m_width)        // x>=0 && x<m_width
            {
                // put pixel closest to one of the current color registers
                distance_t closest_dist;
                e_target closest_register = FindClosestColorRegister(spriterow, picture_row_index + x, x, y, restart_line, closest_dist);
                total_line_error += closest_dist;
                created_picture_row[x] = m_mem_regs[closest_register] >> 1;
                created_picture_targets_row[x] = closest_register;
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
            line_cache_result& result_state = m_line_caches[y].insert(lck, lck_hash, *m_linear_allocator_ptr);
            // Update LRU status for this line
            UpdateLRU(y);

            result_state.line_error = total_line_error;
            CaptureRegisterState(result_state.new_state);
            result_state.color_row = (unsigned char*)m_linear_allocator_ptr->allocate(m_width);
            memcpy(result_state.color_row, created_picture_row, m_width);

            result_state.target_row = (unsigned char*)m_linear_allocator_ptr->allocate(m_width);
            memcpy(result_state.target_row, created_picture_targets_row, m_width);

            memcpy(result_state.sprite_data, m_sprites_memory[y], sizeof result_state.sprite_data);

            results_array[y] = &result_state;
        }
    }

    return total_error;
}

e_target Executor::FindClosestColorRegister(sprites_row_memory_t& spriterow, int index, int x, int y, bool &restart_line, distance_t& best_error)
{
    distance_t distance;
    int sprite_bit;
    int best_sprite_bit;
    e_target result = E_COLBAK;
    distance_t min_distance = DISTANCE_MAX;
    bool sprite_covers_colbak = false;

    // check sprites
    // Sprites priority is 0,1,2,3
    for (int temp = E_COLPM0; temp <= E_COLPM3; ++temp)
    {
        int sprite_pos = m_sprite_shift_regs[temp - E_COLPM0];
        int sprite_x = sprite_pos - sprite_screen_color_cycle_start;

        unsigned x_offset = (unsigned)(x - sprite_x);
        if (x_offset < sprite_size)        // (x>=sprite_x && x<sprite_x+sprite_size)
        {
            sprite_bit = x_offset >> 2; // bit of this sprite memory
            assert(sprite_bit < 8);

            sprite_covers_colbak = true;

            // never shifted out remaining sprite pixels combine with sprite memory
            int sprite_leftover_pixel = 0;
            int sprite_leftover = x_offset + m_sprite_shift_emitted[temp - E_COLPM0];
            if (sprite_leftover < sprite_size)
            {
                int sprite_leftover_bit = sprite_leftover >> 2;
                sprite_leftover_pixel = spriterow[temp - E_COLPM0][sprite_leftover_bit];
            }

            // MODIFIED: Use the vector of distance values properly
            distance = (m_picture_all_errors[m_mem_regs[temp] / 2])->at(index);
            
            if (spriterow[temp - E_COLPM0][sprite_bit] || sprite_leftover_pixel)
            {
                // priority of sprites - next sprites are hidden below that one, so they are not processed
                best_sprite_bit = sprite_bit;
                result = (e_target)temp;
                min_distance = distance;
                break;
            }
            if (distance < min_distance)
            {
                best_sprite_bit = sprite_bit;
                result = (e_target)temp;
                min_distance = distance;
            }
        }
    }

    // check standard colors
    int last_color_register;

    if (sprite_covers_colbak)
        last_color_register = E_COLOR2; // COLBAK is not used
    else
        last_color_register = E_COLBAK;

    for (int temp = E_COLOR0; temp <= last_color_register; ++temp)
    {
        // MODIFIED: Use the vector of distance values properly
        distance = (m_picture_all_errors[m_mem_regs[temp] / 2])->at(index);
        
        if (distance < min_distance)
        {
            min_distance = distance;
            result = (e_target)temp;
        }
    }

    // the best color is in sprite, then set the proper bit of the sprite memory and then restart this line
    if (result >= E_COLPM0 && result <= E_COLPM3)
    {
        // if PMG bit has been modified, then restart this line, because previous pixels of COLBAK may be covered
        if (spriterow[result - E_COLPM0][best_sprite_bit] == false)
        {
            restart_line = true;
            spriterow[result - E_COLPM0][best_sprite_bit] = true;
        }
    }

    best_error = min_distance;
    return result;
}

void Executor::TurnOffRegisters(raster_picture *pic)
{
    for (size_t i = 0; i < E_TARGET_MAX; ++i)
    {
        if (m_onoff->on_off[0][i] == false)
            pic->mem_regs_init[i] = 0;
    }

    for (int y = 0; y < (int)m_height; ++y)
    {
        size_t size = pic->raster_lines[y].instructions.size();
        SRasterInstruction *__restrict rastinsns = &pic->raster_lines[y].instructions[0];
        for (size_t i = 0; i < size; ++i)
        {
            unsigned char target = rastinsns[i].loose.target;
            if (target < E_TARGET_MAX && m_onoff->on_off[y][target] == false)
                rastinsns[i].loose.target = E_TARGET_MAX;
        }
    }
}

void Executor::ExecuteInstruction(const SRasterInstruction &instr, int sprite_check_x, sprites_row_memory_t &spriterow, distance_accum_t &total_line_error)
{
    int reg_value = -1;
    switch (instr.loose.instruction)
    {
    case E_RASTER_LDA:
        m_reg_a = instr.loose.value;
        break;
    case E_RASTER_LDX:
        m_reg_x = instr.loose.value;
        break;
    case E_RASTER_LDY:
        m_reg_y = instr.loose.value;
        break;
    case E_RASTER_STA:
        reg_value = m_reg_a;
        break;
    case E_RASTER_STX:
        reg_value = m_reg_x;
        break;
    case E_RASTER_STY:
        reg_value = m_reg_y;
        break;
    }

    if (reg_value != -1)
    {
        const unsigned hpos_index = (unsigned)(instr.loose.target - E_HPOSP0);
        if (hpos_index < 4)
        {
            // Check for unemulated 5 to 6 colour clock latency issues on player hpos changes.
            // Unexpected horizontal lines appear in pictures otherwise when viewed on real
            // hardware and modern emulators.
            const int sprite_old_x = m_mem_regs[instr.loose.target];
            const int sprite_new_x = reg_value;
            const int sprites_visible_left = sprite_screen_color_cycle_start - sprite_size;
            const int sprites_visible_right = sprite_screen_color_cycle_start + 160 - 1;
            
            if (sprite_old_x != sprite_new_x && sprite_new_x >= sprites_visible_left && sprite_new_x <= sprites_visible_right)
            {
                // check if anything to display
                int sprite_bits;
                const int sprite = (int)hpos_index;
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
            m_mem_regs[instr.loose.target] = reg_value;
            m_sprite_shift_start_array[m_mem_regs[instr.loose.target]] |= (1 << hpos_index);
        }
        else
        {
            m_mem_regs[instr.loose.target] = reg_value;
        }
    }
}

void Executor::StartSpriteShift(int mem_reg)
{
    unsigned char sprite_self_overlap = m_mem_regs[mem_reg] - m_sprite_shift_regs[mem_reg - E_HPOSP0];
    if (sprite_self_overlap > 0 && sprite_self_overlap < sprite_size)
        // number of sprite bits shifted out from the old position
        m_sprite_shift_emitted[mem_reg - E_HPOSP0] = sprite_self_overlap;
    else
        // default is all sprite bits shifted out, no leftover
        m_sprite_shift_emitted[mem_reg - E_HPOSP0] = sprite_size;

    // new shift out starting now at this position
    m_sprite_shift_regs[mem_reg - E_HPOSP0] = m_mem_regs[mem_reg];
}

void Executor::ResetSpriteShiftStartArray()
{
    memset(m_sprite_shift_start_array, 0, sizeof m_sprite_shift_start_array);

    for (int i = 0; i < 4; ++i)
        m_sprite_shift_start_array[m_mem_regs[i + E_HPOSP0]] |= (1 << i);
}

void Executor::CaptureRegisterState(register_state& rs) const
{
    rs.reg_a = m_reg_a;
    rs.reg_x = m_reg_x;
    rs.reg_y = m_reg_y;

    memcpy(rs.mem_regs, m_mem_regs, sizeof rs.mem_regs);
}

void Executor::ApplyRegisterState(const register_state& rs)
{
    m_reg_a = rs.reg_a;
    m_reg_x = rs.reg_x;
    m_reg_y = rs.reg_y;
    memcpy(m_mem_regs, rs.mem_regs, sizeof m_mem_regs);
}

void Executor::StoreLineRegs()
{
    CaptureRegisterState(m_old_reg_state);
}

void Executor::RestoreLineRegs()
{
    ApplyRegisterState(m_old_reg_state);
}

void Executor::ClearLineCaches()
{
    m_insn_seq_cache_ptr->clear();
    for (int y = 0; y < (int)m_height; ++y)
        m_line_caches[y].clear();
    m_linear_allocator_ptr->clear();
    
    // Reset LRU tracking
    m_lru_lines.clear();
    m_lru_set.clear();
}

int Executor::Random(int range)
{
    if (range <= 0)
        return 0;

    // XorShift algorithm - much faster than the current LFSR
    m_randseed ^= m_randseed << 13;
    m_randseed ^= m_randseed >> 17;
    m_randseed ^= m_randseed << 5;

    // Use rejection sampling for unbiased distribution
    uint32_t scaled = (uint32_t)(m_randseed & 0x7FFFFFFF) % range;
    return (int)scaled;
}
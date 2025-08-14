#include "Executor.h"
#include "../optimization/EvaluationContext.h"
#include "target/TargetPicture.h"
#include "../mutation/Mutator.h"
#include <algorithm>
#include <thread>
#include <functional>
#include <iostream>
#include <cmath>
#include "../utils/RandomUtils.h"

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
                   const std::vector<distance_t>* pictureAllErrors[128],
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
    m_line_caches_dual.resize(m_height);

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
        // Register this executor with the evaluation context
        m_gstate->RegisterExecutor(this);
    }
}

void Executor::Run() {
    if (!m_gstate) return;

    m_best_pic = m_gstate->m_best_pic;
    m_best_pic.recache_insns(*m_insn_seq_cache_ptr, *m_linear_allocator_ptr);

    unsigned last_eval = 0;
    bool clean_first_evaluation = true;

    raster_picture new_picture;
    std::vector<const line_cache_result*> line_results(m_height);

    while (!m_gstate->m_finished.load()) {
        // Periodic diagnostics on very low progress to detect stalls
        static thread_local unsigned int slow_counter = 0;
        #ifdef THREAD_DEBUG
        if ((m_gstate->m_evaluations % 20000 == 0) && (m_gstate->m_evaluations > 0)) {
            if (++slow_counter % 3 == 0) {
                std::cout << "[DBG] Executor " << m_thread_id
                          << " heartbeat: evals=" << m_gstate->m_evaluations
                          << ", finished=" << m_gstate->m_finished.load()
                          << ", threads_active=" << m_gstate->m_threads_active.load()
                          << ", cache_used=" << GetCacheMemoryUsage()
                          << "/" << m_gstate->m_cache_size
                          << std::endl;
            }
        }
        #endif
        // Check for cache size with cooldown to avoid thrash
        static thread_local unsigned long long last_clear_evals = 0;
        static thread_local unsigned long long last_clear_time_ms = 0;
        auto now = std::chrono::steady_clock::now();
        auto now_ms = (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        if (m_linear_allocator_ptr->size() > m_cache_size) {
            bool cooldown_elapsed = (now_ms - last_clear_time_ms) > 1000; // 1s
            bool evals_progressed = (m_gstate->m_evaluations - last_clear_evals) > 5000;
            if (cooldown_elapsed && evals_progressed) {
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

                    // Update cooldown trackers
                    last_clear_time_ms = now_ms;
                    last_clear_evals = m_gstate->m_evaluations;
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

        // Update global state with the evaluation
        std::unique_lock<std::mutex> lock{ m_gstate->m_mutex };

        ++m_gstate->m_evaluations;

        // Check for termination condition first (respect max_evals only when > 0)
        if (((m_gstate->m_max_evals > 0) && (m_gstate->m_evaluations >= m_gstate->m_max_evals)) || m_gstate->m_finished.load()) {
            #ifdef THREAD_DEBUG
            std::cout << "[FIN] Executor thread " << m_thread_id << ": finish condition met (finished="
                      << m_gstate->m_finished.load() << ", max_evals=" << m_gstate->m_max_evals
                      << ", evals=" << m_gstate->m_evaluations << ")" << std::endl;
            #endif
            CTX_MARK_FINISHED((*m_gstate), "executor_finish_guard");
            break;
        }

        // The evaluation context now handles best solution tracking and DLAS acceptance
        bool accepted = m_gstate->ReportEvaluationResult(
            result, 
            &new_picture, 
            line_results, 
            m_sprites_memory, 
            m_mutator
        );

        if (m_best_result != m_gstate->m_best_result) {
            m_best_result = m_gstate->m_best_result;
            m_best_pic = m_gstate->m_best_pic;
            m_best_pic.recache_insns(*m_insn_seq_cache_ptr, *m_linear_allocator_ptr);
        }
    }

    // Decrement active thread count
    {
        std::unique_lock<std::mutex> lock{ m_gstate->m_mutex };
        --m_gstate->m_threads_active;
        
        // Notify all waiting threads if this was the last one
        if (m_gstate->m_threads_active <= 0) {
            m_gstate->m_condvar_update.notify_all();
        } else {
            m_gstate->m_condvar_update.notify_one();
        }
    }
}

distance_accum_t Executor::ExecuteRasterProgram(raster_picture* pic, const line_cache_result** results_array,
                                                dual_render_role_t dual_role,
                                                const std::vector<const line_cache_result*>* other_results)
{
    // Ensure any mutated lines have valid cache keys using this executor's caches.
    // The mutator deliberately leaves changed lines with cache_key == NULL so execution
    // can recache them into the per-executor allocator/cache to avoid cross-thread races.
    if (pic) {
        pic->recache_insns_if_needed(*m_insn_seq_cache_ptr, *m_linear_allocator_ptr);
    }

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
    distance_accum_t dual_total_error = 0; // accumulates blended base+flicker only in dual-aware mode

    // If dual-aware role is requested, prepare a flat other-frame pixel buffer and parameters
    m_dual_render_role = dual_role;
    if (m_dual_render_role != DUAL_NONE && other_results != nullptr && m_gstate && m_gstate->m_dual_mode) {
        // Build a lightweight rolling hash of the 'other' frame rows to decide cache reuse
        unsigned long long other_hash = 1469598103934665603ull; // FNV-1a offset
        for (int yy = 0; yy < (int)m_height; ++yy) {
            const line_cache_result* lr = ((*other_results)[yy]);
            if (lr && lr->color_row) {
                // Mix first and last 8 bytes only to avoid scanning entire width
                const unsigned char* row = lr->color_row;
                const int W = (int)m_width;
                int sample = (W >= 8) ? 8 : W;
                for (int i = 0; i < sample; ++i) { other_hash ^= row[i]; other_hash *= 1099511628211ull; }
                for (int i = W - sample; i < W; ++i) { if (i >= 0) { other_hash ^= row[i]; other_hash *= 1099511628211ull; } }
            } else {
                other_hash ^= 0xFF; other_hash *= 1099511628211ull;
            }
        }
        if (other_hash != m_dual_last_other_hash) {
            for (int yy = 0; yy < (int)m_height; ++yy) m_line_caches_dual[yy].clear();
            m_dual_last_other_hash = other_hash;
        }
        m_dual_other_pixels.clear();
        m_dual_transient_results.clear();
        m_dual_transient_results.resize(m_height);
        m_dual_other_rows.clear();
        m_dual_other_rows.resize(m_height, nullptr);
        // Snapshot generation counter for the other frame (to avoid redundant copies within a call)
        m_dual_gen_other_snapshot = (m_dual_render_role == DUAL_A) ? m_gstate->m_dual_generation_B.load(std::memory_order_relaxed)
                                                                   : m_gstate->m_dual_generation_A.load(std::memory_order_relaxed);
        // If 'other' generation changed since last call, clear dual caches
        if (m_dual_last_other_generation != m_dual_gen_other_snapshot) {
            for (int yy = 0; yy < (int)m_height; ++yy) m_line_caches_dual[yy].clear();
            m_dual_last_other_generation = m_dual_gen_other_snapshot;
        }
        // Grab pair tables (if precomputed)
        if (m_gstate->m_have_pair_tables && !m_gstate->m_pair_Ysum.empty()) {
            m_pair_Ysum = m_gstate->m_pair_Ysum.data();
            m_pair_Usum = m_gstate->m_pair_Usum.data();
            m_pair_Vsum = m_gstate->m_pair_Vsum.data();
            m_pair_dY = m_gstate->m_pair_dY.data();
            m_pair_dC = m_gstate->m_pair_dC.data();
        } else {
            m_pair_Ysum = m_pair_Usum = m_pair_Vsum = m_pair_dY = m_pair_dC = nullptr;
        }
        // Use per-line row pointers directly (no copy) valid for this call
        for (int yy = 0; yy < (int)m_height; ++yy) {
            const line_cache_result* lr = ((*other_results)[yy]);
            if (lr && lr->color_row) {
                m_dual_other_rows[yy] = lr->color_row;
            }
        }
        // Cache flicker weights/thresholds once for this call
        // Compute effective WL with optional ramp as in DLAS
        float wl = (float)m_gstate->m_flicker_luma_weight;
        if (m_gstate->m_blink_ramp_evals > 0) {
            double t = std::min<double>(1.0, (double)m_gstate->m_evaluations / (double)m_gstate->m_blink_ramp_evals);
            wl = (float)((1.0 - t) * m_gstate->m_flicker_luma_weight_initial + t * m_gstate->m_flicker_luma_weight);
        } else {
            if (m_gstate->m_evaluations < 50000ULL) wl = (float)(0.7 * wl);
        }
        m_dual_wl = wl;
        m_dual_wc = (float)m_gstate->m_flicker_chroma_weight;
        // Fixed hinge and exponents (simplified interface)
        m_dual_Tl = 3.0f;  // luma threshold
        m_dual_Tc = 8.0f;  // chroma threshold
        m_dual_pl = 2;     // luma exponent
        m_dual_pc = 2;     // chroma exponent
    } else {
        m_dual_render_role = DUAL_NONE;
        m_dual_other_pixels.clear();
        m_dual_other_rows.clear();
        m_dual_transient_results.clear();
        m_pair_Ysum = m_pair_Usum = m_pair_Vsum = m_pair_dY = m_pair_dC = nullptr;
    }

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
        // Dual: set fast pointer to the 'other' row for this y once per line
        if (m_dual_render_role != DUAL_NONE && !m_dual_other_rows.empty())
            m_dual_other_row_ptr = m_dual_other_rows[y];
        else
            m_dual_other_row_ptr = nullptr;

        line_cache_key lck;
        CaptureRegisterState(lck.entry_state);
        lck.insn_seq = rline.cache_key;

        const uint32_t lck_hash = lck.hash();

        // check line cache
        unsigned char* __restrict created_picture_row = &m_created_picture[y][0];
        unsigned char* __restrict created_picture_targets_row = &m_created_picture_targets[y][0];

        const line_cache_result* cached_line_result = nullptr;
        if (m_dual_render_role == DUAL_NONE) {
            cached_line_result = m_line_caches[y].find(lck, lck_hash);
        } else {
            cached_line_result = m_line_caches_dual[y].find(lck, lck_hash);
        }
        if (cached_line_result)
        {
            // sweet! cache hit!!
            results_array[y] = cached_line_result;
            ApplyRegisterState(cached_line_result->new_state);
            memcpy(m_sprites_memory[y], cached_line_result->sprite_data, sizeof m_sprites_memory[y]);
            shift_start_array_dirty = true;

            // Update LRU status for this line
            UpdateLRU(y);

            total_error += cached_line_result->line_error; // maintained for backward compatibility; ignored in dual return
            if (m_dual_render_role != DUAL_NONE) {
                dual_total_error += cached_line_result->line_error; // in dual cache, line_error stores blended base+flicker only
            }
            continue;
        }

        if (shift_start_array_dirty)
        {
            shift_start_array_dirty = false;
            ResetSpriteShiftStartArray();
        }

        const int rastinsncnt = (int)rline.instructions.size();
        const SRasterInstruction* __restrict rastinsns = (rastinsncnt > 0 && !rline.instructions.empty()) ? &rline.instructions[0] : nullptr;

        restart_line = false;
        ip = 0;
        cycle = 0;
        next_instr_offset = safe_screen_cycle_offset(cycle);

        // on new line clear sprite shifts and wait to be taken from mem_regs
        memset(m_sprite_shift_regs, 0, sizeof(m_sprite_shift_regs));
        memset(m_sprite_shift_emitted, 0, sizeof(m_sprite_shift_emitted));

        if (!rastinsncnt)
            next_instr_offset = 1000;

        const int picture_row_index = m_width * y;

        distance_accum_t total_line_error = 0;
        distance_accum_t dual_line_error = 0;

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
                if (!rastinsns || ip >= rastinsncnt) {
                    next_instr_offset = 1000;
                    break;
                }
                instr = &rastinsns[ip++];
                ExecuteInstruction(*instr, sprite_check_x, spriterow, total_line_error);

                cycle += GetInstructionCycles(*instr);
                next_instr_offset = safe_screen_cycle_offset(cycle);
                if (ip >= rastinsncnt)
                    next_instr_offset = 1000;
            }

            if ((unsigned)x < (unsigned)m_width)        // x>=0 && x<m_width
            {
                // put pixel closest per current mode (single-frame or dual-aware)
                distance_t closest_dist;
                e_target closest_register = FindClosestColorRegister(spriterow, picture_row_index + x, x, y, restart_line, closest_dist);
                total_line_error += closest_dist;
                if (m_dual_render_role != DUAL_NONE) dual_line_error += (distance_accum_t)closest_dist;
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
            if (m_dual_render_role != DUAL_NONE) dual_total_error += dual_line_error;

            if (m_dual_render_role == DUAL_NONE) {
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
            } else {
                // Add to dual cache keyed by code+regs; this is safe as long as 'other' snapshot is constant during call
                line_cache_result& result_state = m_line_caches_dual[y].insert(lck, lck_hash, *m_linear_allocator_ptr);
                // In dual cache, store only the blended base+flicker for fast reuse
                result_state.line_error = dual_line_error;
                CaptureRegisterState(result_state.new_state);
                result_state.color_row = (unsigned char*)m_linear_allocator_ptr->allocate(m_width);
                memcpy(result_state.color_row, created_picture_row, m_width);
                result_state.target_row = (unsigned char*)m_linear_allocator_ptr->allocate(m_width);
                memcpy(result_state.target_row, created_picture_targets_row, m_width);
                memcpy(result_state.sprite_data, m_sprites_memory[y], sizeof result_state.sprite_data);
                results_array[y] = &result_state;
            }
        }
    }

    if (m_dual_render_role != DUAL_NONE) return dual_total_error;
    return total_error;
}

e_target Executor::FindClosestColorRegister(sprites_row_memory_t& spriterow, int index, int x, int y, bool &restart_line, distance_t& best_error)
{
    distance_t distance;
    int sprite_bit = 0;  // Initialize to prevent uninitialized use
    int best_sprite_bit = 0;  // Initialize to prevent uninitialized use
    e_target result = E_COLBAK;
    distance_t min_distance = DISTANCE_MAX;
    bool sprite_covers_colbak = false;

    // Common invariants for this pixel
    const unsigned idx_pix = (index >= 0) ? (unsigned)index : 0u;
    unsigned char other_idx_for_pixel = 0;
    const bool dual_active = (m_dual_render_role != DUAL_NONE && m_gstate && m_gstate->m_dual_mode);
    if (dual_active) {
        unsigned char oi = m_dual_other_row_ptr ? m_dual_other_row_ptr[x] : m_dual_other_pixels[idx_pix];
        other_idx_for_pixel = (oi < 128) ? oi : 0;
    }
    const float Ty = (dual_active ? m_gstate->m_target_y[idx_pix] : 0.0f);
    const float Tu = (dual_active ? m_gstate->m_target_u[idx_pix] : 0.0f);
    const float Tv = (dual_active ? m_gstate->m_target_v[idx_pix] : 0.0f);

    // Additional input validation
    if (index < 0) {
        best_error = DISTANCE_MAX;
        return E_COLBAK;
    }
    
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
            
            // Ensure sprite_bit is within bounds
            if (sprite_bit < 0 || sprite_bit >= 8) {
                sprite_bit = 0;
            }
            
            assert(sprite_bit < 8);

            sprite_covers_colbak = true;

            // never shifted out remaining sprite pixels combine with sprite memory
            int sprite_leftover_pixel = 0;
            int sprite_leftover = x_offset + m_sprite_shift_emitted[temp - E_COLPM0];
            if (sprite_leftover < sprite_size)
            {
                int sprite_leftover_bit = sprite_leftover >> 2;
                
                // Ensure sprite_leftover_bit is within bounds
                if (sprite_leftover_bit >= 0 && sprite_leftover_bit < 8) {
                    sprite_leftover_pixel = spriterow[temp - E_COLPM0][sprite_leftover_bit];
                }
            }

            // Compute distance/cost for this sprite color
            double distance_d = (double)DISTANCE_MAX;
            unsigned char mem_reg_value = m_mem_regs[temp];
            unsigned char color_index = mem_reg_value / 2;

            if (dual_active) {
                if (color_index < 128) {
                    // Use precomputed pair tables when available to avoid sqrt/extra ops
                    const unsigned pairIdx = ((unsigned)color_index << 7) | (unsigned)other_idx_for_pixel;
                    float Ysum, Usum, Vsum, dY, dC;
                    if (m_pair_Ysum && m_pair_Usum && m_pair_Vsum && m_pair_dY && m_pair_dC) {
                        Ysum = m_pair_Ysum[pairIdx];
                        Usum = m_pair_Usum[pairIdx];
                        Vsum = m_pair_Vsum[pairIdx];
                        dY   = m_pair_dY[pairIdx];
                        dC   = m_pair_dC[pairIdx];
                    } else {
                        const float Yc = m_gstate->m_palette_y[color_index];
                        const float Uc = m_gstate->m_palette_u[color_index];
                        const float Vc = m_gstate->m_palette_v[color_index];
                        const float Yo = m_gstate->m_palette_y[other_idx_for_pixel];
                        const float Uo = m_gstate->m_palette_u[other_idx_for_pixel];
                        const float Vo = m_gstate->m_palette_v[other_idx_for_pixel];
                        Ysum = 0.5f * (Yc + Yo);
                        Usum = 0.5f * (Uc + Uo);
                        Vsum = 0.5f * (Vc + Vo);
                        dY   = fabsf(Yc - Yo);
                        dC   = sqrtf((Uc - Uo)*(Uc - Uo) + (Vc - Vo)*(Vc - Vo));
                    }
                    const float dy = Ysum - Ty; const float du = Usum - Tu; const float dv = Vsum - Tv;
                    double base = (double)(dy*dy + du*du + dv*dv);
                    float yl = dY - m_dual_Tl; if (yl < 0) yl = 0;
                    float yc = dC - m_dual_Tc; if (yc < 0) yc = 0;
                    double flick = 0.0;
                    if (m_dual_wl > 0.0f) { double t = yl; if (m_dual_pl == 2) t = t*t; else if (m_dual_pl == 3) t = t*t*t; else t = pow(t, (double)m_dual_pl); flick += (double)m_dual_wl * t; }
                    if (m_dual_wc > 0.0f) { double t = yc; if (m_dual_pc == 2) t = t*t; else if (m_dual_pc == 3) t = t*t*t; else t = pow(t, (double)m_dual_pc); flick += (double)m_dual_wc * t; }
                    distance_d = base + flick;
                }
            } else {
                // Single-frame: use precomputed error map
                distance = DISTANCE_MAX;
                if (color_index < 128 && m_picture_all_errors[color_index] != nullptr) {
                    const std::vector<distance_t>* error_vector = m_picture_all_errors[color_index];
                    if (index < (int)error_vector->size()) {
                        distance = (*error_vector)[index];
                    }
                }
                distance_d = (double)distance;
            }

            if (spriterow[temp - E_COLPM0][sprite_bit] || sprite_leftover_pixel)
            {
                // priority of sprites - next sprites are hidden below that one, so they are not processed
                best_sprite_bit = sprite_bit;
                result = (e_target)temp;
                min_distance = (distance_t)distance_d;
                break;
            }
            if (distance_d < (double)min_distance)
            {
                best_sprite_bit = sprite_bit;
                result = (e_target)temp;
                min_distance = (distance_t)distance_d;
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
        // Compute cost depending on mode
        double distance_d = (double)DISTANCE_MAX;
        unsigned char mem_reg_value = m_mem_regs[temp];
        unsigned char color_index = mem_reg_value / 2; // 0..127

        if (dual_active) {
            // Pair-aware selection against other frame pixel and source target
            if (color_index < 128) {
                const unsigned pairIdx = ((unsigned)color_index << 7) | (unsigned)other_idx_for_pixel;
                float Ysum, Usum, Vsum, dY, dC;
                if (m_pair_Ysum && m_pair_Usum && m_pair_Vsum && m_pair_dY && m_pair_dC) {
                    Ysum = m_pair_Ysum[pairIdx];
                    Usum = m_pair_Usum[pairIdx];
                    Vsum = m_pair_Vsum[pairIdx];
                    dY   = m_pair_dY[pairIdx];
                    dC   = m_pair_dC[pairIdx];
                } else {
                    const float Yc = m_gstate->m_palette_y[color_index];
                    const float Uc = m_gstate->m_palette_u[color_index];
                    const float Vc = m_gstate->m_palette_v[color_index];
                    const float Yo = m_gstate->m_palette_y[other_idx_for_pixel];
                    const float Uo = m_gstate->m_palette_u[other_idx_for_pixel];
                    const float Vo = m_gstate->m_palette_v[other_idx_for_pixel];
                    Ysum = 0.5f * (Yc + Yo);
                    Usum = 0.5f * (Uc + Uo);
                    Vsum = 0.5f * (Vc + Vo);
                    dY   = fabsf(Yc - Yo);
                    dC   = sqrtf((Uc - Uo)*(Uc - Uo) + (Vc - Vo)*(Vc - Vo));
                }
                const float dy = Ysum - Ty; const float du = Usum - Tu; const float dv = Vsum - Tv;
                double base = (double)(dy*dy + du*du + dv*dv);
                const float yl_base = dY - m_dual_Tl;
                const float yc_base = dC - m_dual_Tc;
                float yl = yl_base; if (yl < 0) yl = 0;
                float yc = yc_base; if (yc < 0) yc = 0;
                double flick = 0.0;
                if (m_dual_wl > 0.0f) {
                    double t = yl; if (m_dual_pl == 2) t = t*t; else if (m_dual_pl == 3) t = t*t*t; else t = pow(t, (double)m_dual_pl);
                    flick += (double)m_dual_wl * t;
                }
                if (m_dual_wc > 0.0f) {
                    double t = yc; if (m_dual_pc == 2) t = t*t; else if (m_dual_pc == 3) t = t*t*t; else t = pow(t, (double)m_dual_pc);
                    flick += (double)m_dual_wc * t;
                }
                distance_d = base + flick;
            }
        } else {
            // Single-frame original selection using precomputed error map to destination
            distance = DISTANCE_MAX;
            if (color_index < 128 && m_picture_all_errors[color_index] != nullptr) {
                const std::vector<distance_t>* error_vector = m_picture_all_errors[color_index];
                if (index < (int)error_vector->size()) {
                    distance = (*error_vector)[index];
                }
            }
            distance_d = (double)distance;
        }

        if (distance_d < (double)min_distance)
        {
            min_distance = (distance_t)distance_d;
            result = (e_target)temp;
        }
    }

    // the best color is in sprite, then set the proper bit of the sprite memory and then restart this line
    if (result >= E_COLPM0 && result <= E_COLPM3)
    {
        // Ensure best_sprite_bit is within bounds before accessing
        if (best_sprite_bit >= 0 && best_sprite_bit < 8) {
            // if PMG bit has been modified, then restart this line, because previous pixels of COLBAK may be covered
            if (spriterow[result - E_COLPM0][best_sprite_bit] == false)
            {
                restart_line = true;
                spriterow[result - E_COLPM0][best_sprite_bit] = true;
            }
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
    for (int y = 0; y < (int)m_height; ++y) {
        m_line_caches[y].clear();
        if ((int)m_line_caches_dual.size() > y) m_line_caches_dual[y].clear();
    }
    m_linear_allocator_ptr->clear();
    
    // Reset LRU tracking
    m_lru_lines.clear();
    m_lru_set.clear();
}

size_t Executor::GetCacheMemoryUsage() const
{
    return m_linear_allocator_ptr ? m_linear_allocator_ptr->size() : 0;
}

int Executor::Random(int range)
{
    if (range <= 0)
        return 0;
    // Use shared MT19937-based RNG for parity with legacy behavior
    return ::random(range);
}
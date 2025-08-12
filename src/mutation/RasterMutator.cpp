#include "RasterMutator.h"
#include "../optimization/EvaluationContext.h"
#include "../TargetPicture.h"
#include <algorithm>
#include <iostream>
#include <assert.h>
#include <cmath>

// Define mutation names array for display
const char* mutation_names[E_MUTATION_MAX] = {
    "Push back to prev",
    "Copy line to next one",
    "Swap line with prev one",
    "Add instruction",
    "Remove instruction",
    "Swap instruction",
    "Change target",
    "Change value",
    "Change value to color",
    // New
    "Shift instruction",
    "Insert timing NOP",
    "Adjust sprite pos",
    "Copy block from prev",
    "Line micro-optimize"
};

RasterMutator::RasterMutator(EvaluationContext* context, int thread_id)
    : m_width(context->m_width)
    , m_height(context->m_height)
    , m_picture(context->m_picture)
    , m_gstate(context)
    , m_thread_id(thread_id)
    , m_currently_mutated_y(0)
    , m_randseed(0)
{
    memset(m_current_mutations, 0, sizeof(m_current_mutations));
}

void RasterMutator::Init(unsigned long long seed)
{
    m_randseed = seed;
    ResetStats();
}

void RasterMutator::ResetStats()
{
    m_stats = MutationStats{};
    memset(m_current_mutations, 0, sizeof(m_current_mutations));
}

int RasterMutator::Random(int range)
{
    if (range <= 0)
        return 0;

    // XorShift algorithm - faster than MT19937
    m_randseed ^= m_randseed << 13;
    m_randseed ^= m_randseed >> 17;
    m_randseed ^= m_randseed << 5;

    // Use rejection sampling for unbiased distribution
    uint32_t scaled = (uint32_t)(m_randseed & 0x7FFFFFFF) % range;
    return (int)scaled;
}

int RasterMutator::SelectMutation()
{
    // Calculate weights based on success history
    double weights[E_MUTATION_MAX];
    double total_weight = 0.0;

    for (int i = 0; i < E_MUTATION_MAX; i++) {
        // Use success rate if we have enough samples, otherwise use default weight
        double success_rate = 0.1;  // Default weight

        // Only calculate success rate if we have samples and attempt_count is non-zero
        if (m_stats.attempt_count[i] > 10 && m_stats.attempt_count[i] != 0) {
            success_rate = (double)m_stats.success_count[i] / m_stats.attempt_count[i];
        }

        // Balance exploration vs exploitation - never let probability go to zero
        weights[i] = 0.1 + 0.9 * success_rate;
        total_weight += weights[i];
    }

    // Ensure total_weight is not zero
    if (total_weight <= 0.0) {
        total_weight = 1.0;  // Set a default to prevent division by zero
    }

    // Select based on weights using a fair distribution
    double r = (double)Random(10000) / 10000.0 * total_weight;
    double sum = 0;

    for (int i = 0; i < E_MUTATION_MAX; i++) {
        sum += weights[i];
        if (r <= sum) return i;
    }

    // Fallback (should rarely happen)
    return Random(E_MUTATION_MAX);
}

void RasterMutator::MutateProgram(raster_picture* pic)
{
    memset(m_current_mutations, 0, sizeof(m_current_mutations));

    // Determine mutation strategy - regional or global
    bool use_regional = m_gstate->m_use_regional_mutation;
    int new_line = m_currently_mutated_y;

    // Thread synchronization needed when lines could be accessed by multiple threads
    std::unique_lock<std::mutex> lock(m_gstate->m_mutex, std::defer_lock);

    if (use_regional) {
        // Regional mutation strategy - each thread focuses on its own region
        int thread_count = std::max(1, m_gstate->m_thread_count);  // Ensure we don't divide by zero

        int lines_per_thread = thread_count > 0 ? m_height / thread_count : m_height;
        int region_start = m_thread_id * lines_per_thread;
        int region_end = (m_thread_id == thread_count - 1) ?
            m_height : region_start + lines_per_thread;

        // Acquire lock only if we might go outside our region
        if (Random(100) >= 80) {
            lock.lock();
            new_line = Random(m_height);
        }
        else if (region_end > region_start) {
            // We're staying in our region - no lock needed
            new_line = region_start + Random(region_end - region_start);
        }
        else {
            // Fallback if region is empty - use thread ID as line
            new_line = m_thread_id % m_height;
        }
    }
    else {
        // Global mutation strategy - all threads can mutate any line
        // Need to lock when modifying global state
        lock.lock();

        if (new_line >= static_cast<int>(pic->raster_lines.size())) {
            new_line = 0;
        }
        else if (new_line < 0) {
            new_line = static_cast<int>(pic->raster_lines.size()) - 1;
        }

        // Random line changes
        if (Random(5) == 0) {
            new_line = Random(m_height);
        }
    }

    // Ensure new_line is within valid bounds
    if (new_line < 0 || new_line >= static_cast<int>(m_height)) {
        new_line = Random(m_height);
    }

    // Update the current line to mutate
    m_currently_mutated_y = new_line;

    // If we acquired a lock, release it after updating m_currently_mutated_y
    if (lock.owns_lock()) {
        lock.unlock();
    }

    // Batch memory register mutations
    if (Random(10) == 0) // mutate random init mem reg
    {
        int c = 1;
        if (Random(2))
            c *= -1;
        if (Random(2))
            c *= 16;

        int targ;
        do {
            targ = Random(E_TARGET_MAX);
        } while (targ == E_COLBAK);

        // Lock for memory register mutations
        std::unique_lock<std::mutex> mem_lock(m_gstate->m_mutex);
        pic->mem_regs_init[targ] += c;
        mem_lock.unlock();
    }

    // Ensure current line is in bounds
    if (m_currently_mutated_y >= 0 && m_currently_mutated_y < static_cast<int>(pic->raster_lines.size())) {
        raster_line& current_line = pic->raster_lines[m_currently_mutated_y];
        MutateLine(current_line, *pic);
    }

    // Batch mutations
    if (Random(20) == 0)
    {
        for (int t = 0; t < 10; ++t)
        {
            bool need_lock = false;
            new_line = m_currently_mutated_y;

            if (use_regional) {
                int thread_count = std::max(1, m_gstate->m_thread_count);  // Ensure we don't divide by zero

                int lines_per_thread = thread_count > 0 ? m_height / thread_count : m_height;
                int region_start = m_thread_id * lines_per_thread;
                int region_end = (m_thread_id == thread_count - 1) ?
                    m_height : region_start + lines_per_thread;

                if (Random(100) >= 80) {
                    // Going outside our region - need lock
                    need_lock = true;
                    new_line = Random(m_height);
                }
                else if (region_end > region_start) {
                    // Determine new line within our region
                    if (Random(2) && new_line > region_start)
                        new_line = new_line - 1;
                    else if (new_line < region_end - 1)
                        new_line = new_line + 1;
                    else
                        new_line = region_start + Random(region_end - region_start);
                }
                else {
                    // Fallback if region is empty
                    need_lock = true;
                    new_line = Random(m_height);
                }
            }
            else {
                // Global mutation - always need lock
                need_lock = true;

                if (Random(2))
                    new_line = new_line - 1;
                else
                    new_line = new_line + 1;

                // Ensure within bounds
                if (new_line < 0)
                    new_line = static_cast<int>(m_height) - 1;
                if (new_line >= static_cast<int>(m_height))
                    new_line = 0;
            }

            // Acquire lock if needed
            std::unique_lock<std::mutex> batch_lock(m_gstate->m_mutex, std::defer_lock);
            if (need_lock) {
                batch_lock.lock();
            }

            // Ensure new_line is within valid bounds
            if (new_line < 0 || new_line >= static_cast<int>(m_height)) {
                new_line = Random(m_height);
            }

            m_currently_mutated_y = new_line;

            // Release lock before mutating if we acquired it
            if (batch_lock.owns_lock()) {
                batch_lock.unlock();
            }

            // Ensure current line is in bounds
            if (m_currently_mutated_y >= 0 && m_currently_mutated_y < static_cast<int>(pic->raster_lines.size())) {
                raster_line& current_line = pic->raster_lines[m_currently_mutated_y];
                MutateLine(current_line, *pic);
            }
        }
    }

    // Do not recache into the shared context here. Leave cache_key == NULL
    // so the executor can safely recache into its per-thread allocator/cache
    // inside ExecuteRasterProgram(). This avoids double work and global mutex contention.
}

void RasterMutator::MutateOnce(raster_line& prog, raster_picture& pic)
{
    int i1, i2, c, x;

    if (prog.instructions.empty())
        return;

    i1 = Random(static_cast<int>(prog.instructions.size()));
    i2 = i1;
    if (prog.instructions.size() > 2) {
        do {
            i2 = Random(static_cast<int>(prog.instructions.size()));
        } while (i1 == i2);
    }

    SRasterInstruction temp;

    // Use smart selection instead of random
    int mutation = SelectMutation();
    m_stats.attempt_count[mutation]++;

    switch (mutation)
    {
    case E_MUTATION_COPY_LINE_TO_NEXT_ONE:
        if (m_currently_mutated_y < (int)m_height - 1)
        {
            int next_y = m_currently_mutated_y + 1;
            raster_line& next_line = pic.raster_lines[next_y];
            prog = next_line;
            m_current_mutations[E_MUTATION_COPY_LINE_TO_NEXT_ONE]++;
            m_stats.success_count[mutation]++;
            break;
        }
        // fallthrough if not possible
    case E_MUTATION_PUSH_BACK_TO_PREV:
        if (m_currently_mutated_y > 0)
        {
            int prev_y = m_currently_mutated_y - 1;
            raster_line& prev_line = pic.raster_lines[prev_y];
            c = GetInstructionCycles(prog.instructions[i1]);
            if (prev_line.cycles + c < free_cycles)
            {
                // add it to prev line but do not remove it from the current
                prev_line.cycles += c;
                prev_line.instructions.push_back(prog.instructions[i1]);
                prev_line.cache_key = NULL;
                m_current_mutations[E_MUTATION_PUSH_BACK_TO_PREV]++;
                m_stats.success_count[mutation]++;
                break;
            }
        }
        // fallthrough if not possible
    case E_MUTATION_SWAP_LINE_WITH_PREV_ONE:
        if (m_currently_mutated_y > 0)
        {
            int prev_y = m_currently_mutated_y - 1;
            raster_line& prev_line = pic.raster_lines[prev_y];
            prog.swap(prev_line);
            m_current_mutations[E_MUTATION_SWAP_LINE_WITH_PREV_ONE]++;
            m_stats.success_count[mutation]++;
            break;
        }
        // fallthrough if not possible
    case E_MUTATION_ADD_INSTRUCTION:
        if (prog.cycles + 2 < free_cycles)
        {
            if (prog.cycles + 4 < free_cycles && Random(2)) // 4 cycles instructions
            {
                temp.loose.instruction = (e_raster_instruction)(E_RASTER_STA + Random(3));
                temp.loose.value = (Random(128) * 2);
                temp.loose.target = (e_target)(Random(E_TARGET_MAX));

                // More efficient insert - add at end then swap to position
                prog.instructions.push_back(temp);
                for (int i = static_cast<int>(prog.instructions.size()) - 1; i > i1; --i) {
                    std::swap(prog.instructions[i], prog.instructions[i - 1]);
                }

                prog.cache_key = NULL;
                prog.cycles += 4;
            }
            else
            {
                temp.loose.instruction = (e_raster_instruction)(E_RASTER_LDA + Random(4));
                // Seed value: prefer complementary pick in dual mode
                bool seededByComplement = false;
                if (m_gstate->m_dual_mode && m_currently_mutated_y >= 0 && m_currently_mutated_y < (int)m_height) {
                    unsigned char compVal;
                    int px = -1;
                    // Estimate a pixel x similarly to change-to-color path
                    if (!prog.instructions.empty()) {
                        px = ComputePixelXForInstructionIndex(prog, i1);
                    }
                    if (px < 0 || px >= (int)m_width) px = Random(m_width);
                    if (TryComplementaryPick(m_currently_mutated_y, px, compVal)) {
                        temp.loose.value = compVal;
                        m_gstate->m_stat_dualSeedAdd++;
                        seededByComplement = true;
                        m_used_seed_add = true;
                    }
                }
                if (!seededByComplement) {
                    if (Random(2))
                        temp.loose.value = (Random(128) * 2);
                    else {
                        const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
                        if (!possible_colors.empty()) {
                            temp.loose.value = possible_colors[Random((int)possible_colors.size())];
                        } else {
                            temp.loose.value = (Random(128) * 2);
                        }
                    }
                }

                temp.loose.target = (e_target)(Random(E_TARGET_MAX));

                // Only sample target picture color if we didn't already seed from complementary logic
                if (!seededByComplement) {
                    if (m_currently_mutated_y >= 0 && m_currently_mutated_y < static_cast<int>(m_height) && m_picture != nullptr) {
                        const screen_line& line = m_picture[m_currently_mutated_y];
                        if (line.size() > 0) {
                            c = Random((int)line.size());
                            temp.loose.value = FindAtariColorIndex(line[c]) * 2;
                        } else {
                            temp.loose.value = (Random(128) * 2);
                        }
                    } else {
                        temp.loose.value = (Random(128) * 2);
                    }
                }

                // More efficient insert
                prog.instructions.push_back(temp);
                for (int i = static_cast<int>(prog.instructions.size()) - 1; i > i1; --i) {
                    std::swap(prog.instructions[i], prog.instructions[i - 1]);
                }

                prog.cache_key = NULL;
                prog.cycles += 2;
            }
            m_current_mutations[E_MUTATION_ADD_INSTRUCTION]++;
            m_stats.success_count[mutation]++;
            break;
        }
        // fallthrough if not possible
    case E_MUTATION_REMOVE_INSTRUCTION:
        if (prog.cycles > 4)
        {
            c = GetInstructionCycles(prog.instructions[i1]);
            if (prog.cycles - c > 0)
            {
                prog.cycles -= c;

                // FIXED: Use erase to maintain instruction order
                prog.instructions.erase(prog.instructions.begin() + i1);

                prog.cache_key = NULL;
                assert(prog.cycles > 0);
                m_current_mutations[E_MUTATION_REMOVE_INSTRUCTION]++;
                m_stats.success_count[mutation]++;
                break;
            }
        }
        // fallthrough if not possible
    case E_MUTATION_SWAP_INSTRUCTION:
            if (prog.instructions.size() > 2)
        {
            temp = prog.instructions[i1];
            prog.instructions[i1] = prog.instructions[i2];
            prog.instructions[i2] = temp;
            prog.cache_key = NULL;
            m_current_mutations[E_MUTATION_SWAP_INSTRUCTION]++;
            m_stats.success_count[mutation]++;
            break;
        }
        // fallthrough if not possible
    case E_MUTATION_CHANGE_TARGET:
        prog.instructions[i1].loose.target = (e_target)(Random(E_TARGET_MAX));
        prog.cache_key = NULL;
        m_current_mutations[E_MUTATION_CHANGE_TARGET]++;
        m_stats.success_count[mutation]++;
        break;
    case E_MUTATION_CHANGE_VALUE_TO_COLOR:
        if ((prog.instructions[i1].loose.target >= E_HPOSP0 && prog.instructions[i1].loose.target <= E_HPOSP3))
        {
            x = pic.mem_regs_init[prog.instructions[i1].loose.target] - sprite_screen_color_cycle_start;
            x += Random(sprite_size);
        }
        else
        {
            c = 0;
            // find color in the next raster column
            for (x = 0; x < i1 - 1; ++x)
            {
                if (prog.instructions[x].loose.instruction <= E_RASTER_NOP)
                    c += 2;
                else
                    c += 4; // cycles
            }
            while (Random(5) == 0)
                ++c;

            if (c < 0) c = 0;
            if (c >= free_cycles) c = free_cycles - 1;
            if (c >= CYCLES_MAX) c = CYCLES_MAX - 1;
            x = safe_screen_cycle_offset(c);
            {
                int len = safe_screen_cycle_length(c);
                if (len < 0) len = 0;
                x += Random(len > 0 ? len : 1);
            }
        }
        if (x < 0 || x >= (int)m_width)
            x = Random(m_width);
        i2 = m_currently_mutated_y;
        // check color in next lines
        while (Random(5) == 0 && i2 + 1 < (int)m_height)
            ++i2;

        // Dual-aware complementary value selection
        if (m_gstate->m_dual_mode) {
            unsigned char compVal;
            if (TryComplementaryPick(i2, x, compVal)) {
                prog.instructions[i1].loose.value = compVal;
                m_used_complementary_pick = true;
            } else {
                // Fallback to original behavior (use source picture color)
                if (i2 >= 0 && i2 < static_cast<int>(m_height) && m_picture != nullptr) {
                    const screen_line& line = m_picture[i2];
                    if (x >= 0 && x < (int)line.size()) {
                        prog.instructions[i1].loose.value = FindAtariColorIndex(line[x]) * 2;
                    } else {
                        prog.instructions[i1].loose.value = (Random(128) * 2);
                    }
                } else {
                    prog.instructions[i1].loose.value = (Random(128) * 2);
                }
            }
        } else {
            // Single-frame behavior (original)
            if (i2 >= 0 && i2 < static_cast<int>(m_height) && m_picture != nullptr) {
                const screen_line& line = m_picture[i2];
                if (x >= 0 && x < (int)line.size()) {
                    prog.instructions[i1].loose.value = FindAtariColorIndex(line[x]) * 2;
                }
                else {
                    prog.instructions[i1].loose.value = (Random(128) * 2);
                }
            }
            else {
                prog.instructions[i1].loose.value = (Random(128) * 2);
            }
        }

        prog.cache_key = NULL;
        m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
        m_stats.success_count[mutation]++;
        break;
    case E_MUTATION_CHANGE_VALUE:
        if (m_gstate->m_dual_mode && Random(2) == 0)
        {
            // 50%: complementary recompute for color targets
            unsigned char compVal;
            int px = ComputePixelXForInstructionIndex(prog, i1);
            if (px < 0 || px >= (int)m_width) px = Random(m_width);
            if (TryComplementaryPick(m_currently_mutated_y, px, compVal)) {
                prog.instructions[i1].loose.value = compVal;
                m_used_complementary_pick = true;
            } else {
                // fallback to small random change
                int dc = (Random(2) ? 1 : -1) * (Random(2) ? 1 : 16);
                prog.instructions[i1].loose.value += dc;
            }
        }
        else if (Random(10) == 0)
        {
            if (Random(2))
                prog.instructions[i1].loose.value = (Random(128) * 2);
            else
            {
                const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
                if (!possible_colors.empty()) {
                    prog.instructions[i1].loose.value = possible_colors[Random((int)possible_colors.size())];
                }
                else {
                    prog.instructions[i1].loose.value = (Random(128) * 2);
                }
            }
        }
        else
        {
            c = 1;
            if (Random(2))
                c *= -1;
            if (Random(2))
                c *= 16;
            prog.instructions[i1].loose.value += c;
        }
        prog.cache_key = NULL;
        m_current_mutations[E_MUTATION_CHANGE_VALUE]++;
        m_stats.success_count[mutation]++;
        break;
    case E_MUTATION_SHIFT_INSTRUCTION:
        if (prog.instructions.size() > 1)
        {
            int dir = (Random(2) ? 1 : -1);
            int j = i1 + dir;
            if (j >= 0 && j < static_cast<int>(prog.instructions.size()))
            {
                std::swap(prog.instructions[i1], prog.instructions[j]);
                prog.cache_key = NULL;
                m_current_mutations[E_MUTATION_SHIFT_INSTRUCTION]++;
                m_stats.success_count[mutation]++;
                break;
            }
        }
        // fallthrough if not possible
    case E_MUTATION_INSERT_TIMING_NOP:
        if (prog.cycles + 2 < free_cycles)
        {
            SRasterInstruction nop;
            nop.loose.instruction = E_RASTER_NOP;
            nop.loose.target = E_COLBAK;
            nop.loose.value = 0;
            // Prefer to insert near a store to shift its effect; try to find any store nearby
            int insert_at = i1;
            for (int k = std::max(0, i1 - 2); k <= std::min(static_cast<int>(prog.instructions.size()) - 1, i1 + 2); ++k)
            {
                unsigned char ins = static_cast<unsigned char>(prog.instructions[k].loose.instruction);
                if (ins >= E_RASTER_STA) { insert_at = k; break; }
            }
            // Insert before or after randomly
            if (Random(2) && insert_at < (int)prog.instructions.size()) insert_at++;
            prog.instructions.push_back(nop);
            for (int p = (int)prog.instructions.size() - 1; p > insert_at; --p) std::swap(prog.instructions[p], prog.instructions[p - 1]);
            prog.cycles += 2;
            prog.cache_key = NULL;
            m_current_mutations[E_MUTATION_INSERT_TIMING_NOP]++;
            m_stats.success_count[mutation]++;
            break;
        }
        // fallthrough if not possible
    case E_MUTATION_ADJUST_SPRITE_POS:
        {
            // Find a store to HPOSPx on this line
            int store_idx = -1;
            for (int k = 0; k < static_cast<int>(prog.instructions.size()); ++k)
            {
                const auto &ins = prog.instructions[k];
                if (ins.loose.instruction >= E_RASTER_STA && ins.loose.target >= E_HPOSP0 && ins.loose.target <= E_HPOSP3)
                { store_idx = k; break; }
            }
            if (store_idx >= 0)
            {
                const auto &store = prog.instructions[store_idx];
                int reg_idx = store.loose.instruction - E_RASTER_STA; // 0=A,1=X,2=Y
                // search backwards for corresponding load
                for (int k = store_idx - 1; k >= 0; --k)
                {
                    auto &cand = prog.instructions[k];
                    if (cand.loose.instruction == static_cast<unsigned>(E_RASTER_LDA + reg_idx))
                    {
                        // tweak by small delta (keep in 0..255)
                        int delta = (Random(2) ? 1 : -1) * (1 + Random(3));
                        int nv = (int)cand.loose.value + delta;
                        if (nv < 0) nv = 0; if (nv > 255) nv = 255;
                        cand.loose.value = (unsigned char)nv;
                        prog.cache_key = NULL;
                        m_current_mutations[E_MUTATION_ADJUST_SPRITE_POS]++;
                        m_stats.success_count[mutation]++;
                        goto adjust_sprite_done;
                    }
                }
                // no load found; try to insert LDA/STA pair if space allows
                if (prog.cycles + 6 < free_cycles)
                {
                    SRasterInstruction ld, st;
                    int regPick = Random(3); // 0=A,1=X,2=Y
                    ld.loose.instruction = (e_raster_instruction)(E_RASTER_LDA + regPick);
                    // value: pick current mem init or random pos near screen
                    int base = sprite_screen_color_cycle_start + Random(m_width);
                    if (base < 0) base = 0; if (base > 255) base = 255;
                    ld.loose.value = (unsigned char)base;
                    ld.loose.target = E_COLBAK;
                    st.loose.instruction = (e_raster_instruction)(E_RASTER_STA + regPick);
                    st.loose.value = 0;
                    st.loose.target = prog.instructions[store_idx].loose.target; // same sprite target
                    // insert ld,st before existing store to avoid changing much
                    int insert_at = store_idx;
                    prog.instructions.push_back(st);
                    prog.instructions.push_back(ld);
                    // move them into place: first push ld then st in order
                    int end = static_cast<int>(prog.instructions.size());
                    // move last-1 (ld) to position insert_at
                    for (int p = end - 2; p > insert_at; --p) std::swap(prog.instructions[p], prog.instructions[p - 1]);
                    // after ld moved, store is at insert_at+1, move last (st) to insert_at+1
                    for (int p = end - 1; p > insert_at + 1; --p) std::swap(prog.instructions[p], prog.instructions[p - 1]);
                    prog.cycles += 6;
                    prog.cache_key = NULL;
                    m_current_mutations[E_MUTATION_ADJUST_SPRITE_POS]++;
                    m_stats.success_count[mutation]++;
                }
            }
adjust_sprite_done:
            break;
        }
    case E_MUTATION_COPY_BLOCK_FROM_PREV:
        if (m_currently_mutated_y > 0)
        {
            raster_line &prev = pic.raster_lines[m_currently_mutated_y - 1];
            if (!prev.instructions.empty())
            {
            int max_len = std::min(3, static_cast<int>(prev.instructions.size()));
                int len = 1 + Random(max_len);
            int start = Random(static_cast<int>(prev.instructions.size()));
            if (start + len > static_cast<int>(prev.instructions.size())) start = std::max(0, static_cast<int>(prev.instructions.size()) - len);
                // compute cycles
                int add_cycles = 0;
                for (int t = 0; t < len; ++t) add_cycles += GetInstructionCycles(prev.instructions[start + t]);
                if (prog.cycles + add_cycles < free_cycles)
                {
                    int insert_at = static_cast<int>(prog.instructions.size());
                    if (!prog.instructions.empty()) insert_at = Random(static_cast<int>(prog.instructions.size()));
                    // append then swap into place in original order
                    for (int t = 0; t < len; ++t) prog.instructions.push_back(prev.instructions[start + t]);
                    for (int t = 0; t < len; ++t)
                    {
                        int from = static_cast<int>(prog.instructions.size()) - len + t;
                        for (int p = from; p > insert_at + t; --p) std::swap(prog.instructions[p], prog.instructions[p - 1]);
                    }
                    prog.cycles += add_cycles;
                    prog.cache_key = NULL;
                    m_current_mutations[E_MUTATION_COPY_BLOCK_FROM_PREV]++;
                    m_stats.success_count[mutation]++;
                    break;
                }
            }
        }
        // fallthrough if not possible
    case E_MUTATION_LINE_OPTIMIZE:
        if (!prog.instructions.empty())
        {
            int last_load_idx[3] = { -1, -1, -1 };
            for (int k = 0; k < static_cast<int>(prog.instructions.size()); ++k)
            {
                unsigned char ins = static_cast<unsigned char>(prog.instructions[k].loose.instruction);
                if (ins <= E_RASTER_LDY)
                {
                    int r = ins - E_RASTER_LDA; // 0=A,1=X,2=Y
                    if (r >= 0 && r < 3 && last_load_idx[r] != -1)
                    {
                        // previous load to same register not used; neutralize it by NOP (keep timing)
                        prog.instructions[last_load_idx[r]].loose.instruction = E_RASTER_NOP;
                    }
                    if (r >= 0 && r < 3) last_load_idx[r] = k;
                }
                else if (ins >= E_RASTER_STA)
                {
                    int r = ins - E_RASTER_STA; // 0=A,1=X,2=Y
                    if (r >= 0 && r < 3) last_load_idx[r] = -1; // register was consumed
                }
            }
            prog.cache_key = NULL;
            m_current_mutations[E_MUTATION_LINE_OPTIMIZE]++;
            m_stats.success_count[mutation]++;
        }
        break;
    default:
        // Should not happen
        break;
    }
}

// Compute an approximate pixel x-position for a given instruction index on the line
int RasterMutator::ComputePixelXForInstructionIndex(const raster_line& prog, int insnIndex)
{
    int cycles = 0;
    for (int k = 0; k < insnIndex && k < (int)prog.instructions.size(); ++k) {
        const auto& ins = prog.instructions[k];
        cycles += (ins.loose.instruction <= E_RASTER_NOP) ? 2 : 4;
    }
    if (cycles < 0) cycles = 0;
    if (cycles >= free_cycles) cycles = free_cycles - 1;
    if (cycles >= CYCLES_MAX) cycles = CYCLES_MAX - 1;
    int x = safe_screen_cycle_offset(cycles);
    int len = safe_screen_cycle_length(cycles);
    if (len < 0) len = 0;
    if (len > 0) x += Random(len);
    return x;
}

// Try to choose a complementary palette value (encoded as value*2) at (y,x) against the other frame
bool RasterMutator::TryComplementaryPick(int y, int x, unsigned char& outValue)
{
    if (!m_gstate->m_dual_mode) return false;
    if (y < 0 || y >= (int)m_height || x < 0 || x >= (int)m_width) return false;

    // Access other-frame created picture safely (use snapshot in context; may be slightly stale but okay)
    unsigned char a = 0;
    bool haveOther = false;
    {
        std::unique_lock<std::mutex> lock(m_gstate->m_mutex);
        if (m_is_mutating_B) {
            if (y < (int)m_gstate->m_created_picture.size() && x < (int)m_gstate->m_created_picture[y].size()) {
                a = m_gstate->m_created_picture[y][x];
                haveOther = true;
            }
        } else {
            if (y < (int)m_gstate->m_created_picture_B.size() && x < (int)m_gstate->m_created_picture_B[y].size()) {
                a = m_gstate->m_created_picture_B[y][x];
                haveOther = true;
            }
        }
    }
    if (!haveOther) return false;

    const unsigned idx = (unsigned)y * m_width + (unsigned)x;
    const float Ya = m_gstate->m_palette_y[a];
    const float Ua = m_gstate->m_palette_u[a];
    const float Va = m_gstate->m_palette_v[a];
    const float ty = m_gstate->m_target_y[idx];
    const float tu = m_gstate->m_target_u[idx];
    const float tv = m_gstate->m_target_v[idx];
    // Effective WL with ramp
    float wl = (float)m_gstate->m_flicker_luma_weight;
    if (m_gstate->m_blink_ramp_evals > 0) {
        double t = std::min<double>(1.0, (double)m_gstate->m_evaluations / (double)m_gstate->m_blink_ramp_evals);
        wl = (float)((1.0 - t) * m_gstate->m_flicker_luma_weight_initial + t * m_gstate->m_flicker_luma_weight);
    }
    const float wc = (float)m_gstate->m_flicker_chroma_weight;
    const float Tl = (float)m_gstate->m_flicker_luma_thresh;
    const float Tc = (float)m_gstate->m_flicker_chroma_thresh;
    const int   pl = m_gstate->m_flicker_exp_luma;
    const int   pc = m_gstate->m_flicker_exp_chroma;

    const std::vector<unsigned char>& candidates = m_gstate->m_possible_colors_for_each_line[y];
    double bestScore = 1e100;
    int bestB = -1;

    auto evalB = [&](int bIdx) {
        const float Yb = m_gstate->m_palette_y[bIdx];
        const float Ub = m_gstate->m_palette_u[bIdx];
        const float Vb = m_gstate->m_palette_v[bIdx];
        const float Ybl = 0.5f * (Ya + Yb);
        const float Ubl = 0.5f * (Ua + Ub);
        const float Vbl = 0.5f * (Va + Vb);
        const float dy = Ybl - ty;
        const float du = Ubl - tu;
        const float dv = Vbl - tv;
        double base = (double)(dy * dy + du * du + dv * dv);
        const float dY = fabsf(Ya - Yb);
        const float dC = sqrtf((Ua - Ub) * (Ua - Ub) + (Va - Vb) * (Va - Vb));
        float yl = dY - Tl; if (yl < 0) yl = 0;
        float yc = dC - Tc; if (yc < 0) yc = 0;
        double flick = 0.0;
        if (wl > 0) { double t = yl; if (pl == 2) t = t * t; else if (pl == 3) t = t * t * t; else t = pow(t, (double)pl); flick += wl * t; }
        if (wc > 0) { double t = yc; if (pc == 2) t = t * t; else if (pc == 3) t = t * t * t; else t = pow(t, (double)pc); flick += wc * t; }
        return base + flick;
    };

    // Dual mode: always include full-palette exploration in addition to any per-line candidates
    // Sample from candidate set (if available)
    if (!candidates.empty()) {
        int limit = (int)std::min<size_t>(24, candidates.size());
        for (int i = 0; i < limit; ++i) {
            int idxC = candidates[Random((int)candidates.size())] / 2; // ensure 0..127
            if (idxC < 0) idxC = 0; if (idxC > 127) idxC = 127;
            double s = evalB(idxC);
            if (s < bestScore) { bestScore = s; bestB = idxC; }
        }
    }
    // Always sample a random subset from the full palette to escape per-line color traps
    {
        int trials = 16; // modest extra exploration
        for (int t = 0; t < trials; ++t) {
            int idxC = Random(128);
            double s = evalB(idxC);
            if (s < bestScore) { bestScore = s; bestB = idxC; }
        }
    }

    if (bestB >= 0) {
        outValue = (unsigned char)(bestB * 2);
        // Stats: count dual complement value
        m_gstate->m_stat_dualComplementValue++;
        return true;
    }
    return false;
}

void RasterMutator::BatchMutateLine(raster_line& prog, raster_picture& pic, int count)
{
    for (int i = 0; i < count; i++) {
        MutateOnce(prog, pic);
    }
    prog.rehash();
    prog.cache_key = NULL; // Explicitly mark for recaching
}

void RasterMutator::MutateLine(raster_line& prog, raster_picture& pic)
{
    // Apply a batch of mutations based on line complexity
    int mutation_count = std::min(3 + (int)(prog.instructions.size() / 5), 8);

    // Call the batch mutation method instead of doing individual mutations
    BatchMutateLine(prog, pic, mutation_count);
}
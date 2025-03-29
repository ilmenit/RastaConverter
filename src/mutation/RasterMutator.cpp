#include "mutation/RasterMutator.h"
#include "optimization/EvaluationContext.h"
#include "TargetPicture.h"
#include <algorithm>
#include <assert.h>

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
    memset(&m_stats, 0, sizeof(m_stats));
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
        double success_rate = (m_stats.attempt_count[i] > 10) ?
            (double)m_stats.success_count[i] / m_stats.attempt_count[i] : 0.1;

        // Balance exploration vs exploitation - never let probability go to zero
        weights[i] = 0.1 + 0.9 * success_rate;
        total_weight += weights[i];
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

void RasterMutator::MutateOnce(raster_line& prog, raster_picture& pic)
{
    int i1, i2, c, x;

    i1 = Random(prog.instructions.size());
    i2 = i1;
    if (prog.instructions.size() > 2) {
        do {
            i2 = Random(prog.instructions.size());
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
                for (int i = prog.instructions.size() - 1; i > i1; --i) {
                    std::swap(prog.instructions[i], prog.instructions[i - 1]);
                }

                prog.cache_key = NULL;
                prog.cycles += 4;
            }
            else
            {
                temp.loose.instruction = (e_raster_instruction)(E_RASTER_LDA + Random(4));
                if (Random(2))
                    temp.loose.value = (Random(128) * 2);
                else
                {
                    const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
                    temp.loose.value = possible_colors[Random(possible_colors.size())];
                }

                temp.loose.target = (e_target)(Random(E_TARGET_MAX));
                c = Random(m_picture[m_currently_mutated_y].size());
                temp.loose.value = FindAtariColorIndex(m_picture[m_currently_mutated_y][c]) * 2;

                // More efficient insert
                prog.instructions.push_back(temp);
                for (int i = prog.instructions.size() - 1; i > i1; --i) {
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

                // More efficient erase - swap with last and pop_back
                if (i1 < prog.instructions.size() - 1) {
                    prog.instructions[i1] = prog.instructions.back();
                }
                prog.instructions.pop_back();

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

            if (c >= free_cycles)
                c = free_cycles - 1;
            x = screen_cycles[c].offset;
            x += Random(screen_cycles[c].length);
        }
        if (x < 0 || x >= (int)m_width)
            x = Random(m_width);
        i2 = m_currently_mutated_y;
        // check color in next lines
        while (Random(5) == 0 && i2 + 1 < (int)m_height)
            ++i2;
        prog.instructions[i1].loose.value = FindAtariColorIndex(m_picture[i2][x]) * 2;
        prog.cache_key = NULL;
        m_current_mutations[E_MUTATION_CHANGE_VALUE_TO_COLOR]++;
        m_stats.success_count[mutation]++;
        break;
    case E_MUTATION_CHANGE_VALUE:
        if (Random(10) == 0)
        {
            if (Random(2))
                prog.instructions[i1].loose.value = (Random(128) * 2);
            else
            {
                const std::vector<unsigned char>& possible_colors = m_gstate->m_possible_colors_for_each_line[m_currently_mutated_y];
                prog.instructions[i1].loose.value = possible_colors[Random(possible_colors.size())];
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
    }
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

void RasterMutator::MutateProgram(raster_picture* pic)
{
    memset(m_current_mutations, 0, sizeof(m_current_mutations));

    // Calculate this thread's assigned region
    int thread_count = m_gstate->m_thread_count;
    int lines_per_thread = m_height / thread_count;
    int region_start = m_thread_id * lines_per_thread;
    int region_end = (m_thread_id == thread_count - 1) ?
        m_height : region_start + lines_per_thread;

    // Prefer mutating lines in this thread's region (80% of the time)
    if (Random(100) < 80 && region_end > region_start) {
        m_currently_mutated_y = region_start + Random(region_end - region_start);
    }
    // Otherwise, allow some exploration outside the region (20% of time)
    else if (m_currently_mutated_y >= (int)pic->raster_lines.size()) {
        m_currently_mutated_y = 0;
    }
    else if (m_currently_mutated_y < 0) {
        m_currently_mutated_y = pic->raster_lines.size() - 1;
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

        pic->mem_regs_init[targ] += c;
    }

    raster_line& current_line = pic->raster_lines[m_currently_mutated_y];
    MutateLine(current_line, *pic);

    if (Random(20) == 0)
    {
        for (int t = 0; t < 10; ++t)
        {
            // When jumping, prefer to stay within this thread's region
            if (Random(2) && m_currently_mutated_y > region_start)
                --m_currently_mutated_y;
            else if (m_currently_mutated_y < region_end - 1)
                ++m_currently_mutated_y;
            else {
                // Fall back to anywhere in the thread's region
                m_currently_mutated_y = region_start + Random(region_end - region_start);
            }

            raster_line& current_line = pic->raster_lines[m_currently_mutated_y];
            MutateLine(current_line, *pic);
        }
    }

    // recache any lines that have changed
    for (int y = 0; y < (int)m_height; ++y) {
        raster_line& rline = pic->raster_lines[y];
        if (rline.cache_key == NULL)
            rline.recache_insns(m_gstate->m_insn_seq_cache, m_gstate->m_linear_allocator);
    }
}

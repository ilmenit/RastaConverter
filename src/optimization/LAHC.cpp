// Refactored LAHC: thin wrapper that delegates to OptimizationRunner + LAHCPolicy
#include "LAHC.h"
#include "OptimizationRunner.h"
#include "AcceptancePolicy.h"
#include <cstring>
#include <cstdio>

extern bool quiet;

LAHC::LAHC(EvaluationContext* context, Executor* executor, Mutator* mutator, int historyLength)
    : m_context(context)
    , m_executor(executor)
    , m_mutator(mutator)
    , m_history_length(historyLength > 0 ? historyLength : 1)
{
}

LAHC::~LAHC()
{
    Stop();
    if (m_context) {
        m_context->JoinWorkerThreads();
    }
}

void LAHC::Initialize(const raster_picture& initialSolution)
{
    if (!m_context) return;
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    m_context->m_best_pic = initialSolution;
    try { m_context->m_best_pic.recache_insns(m_context->m_insn_seq_cache, m_context->m_linear_allocator); } catch (...) {}
}

double LAHC::EvaluateInitialSolution()
{
    if (!m_context || !m_executor) return DBL_MAX;
    raster_picture pic = m_context->m_best_pic;
    std::vector<const line_cache_result*> line_results(m_context->m_height);
    double result = static_cast<double>(m_executor->ExecuteRasterProgram(&pic, line_results.data()));
    {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        m_context->m_best_result = result;
        m_context->m_current_cost = result;
        m_context->m_created_picture.resize(m_context->m_height);
        m_context->m_created_picture_targets.resize(m_context->m_height);
        const line_cache_result* last_valid = nullptr;
        for (int y = 0; y < (int)m_context->m_height; ++y) {
            if (line_results[y]) {
                last_valid = line_results[y];
                const line_cache_result& lcr = *line_results[y];
                m_context->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_context->m_width);
                m_context->m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_context->m_width);
            } else if (last_valid) {
                m_context->m_created_picture[y].assign(last_valid->color_row, last_valid->color_row + m_context->m_width);
                m_context->m_created_picture_targets[y].assign(last_valid->target_row, last_valid->target_row + m_context->m_width);
            } else {
                m_context->m_created_picture[y].assign(m_context->m_width, 0);
                m_context->m_created_picture_targets[y].assign(m_context->m_width, E_COLBAK);
            }
        }
        memcpy(&m_context->m_sprites_memory, &m_executor->GetSpritesMemory(), sizeof(m_context->m_sprites_memory));
        m_context->m_initialized = true;
        m_context->m_update_initialized = true;
        m_context->m_condvar_update.notify_all();
    }
    return result;
}

void LAHC::RunWorker(int threadId)
{
    // No longer used in the refactored path
}

void LAHC::Run()
{
    if (!m_context) return;
    // Build LAHC policy and runner; runner provides dual-mode automatically
    auto policy = std::make_unique<LAHCPolicy>(m_history_length);
    OptimizationRunner runner(m_context, std::move(policy), m_mutator);
    runner.run();
}

void LAHC::Start()
{
    try {
        m_control_thread = std::thread(&LAHC::Run, this);
    } catch (...) {}
}

void LAHC::Stop()
{
    if (m_context) {
        CTX_MARK_FINISHED((*m_context), "lahc_stop");
        m_context->m_condvar_update.notify_all();
    }
    if (m_control_thread.joinable()) {
        try { m_control_thread.join(); } catch (...) {}
    }
}

bool LAHC::IsFinished() const
{
    return (m_context && m_context->m_finished.load());
}

const raster_picture& LAHC::GetBestSolution() const
{
    if (!m_context) { static raster_picture empty; return empty; }
    return m_context->m_best_pic;
}

void LAHC::SaveState(const std::string& filename) const
{
    if (!m_context) return;
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    FILE* f = fopen(filename.c_str(), "wt+");
    if (!f) return;
    fprintf(f, "%lu\n", (unsigned long)m_context->m_previous_results.size());
    fprintf(f, "%lu\n", (unsigned long)m_context->m_previous_results_index);
    fprintf(f, "%Lf\n", (long double)m_context->m_cost_max);
    fprintf(f, "%d\n", m_history_length);
    fprintf(f, "%Lf\n", (long double)m_context->m_current_cost);
    for (double v : m_context->m_previous_results) fprintf(f, "%Lf\n", (long double)v);
    fclose(f);
}

bool LAHC::LoadState(const std::string& filename)
{
    if (!m_context) return false;
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    FILE* f = fopen(filename.c_str(), "rt");
    if (!f) return false;
    unsigned long no_elements = 0, index = 0; long double cost_max = 0, current_cost = 0; int N = 0;
    if (fscanf(f, "%lu\n", &no_elements) != 1 || fscanf(f, "%lu\n", &index) != 1 ||
        fscanf(f, "%Lf\n", &cost_max) != 1 || fscanf(f, "%d\n", &N) != 1 ||
        fscanf(f, "%Lf\n", &current_cost) != 1) { fclose(f); return false; }
    m_context->m_previous_results.clear();
    for (size_t i = 0; i < (size_t)no_elements; ++i) { long double v = 0; if (fscanf(f, "%Lf\n", &v) != 1) { fclose(f); return false; } m_context->m_previous_results.push_back((double)v); }
    fclose(f);
    m_context->m_previous_results_index = index;
    m_context->m_cost_max = (double)cost_max;
    m_context->m_current_cost = (double)current_cost;
    m_history_length = N > 0 ? N : (int)m_context->m_previous_results.size();
    m_context->m_initialized = true;
    return true;
}



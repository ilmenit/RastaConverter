#include "LAHC.h"
#include "../mutation/RasterMutator.h"
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <iostream>

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
    try {
        m_context->m_best_pic.recache_insns(m_context->m_insn_seq_cache, m_context->m_linear_allocator);
    } catch (...) {}

    // Initialize LAHC history to the configured length
    m_context->m_previous_results.clear();
    m_context->m_previous_results.resize((size_t)m_history_length, DBL_MAX);
    m_context->m_previous_results_index = 0;
    m_context->m_current_cost = DBL_MAX;
    m_context->m_cost_max = DBL_MAX;
    m_context->m_N = (int)m_context->m_previous_results.size();
}

double LAHC::EvaluateInitialSolution()
{
    if (!m_context || !m_executor) return DBL_MAX;
    raster_picture pic = m_context->m_best_pic;
    std::vector<const line_cache_result*> line_results(m_context->m_height);
    double result = m_executor->ExecuteRasterProgram(&pic, line_results.data());

    {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        m_context->m_best_result = result;
        m_context->m_current_cost = result;
        // Initialize history with the initial result
        std::fill(m_context->m_previous_results.begin(), m_context->m_previous_results.end(), result);
        m_context->m_previous_results_index = 0;

        // Copy visualization data (best-effort)
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
    if (!m_context) return;

    // Create per-thread executor and mutator
    RasterMutator thread_mutator(m_context, threadId);
    thread_mutator.Init((unsigned long long)time(NULL) + (unsigned long long)threadId * 187927ULL);

    Executor local_executor;
    try {
        local_executor.Init(
            m_context->m_width,
            m_context->m_height,
            m_context->m_picture_all_errors,
            m_context->m_picture,
            m_context->m_onoff,
            m_context,
            m_history_length,
            (unsigned long long)time(NULL) + (unsigned long long)threadId * 911ULL,
            m_context->m_cache_size,
            &thread_mutator,
            threadId);
    } catch (...) {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        m_context->m_threads_active.fetch_sub(1);
        m_context->m_condvar_update.notify_all();
        return;
    }
    local_executor.Start();

    raster_picture current = m_context->m_best_pic;
    double current_cost = m_context->m_current_cost;
    std::vector<const line_cache_result*> line_results(m_context->m_height);

    while (m_running && !m_context->m_finished.load()) {
        raster_picture candidate = current;
        thread_mutator.MutateProgram(&candidate);
        double cand_cost = (double)local_executor.ExecuteRasterProgram(&candidate, line_results.data());

        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        ++m_context->m_evaluations;
        if ((m_context->m_max_evals > 0 && m_context->m_evaluations >= m_context->m_max_evals) || m_context->m_finished.load()) {
            CTX_MARK_FINISHED((*m_context), "lahc_max_evals");
            break;
        }

        // LAHC acceptance: accept if cand_cost <= history[l] or cand_cost < current_cost
        size_t L = m_context->m_previous_results.empty() ? 1 : m_context->m_previous_results.size();
        size_t l = (L > 0) ? (m_context->m_previous_results_index % L) : 0;
        double history_cost = (L > 0) ? m_context->m_previous_results[l] : DBL_MAX;
        bool accept = (cand_cost <= history_cost) || (cand_cost < current_cost);

        if (accept) {
            current = candidate;
            current_cost = cand_cost;
        }

        // Replacement: store current_cost into history at l (classic LAHC)
        if (L > 0) {
            m_context->m_previous_results[l] = current_cost;
            ++m_context->m_previous_results_index;
        }

        // Best tracking and visualization update if improved
        if (cand_cost < m_context->m_best_result) {
            m_context->m_last_best_evaluation = m_context->m_evaluations;
            m_context->m_best_pic = candidate;
            m_context->m_best_pic.uncache_insns();
            m_context->m_best_result = cand_cost;

            try {
                m_context->m_created_picture.resize(m_context->m_height);
                m_context->m_created_picture_targets.resize(m_context->m_height);
                for (int y = 0; y < (int)m_context->m_height; ++y) {
                    if (y < (int)line_results.size() && line_results[y]) {
                        const line_cache_result& lcr = *line_results[y];
                        m_context->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_context->m_width);
                        m_context->m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_context->m_width);
                    }
                }
                memcpy(&m_context->m_sprites_memory, &local_executor.GetSpritesMemory(), sizeof(m_context->m_sprites_memory));
            } catch (...) {}

            m_context->m_update_improvement = true;
            m_context->m_condvar_update.notify_all();

            if (cand_cost == 0) {
                CTX_MARK_FINISHED((*m_context), "lahc_perfect_solution");
                break;
            }
        }

        // Statistics collection and auto-save trigger
        m_context->CollectStatisticsTickUnsafe();
        if (m_context->m_save_period > 0 && (m_context->m_evaluations % m_context->m_save_period == 0)) {
            m_context->m_update_autosave = true;
            m_context->m_condvar_update.notify_all();
        }
    }

    // Decrement active threads
    {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        int prev = m_context->m_threads_active.fetch_sub(1);
        if (prev <= 1) m_context->m_condvar_update.notify_all();
    }
}

void LAHC::Run()
{
    if (!m_context) return;
    m_running = true;
    {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        m_context->m_finished.store(false);
    }

    // Evaluate initial solution once using the provided executor
    EvaluateInitialSolution();

    // Start worker threads
    m_context->StartWorkerThreads(std::bind(&LAHC::RunWorker, this, std::placeholders::_1));

    // Wait until finished or no threads active
    {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        while (m_running && !m_context->m_finished.load() && m_context->m_threads_active.load() > 0) {
            m_context->m_condvar_update.wait_for(lock, std::chrono::seconds(5));
        }
    }

    m_context->JoinWorkerThreads();
    m_running = false;
}

void LAHC::Start()
{
    m_running = true;
    try {
        m_control_thread = std::thread(&LAHC::Run, this);
    } catch (...) {
        m_running = false;
    }
}

void LAHC::Stop()
{
    m_running = false;
    if (m_context) {
        // Avoid lock ordering/deadlock; just mark finished and notify.
        CTX_MARK_FINISHED((*m_context), "lahc_stop");
        m_context->m_condvar_update.notify_all();
    }
    if (m_control_thread.joinable()) {
        try { m_control_thread.join(); } catch (...) {}
    }
}

bool LAHC::IsFinished() const
{
    return !m_running || (m_context && m_context->m_finished.load());
}

const raster_picture& LAHC::GetBestSolution() const
{
    if (!m_context) {
        static raster_picture empty;
        return empty;
    }
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
    unsigned long no_elements = 0;
    unsigned long index = 0;
    long double cost_max = 0;
    int N = 0;
    long double current_cost = 0;
    if (fscanf(f, "%lu\n", &no_elements) != 1 ||
        fscanf(f, "%lu\n", &index) != 1 ||
        fscanf(f, "%Lf\n", &cost_max) != 1 ||
        fscanf(f, "%d\n", &N) != 1 ||
        fscanf(f, "%Lf\n", &current_cost) != 1) {
        fclose(f);
        return false;
    }
    m_context->m_previous_results.clear();
    for (size_t i = 0; i < (size_t)no_elements; ++i) {
        long double v = 0;
        if (fscanf(f, "%Lf\n", &v) != 1) { fclose(f); return false; }
        m_context->m_previous_results.push_back((double)v);
    }
    fclose(f);
    m_context->m_previous_results_index = index;
    m_context->m_cost_max = (double)cost_max;
    m_context->m_current_cost = (double)current_cost;
    m_history_length = N > 0 ? N : (int)m_context->m_previous_results.size();
    m_context->m_initialized = true;
    return true;
}



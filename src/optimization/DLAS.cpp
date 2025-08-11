#include "DLAS.h"
#include "OptimizationRunner.h"
#include "AcceptancePolicy.h"
#include <cstdio>
#include <iostream>
#include <mutex>

DLAS::DLAS(EvaluationContext* context, Executor* /*executor*/, Mutator* /*mutator*/, int solutions)
    : m_context(context)
    , m_solutions(solutions > 0 ? solutions : 1)
{
    if (!m_context) {
        std::cerr << "Error: DLAS requires a valid EvaluationContext" << std::endl;
    }
}

DLAS::~DLAS()
{
    if (m_context) m_context->JoinWorkerThreads();
}

void DLAS::Initialize(const raster_picture& initialSolution)
{
    if (!m_context) return;
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    m_context->m_best_pic = initialSolution;
    try { m_context->m_best_pic.recache_insns(m_context->m_insn_seq_cache, m_context->m_linear_allocator); } catch (...) {}
}

void DLAS::Run()
{
    if (!m_context) return;
    auto policy = std::make_unique<DLASPolicy>();
    OptimizationRunner runner(m_context, std::move(policy), nullptr);
    runner.run();
}

void DLAS::Start()
{
    try { m_control_thread = std::thread(&DLAS::Run, this); } catch (...) {}
}

void DLAS::Stop()
{
    if (m_context) { CTX_MARK_FINISHED((*m_context), "dlas_stop"); m_context->m_condvar_update.notify_all(); }
    if (m_control_thread.joinable()) { try { m_control_thread.join(); } catch (...) {} }
}

bool DLAS::IsFinished() const
{
    return (m_context && m_context->m_finished.load());
}

const raster_picture& DLAS::GetBestSolution() const
{
    if (!m_context) { static raster_picture empty; return empty; }
    return m_context->m_best_pic;
}

void DLAS::SaveState(const std::string& filename) const
{
    if (!m_context) return;
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    FILE* f = fopen(filename.c_str(), "wt+");
    if (!f) return;
    fprintf(f, "%lu\n", (unsigned long)m_context->m_previous_results.size());
    fprintf(f, "%lu\n", (unsigned long)m_context->m_previous_results_index);
    fprintf(f, "%Lf\n", (long double)m_context->m_cost_max);
    fprintf(f, "%d\n", m_context->m_N);
    fprintf(f, "%Lf\n", (long double)m_context->m_current_cost);
    for (double v : m_context->m_previous_results) fprintf(f, "%Lf\n", (long double)v);
    fclose(f);
}

bool DLAS::LoadState(const std::string& filename)
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
    m_context->m_N = N;
    m_context->m_initialized = true;
    return true;
}
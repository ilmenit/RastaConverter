#include "OptimizationController.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>

// Array variable is now defined in RasterMutator.cpp
extern const char* mutation_names[E_MUTATION_MAX];

OptimizationController::OptimizationController()
    : m_running(false)
    , m_rate(0.0)
    , m_lastEval(0)
{
    m_lastRateCheckTime = std::chrono::steady_clock::now();
}

OptimizationController::~OptimizationController()
{
    // Clean up executors
    for (Executor* exec : m_executors) {
        delete exec;
    }
    m_executors.clear();
}

bool OptimizationController::Initialize(int threads, int width, int height,
    const std::vector<screen_line>* picture,
    const std::vector<distance_t>* pictureAllErrors[128],
    unsigned long long maxEvals,
    int savePeriod,
    size_t cacheSize,
    unsigned long initialSeed,
    const OnOffMap* onoffMap)
{
    // Initialize evaluation context
    m_evalContext.m_width = width;
    m_evalContext.m_height = height;
    m_evalContext.m_picture = picture->data();

    // Store pictureAllErrors directly and create a raw pointer array for compatibility
    static const distance_t* error_ptrs[128];
    for (int i = 0; i < 128; ++i) {
        error_ptrs[i] = pictureAllErrors[i]->data();
    }
    m_evalContext.m_picture_all_errors = error_ptrs;

    m_evalContext.m_max_evals = maxEvals;
    m_evalContext.m_save_period = savePeriod;
    m_evalContext.m_cache_size = cacheSize;
    m_evalContext.m_thread_count = threads;
    m_evalContext.m_time_start = time(NULL);
    m_evalContext.m_onoff = onoffMap;

    // Initialize executors
    m_executors.resize(threads);
    unsigned long long randseed = initialSeed;

    for (int i = 0; i < threads; ++i)
    {
        // Ensure non-zero seed
        if (!randseed)
            ++randseed;

        m_executors[i] = new Executor();
        m_executors[i]->Init(width, height, error_ptrs,
            picture->data(), onoffMap, &m_evalContext,
            1, randseed, cacheSize, i);

        // Ensure different seeds for each thread
        randseed += 187927 * i;
    }

    return true;
}

void OptimizationController::SetInitialProgram(const raster_picture& initialProgram,
    const std::vector<std::vector<unsigned char>>& possibleColors)
{
    // Set the possible colors
    m_evalContext.m_possible_colors_for_each_line = possibleColors;

    // Set the initial program
    m_evalContext.m_best_pic = initialProgram;
}

void OptimizationController::Run()
{
    if (m_running)
        return;

    m_running = true;

    // Start with a single executor to initialize
    std::unique_lock<std::mutex> lock{ m_evalContext.m_mutex };
    m_executors[0]->Start();

    // Wait for initialization before starting other executors
    while (!m_evalContext.m_initialized) {
        m_evalContext.m_condvar_update.wait(lock);
    }

    // Start remaining executors
    for (size_t i = 1; i < m_executors.size(); ++i) {
        m_executors[i]->Start();
    }

    // Main thread will return, executors run in the background
}

void OptimizationController::Stop()
{
    m_running = false;
    m_evalContext.m_finished = true;

    // Notify all waiting threads
    m_evalContext.m_condvar_update.notify_all();

    // Wait for all threads to finish
    std::unique_lock<std::mutex> lock{ m_evalContext.m_mutex };
    while (m_evalContext.m_threads_active > 0) {
        m_evalContext.m_condvar_update.wait(lock);
    }
}

bool OptimizationController::IsFinished() const
{
    return m_evalContext.m_finished;
}

bool OptimizationController::IsInitialized() const
{
    return m_evalContext.m_initialized;
}

const raster_picture& OptimizationController::GetBestProgram() const
{
    return m_evalContext.m_best_pic;
}

const EvaluationContext& OptimizationController::GetEvaluationContext() const
{
    return m_evalContext;
}

EvaluationContext& OptimizationController::GetEvaluationContext()
{
    return m_evalContext;
}

void OptimizationController::SaveState(const std::string& filename) const
{
    // Use a non-const reference to m_mutex to avoid const issues
    std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(m_evalContext.m_mutex));

    FILE* f = fopen(filename.c_str(), "wt+");
    if (!f)
        return;

    fprintf(f, "%lu\n", (unsigned long)m_evalContext.m_previous_results.size());
    fprintf(f, "%lu\n", (unsigned long)m_evalContext.m_previous_results_index);
    fprintf(f, "%Lf\n", (long double)m_evalContext.m_cost_max);
    fprintf(f, "%d\n", m_evalContext.m_N);
    fprintf(f, "%Lf\n", (long double)m_evalContext.m_current_cost);

    for (size_t i = 0; i < m_evalContext.m_previous_results.size(); ++i)
    {
        fprintf(f, "%Lf\n", (long double)m_evalContext.m_previous_results[i]);
    }

    fclose(f);
}

bool OptimizationController::LoadState(const std::string& filename)
{
    std::unique_lock<std::mutex> lock(m_evalContext.m_mutex);

    FILE* f = fopen(filename.c_str(), "rt");
    if (!f)
        return false;

    unsigned long no_elements;
    unsigned long index;
    long double cost_max;
    int N;
    long double current_cost;

    fscanf(f, "%lu\n", &no_elements);
    fscanf(f, "%lu\n", &index);
    fscanf(f, "%Lf\n", &cost_max);
    fscanf(f, "%d\n", &N);
    fscanf(f, "%Lf\n", &current_cost);

    m_evalContext.m_previous_results_index = index;
    m_evalContext.m_cost_max = cost_max;
    m_evalContext.m_N = N;
    m_evalContext.m_current_cost = current_cost;

    m_evalContext.m_previous_results.clear();

    for (size_t i = 0; i < (size_t)no_elements; ++i)
    {
        long double dst = 0;
        fscanf(f, "%Lf\n", &dst);
        m_evalContext.m_previous_results.push_back(dst);
    }

    fclose(f);
    return true;
}

void OptimizationController::ShowMutationStats() const
{
    // This is a utility function for UI classes
    // The actual implementation would be in the UI adapter
}

double OptimizationController::GetRate() const
{
    return m_rate;
}

void OptimizationController::UpdateRate()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastRateCheckTime).count();

    if (elapsed > 0) {
        unsigned long long current_evals = m_evalContext.m_evaluations;
        m_rate = 1000.0 * (double)(current_evals - m_lastEval) / (double)elapsed;

        m_lastRateCheckTime = now;
        m_lastEval = current_evals;
    }
}
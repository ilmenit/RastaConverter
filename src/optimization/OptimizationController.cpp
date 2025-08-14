#include "OptimizationController.h"
#include "DLAS.h"
#include "LAHC.h"
#include "../config/config.h"
#include "../mutation/RasterMutator.h"
#include <algorithm>
#include <iostream>

// Defined in config.cpp
extern int solutions;

OptimizationController::OptimizationController()
    : m_running(false)
    , m_rate(0.0)
    , m_lastEval(0)
{
    m_lastRateCheckTime = std::chrono::steady_clock::now();
    m_lastAutoSaveTime = std::chrono::steady_clock::now();
}

OptimizationController::~OptimizationController()
{
    Stop();
}

bool OptimizationController::Initialize(int threads, int width, int height,
    const std::vector<screen_line>* picture,
    const std::vector<distance_t>* pictureAllErrors[128],
    unsigned long long maxEvals,
    int savePeriod,
    size_t cacheSize,
    unsigned long initialSeed,
    const OnOffMap* onoffMap,
    bool useRegionalMutation,
    e_optimizer_type optimizer_type)
{
    // Initialize evaluation context
    m_evalContext.m_width = width;
    m_evalContext.m_height = height;
    m_evalContext.m_picture = picture->data();

    // Copy the array of vector pointers directly
    for (int i = 0; i < 128; i++) {
        m_evalContext.m_picture_all_errors[i] = pictureAllErrors[i];
    }

    m_evalContext.m_max_evals = maxEvals;
    m_evalContext.m_save_period = savePeriod;
    m_evalContext.m_cache_size = cacheSize;
    m_evalContext.m_onoff = onoffMap;
    m_evalContext.m_thread_count = threads;
    m_evalContext.m_time_start = time(nullptr);
    m_evalContext.m_use_regional_mutation = useRegionalMutation;
    // Dual-frame configuration is wired via EvaluationContext in RastaConverter
    // so we rely on RastaConverter to set these fields after Initialize(). Defaults remain off.

    // Pre-initialize created picture with empty data to avoid crashes
    m_evalContext.m_created_picture.resize(height);
    for (int y = 0; y < height; y++) {
        m_evalContext.m_created_picture[y].resize(width, 0);
    }

    m_evalContext.m_created_picture_targets.resize(height);
    for (int y = 0; y < height; y++) {
        m_evalContext.m_created_picture_targets[y].resize(width, 0);
    }

    // Create a single reference mutator - DLAS will create its own thread-specific mutators
    m_mutators.resize(1);
    m_mutators[0] = std::make_unique<RasterMutator>(&m_evalContext, 0);
    // Honor resume seed if present in context (set by resume flow)
    {
        unsigned long long seed_to_use = initialSeed;
        // no direct access to cfg here; seed continuity should be injected via caller if needed
        m_mutators[0]->Init(seed_to_use);
    }

    // Create a single executor for DLAS to use as a reference
    m_executors.resize(1);
    m_executors[0] = std::make_unique<Executor>();
    m_executors[0]->Init(
        width, height,
        pictureAllErrors,  // Pass the error map array directly
        picture->data(),
        onoffMap,
        &m_evalContext,
        solutions, // Use the global solutions value from config
        initialSeed,
        cacheSize,
        m_mutators[0].get(),  // Pass reference mutator
        0                     // Thread ID
    );

    // Create optimizer based on selection
    int solutionsParam = solutions;
    // Keep desired history length in context for algorithms that want to reference it
    m_evalContext.m_history_length_config = solutionsParam;
    switch (optimizer_type) {
    case E_OPT_LAHC:
        m_optimizer = std::make_unique<LAHC>(&m_evalContext, m_executors[0].get(), m_mutators[0].get(), solutionsParam);
        break;
    case E_OPT_DLAS:
    default:
        m_optimizer = std::make_unique<DLAS>(&m_evalContext, m_executors[0].get(), m_mutators[0].get(), solutionsParam);
        break;
    }

    return true;
}

void OptimizationController::SetInitialProgram(const raster_picture& initialProgram,
                                             const std::vector<std::vector<unsigned char>>& possibleColors)
{
    // Set possible colors
    m_evalContext.m_possible_colors_for_each_line = possibleColors;
    
    // Initialize optimizer with initial program
    m_optimizer->Initialize(initialProgram);
}

void OptimizationController::Run()
{
    if (m_running)
        return;
    
    m_running = true;
    
    // Reset auto-save time
    m_lastAutoSaveTime = std::chrono::steady_clock::now();
    
    // Ensure evaluation context start-of-run state is clean to avoid early exit races
    {
        std::unique_lock<std::mutex> lock(m_evalContext.m_mutex);
        m_evalContext.m_finished.store(false);
        m_evalContext.m_initialized = false;
        m_evalContext.m_update_initialized = false;
        m_evalContext.m_update_improvement = false;
        m_evalContext.m_update_autosave = false;
        m_evalContext.m_threads_active.store(0);
    }
    
    // Start the optimizer
    m_optimizer->Start();

    // Initialize statistics tracking baseline
    {
        std::unique_lock<std::mutex> lock(m_evalContext.m_mutex);
        m_evalContext.m_time_start = time(NULL);
        m_evalContext.m_statistics.clear();
        m_evalContext.m_last_statistics_seconds = 0;
    }
}

void OptimizationController::Stop()
{
    m_running = false;
    if (m_optimizer) {
        #ifdef THREAD_DEBUG
        std::cout << "[CTL] OptimizationController::Stop()" << std::endl;
        #endif
        m_optimizer->Stop();
    }
}

bool OptimizationController::IsFinished() const
{
    // Only the evaluation context's finished flag should control loop termination.
    // Do not end just because worker count temporarily drops to zero; the control thread may re-spawn.
    static std::atomic<bool> reported{false};
    bool fin = m_evalContext.m_finished.load();
    if (fin && !reported.exchange(true)) {
        #ifdef THREAD_DEBUG
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(m_evalContext.m_mutex));
        std::cout << "[CTL] IsFinished=true; reason='" << m_evalContext.m_finish_reason
                  << "' at " << m_evalContext.m_finish_file << ":" << m_evalContext.m_finish_line
                  << ", evals_at=" << m_evalContext.m_finish_evals_at
                  << ", threads_active=" << m_evalContext.m_threads_active.load() << std::endl;
        #endif
    }
    return fin;
}

bool OptimizationController::IsInitialized() const
{
    return m_evalContext.m_initialized;
}

const raster_picture& OptimizationController::GetBestProgram() const
{
    return m_optimizer->GetBestSolution();
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
    m_optimizer->SaveState(filename);
}

bool OptimizationController::LoadState(const std::string& filename)
{
    return m_optimizer->LoadState(filename);
}

void OptimizationController::ShowMutationStats() const
{
    // This is a UI-related function that can be implemented by the caller
}

double OptimizationController::GetRate() const
{
    return m_rate;
}

void OptimizationController::UpdateRate()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastRateCheckTime).count();
    
    if (elapsed > 0) {
        unsigned long long currentEval = m_evalContext.m_evaluations;
        
        // Skip on first update when lastEval hasn't been set yet
        if (m_lastEval == 0 && currentEval > 0) {
            m_lastEval = currentEval;
            m_lastRateCheckTime = now;
            return;
        }
        
        // Ensure we don't underflow
        unsigned long long evalsDone = (currentEval >= m_lastEval) ? 
                                      (currentEval - m_lastEval) : currentEval;
        
        // Calculate evaluations per second
        m_rate = (evalsDone * 1000.0) / elapsed;
        
        // Update last values
        m_lastEval = currentEval;
        m_lastRateCheckTime = now;
    }
}

bool OptimizationController::CheckAutoSave()
{
    bool auto_save_triggered = false;
    
    // Check if automatic time-based saving is enabled (save_period == -1)
    if ((long long)m_evalContext.m_save_period == -1) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastAutoSaveTime).count();
        
        // Auto-save every 30 seconds
        if (elapsed >= 30) {
            m_lastAutoSaveTime = now;
            auto_save_triggered = true;
            
            // Update the context's save time as well
            m_evalContext.m_previous_save_time = now;
        }
    }
    
    return auto_save_triggered;
}

// Periodically collect statistics (evals, seconds, best distance)
// moved to EvaluationContext::CollectStatisticsTickUnsafe()
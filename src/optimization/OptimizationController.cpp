#include "OptimizationController.h"
#include "DLAS.h"
#include "../mutation/RasterMutator.h"
#include <algorithm>

OptimizationController::OptimizationController()
    : m_running(false)
    , m_rate(0.0)
    , m_lastEval(0)
{
    m_lastRateCheckTime = std::chrono::steady_clock::now();
}

OptimizationController::~OptimizationController()
{
    Stop();
}

bool OptimizationController::Initialize(int threads, int width, int height, 
                                      const std::vector<screen_line>* picture,
                                      const std::vector<distance_t>* pictureAllErrors[128],  // Changed back to match RastaConverter
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
    
    // Create a single reference mutator - DLAS will create its own thread-specific mutators
    m_mutators.resize(1);
    m_mutators[0] = std::make_unique<RasterMutator>(&m_evalContext, 0);
    m_mutators[0]->Init(initialSeed);
    
    // Create a single executor for DLAS to use as a reference
    m_executors.resize(1);
    m_executors[0] = std::make_unique<Executor>();
    m_executors[0]->Init(
        width, height,
        pictureAllErrors,  // Pass the error map array directly
        picture->data(),
        onoffMap,
        &m_evalContext,
        threads, // Number of solutions/history entries is based on thread count
        initialSeed,
        cacheSize,
        m_mutators[0].get(),  // Pass reference mutator
        0                     // Thread ID
    );
    
    // Create optimizer (DLAS for now, can be extended later)
    m_optimizer = std::make_unique<DLAS>(&m_evalContext, m_executors[0].get(), m_mutators[0].get());
    
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
    
    // Start the optimizer
    m_optimizer->Start();
}

void OptimizationController::Stop()
{
    m_running = false;
}

bool OptimizationController::IsFinished() const
{
    return m_evalContext.m_finished || m_optimizer->IsFinished();
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
        unsigned long long evalsDone = currentEval - m_lastEval;
        
        // Calculate evaluations per second
        m_rate = (evalsDone * 1000.0) / elapsed;
        
        // Update last values
        m_lastEval = currentEval;
        m_lastRateCheckTime = now;
    }
}
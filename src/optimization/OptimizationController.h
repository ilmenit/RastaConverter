#ifndef OPTIMIZATION_CONTROLLER_H
#define OPTIMIZATION_CONTROLLER_H

#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include "../config/config.h"
#include "../raster/Program.h"
#include "EvaluationContext.h"
#include "../execution/Executor.h"
#include "../mutation/Mutator.h"
#include "Optimizer.h"
#include "DLAS.h"
#include "LAHC.h"

/**
 * Controls and manages the optimization process
 */
class OptimizationController {
public:
    /**
     * Constructor
     */
    OptimizationController();
    
    /**
     * Destructor
     */
    ~OptimizationController();
    
    /**
     * Initialize the controller
     * 
     * @param threads Number of threads to use
     * @param width Image width
     * @param height Image height
     * @param picture Screen line data
     * @param pictureAllErrors Color error maps
     * @param maxEvals Maximum evaluations
     * @param savePeriod How often to save
     * @param cacheSize Size of the instruction cache
     * @param initialSeed Random seed
     * @param onoffMap Register enable/disable map
     * @param useRegionalMutation Whether to use region-based mutation
     * @return True if initialization succeeded
     */
     bool Initialize(int threads, int width, int height, 
                  const std::vector<screen_line>* picture,
                  const std::vector<distance_t>* pictureAllErrors[128],
                  unsigned long long maxEvals, 
                  int savePeriod,
                  size_t cacheSize,
                  unsigned long initialSeed,
                  const OnOffMap* onoffMap = nullptr,
                   bool useRegionalMutation = false,
                   e_optimizer_type optimizer_type = E_OPT_DLAS);
    
    /**
     * Initialize the optimization with a starting raster program
     * 
     * @param initialProgram Initial raster program
     * @param possibleColors Possible colors for each line
     */
    void SetInitialProgram(const raster_picture& initialProgram,
                          const std::vector<std::vector<unsigned char>>& possibleColors);
    
    /**
     * Run the optimization process
     */
    void Run();
    
    /**
     * Stop the optimization process
     */
    void Stop();
    
    /**
     * Check if optimization is finished
     * 
     * @return True if optimization is complete
     */
    bool IsFinished() const;
    
    /**
     * Check if optimization is initialized
     * 
     * @return True if optimization is initialized
     */
    bool IsInitialized() const;
    
    /**
     * Get the best raster program found
     * 
     * @return Best raster program
     */
    const raster_picture& GetBestProgram() const;
    
    /**
     * Get the evaluation context
     * 
     * @return Reference to evaluation context
     */
    const EvaluationContext& GetEvaluationContext() const;
    
    /**
     * Get the evaluation context (mutable)
     * 
     * @return Reference to evaluation context
     */
    EvaluationContext& GetEvaluationContext();
    
    /**
     * Save the current state of the optimization
     * 
     * @param filename Path to save to
     */
    void SaveState(const std::string& filename) const;
    
    /**
     * Load a saved optimization state
     * 
     * @param filename Path to load from
     * @return True if loading succeeded
     */
    bool LoadState(const std::string& filename);
    
    /**
     * Show mutation statistics (for UI)
     */
    void ShowMutationStats() const;
    
    /**
     * Get number of evaluations per second
     * 
     * @return Evaluations per second
     */
    double GetRate() const;
    
    /**
     * Update the evaluation rate
     */
    void UpdateRate();
    
    /**
     * Check and handle auto-save logic
     * 
     * @return True if auto-save was triggered
     */
    bool CheckAutoSave();
    
private:
    // Evaluation context shared across threads
    EvaluationContext m_evalContext;
    
    // Thread control
    bool m_running;
    
    // Optimizer instance
    std::unique_ptr<Optimizer> m_optimizer;
    
    // Executors and mutators for each thread
    std::vector<std::unique_ptr<Executor>> m_executors;
    std::vector<std::unique_ptr<Mutator>> m_mutators;
    
    // Rate tracking
    double m_rate;
    unsigned long long m_lastEval;
    std::chrono::time_point<std::chrono::steady_clock> m_lastRateCheckTime;
    std::chrono::time_point<std::chrono::steady_clock> m_lastAutoSaveTime;
};

#endif // OPTIMIZATION_CONTROLLER_H
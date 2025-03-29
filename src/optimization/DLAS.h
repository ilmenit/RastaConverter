#ifndef DLAS_H
#define DLAS_H

#include "Optimizer.h"
#include "EvaluationContext.h"
#include "../execution/Executor.h"
#include "../mutation/Mutator.h"
#include <thread>
#include <memory>
#include <vector>

/**
 * Dynamic Late Acceptance Search optimizer
 */
class DLAS : public Optimizer {
public:
    /**
     * Constructor
     * 
     * @param context Shared evaluation context
     * @param executor Program executor
     * @param mutator Program mutator
     */
    DLAS(EvaluationContext* context, Executor* executor, Mutator* mutator);
    
    /**
     * Initialize optimization with initial solution
     * 
     * @param initialSolution Initial program to optimize
     */
    void Initialize(const raster_picture& initialSolution) override;
    
    /**
     * Run optimization process (blocking)
     */
    void Run() override;
    
    /**
     * Start optimization in a separate thread
     */
    void Start() override;
    
    /**
     * Check if optimization has finished
     * 
     * @return True if optimization is complete
     */
    bool IsFinished() const override;
    
    /**
     * Get the best solution found
     * 
     * @return Best solution found
     */
    const raster_picture& GetBestSolution() const override;
    
    /**
     * Save current state to a file
     * 
     * @param filename File to save to
     */
    void SaveState(const std::string& filename) const override;
    
    /**
     * Load state from a file
     * 
     * @param filename File to load from
     * @return True if loaded successfully
     */
    bool LoadState(const std::string& filename) override;
    
    /**
     * Run worker thread for optimization
     * 
     * @param threadId ID of this worker thread
     */
    void RunWorker(int threadId);

private:
    EvaluationContext* m_context;
    Executor* m_executor;
    Mutator* m_mutator;
    
    // Thread-local resources
    bool m_threadLocal = false;
    linear_allocator m_thread_local_allocator;
    insn_sequence_cache m_thread_local_cache;
    
    // Worker thread management
    std::vector<std::unique_ptr<Mutator>> m_thread_mutators;
    std::vector<std::thread> m_worker_threads;
    bool m_running;

    // Configuration
    int m_solutions = 1;
};

#endif // DLAS_H
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
     * @param solutions Number of solutions for DLAS history (from command line)
     */
    DLAS(EvaluationContext* context, Executor* executor, Mutator* mutator, int solutions);

    /**
     * Destructor - ensures all threads are stopped and joined
     */
    ~DLAS();

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
     * Stop optimization
     */
    void Stop() override;

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
     * Worker thread function for optimization
     *
     * @param threadId ID of this worker thread
     */
    void RunWorker(int threadId);

    const char* Name() const override { return "DLAS"; }

private:
    /**
     * Evaluate the initial solution
     *
     * @return Initial solution score
     */
    double EvaluateInitialSolution();

private:
    EvaluationContext* m_context;
    Executor* m_executor;
    Mutator* m_mutator;

    // Thread-local resources for each worker
    linear_allocator m_thread_local_allocator;
    insn_sequence_cache m_thread_local_cache;

    // Worker thread management
    std::vector<std::unique_ptr<Mutator>> m_thread_mutators;
    bool m_running;
    std::thread m_control_thread; // control thread running Run()

    // Configuration
    int m_solutions;

    // Thread synchronization
    std::mutex m_init_mutex;
    std::condition_variable m_init_condvar;
    bool m_init_completed = false;
};

#endif // DLAS_H
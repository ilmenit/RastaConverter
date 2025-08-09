#ifndef EVALUATION_CONTEXT_H
#define EVALUATION_CONTEXT_H

#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <chrono>
#include <ctime>
#include <functional>
#include <thread>
#include <atomic>  // Add this for atomic operations
#include <algorithm> // For std::find
#include "../raster/Program.h"
#include "../color/Distance.h"
#include "../cache/LineCache.h"
#include "../cache/InsnSequenceCache.h"
#include "../utils/LinearAllocator.h"
#include "../OnOffMap.h"

// Forward declarations
class Executor;
class Mutator;

// Define these types before use
typedef std::vector<unsigned char> color_index_line;
typedef std::vector<unsigned char> line_target;    // target of the pixel f.e. COLBAK

/**
 * Structure to hold statistics about the optimization process
 */
struct statistics_point {
    unsigned evaluations;
    unsigned seconds;
    double distance;
};

typedef std::vector<statistics_point> statistics_list;

/**
 * Holds shared state for the optimization process
 */
class EvaluationContext {
public:
    EvaluationContext();
    ~EvaluationContext();

    // Centralized finish setter with reason and origin info
    bool MarkFinished(const char* reason, const char* file, int line);

    // Convenience macro to capture file/line automatically
    #define CTX_MARK_FINISHED(ctx, reason) (ctx).MarkFinished((reason), __FILE__, __LINE__)

    /**
     * Report the initial score for DLAS initialization
     * 
     * @param score Initial solution score
     */
    void ReportInitialScore(double score);

    /**
     * Report an evaluation result to the DLAS algorithm
     * 
     * @param result The evaluation result/distance
     * @param pic The solution that was evaluated
     * @param line_results Results for each line (for visualization)
     * @param sprites_memory Sprite memory for visualization
     * @param mutator Mutator that created this solution
     * @return Whether the solution was accepted
     */
    bool ReportEvaluationResult(double result, 
                               raster_picture* pic, 
                               const std::vector<const line_cache_result*>& line_results,
                               const sprites_memory_t& sprites_memory,
                               Mutator* mutator);

    /**
     * Register an executor for thread creation
     * 
     * @param executor Executor to register
     */
    void RegisterExecutor(Executor* executor);
    
    /**
     * Unregister an executor (for cleanup)
     * 
     * @param executor Executor to unregister
     */
    void UnregisterExecutor(Executor* executor);

    /**
     * Create and start worker threads
     * 
     * @param threadFunc Thread function to run
     * @return Number of threads started
     */
    int StartWorkerThreads(std::function<void(int)> threadFunc);

    /**
     * Join all worker threads
     */
    void JoinWorkerThreads();

    // Vector of possible colors for each line
    std::vector<std::vector<unsigned char>> m_possible_colors_for_each_line;

    // Synchronization primitives
    std::mutex m_mutex;
    std::condition_variable m_condvar_update;
    std::mutex m_cache_mutex;

    // Update flags
    bool m_update_tick = false;
    bool m_update_autosave = false;
    bool m_update_improvement = false;
    bool m_update_initialized = false;
    bool m_initialized = false;
    std::atomic<bool> m_finished{false};

    // Using atomic for thread counter to prevent race conditions
    std::atomic<int> m_threads_active{0};

    // Evaluation limits and counters
    unsigned long long m_save_period = 0;
    unsigned long long m_max_evals = 0;
    unsigned long long m_evaluations = 0;
    unsigned long long m_last_best_evaluation = 0;

    // Finish diagnostics (protected by m_mutex)
    std::string m_finish_reason;
    std::string m_finish_file;
    int m_finish_line = 0;
    std::string m_finish_thread;
    unsigned long long m_finish_evals_at = 0;

    // Best solution found
    raster_picture m_best_pic;
    double m_best_result = DBL_MAX;

    // Sprites memory for the best solution
    sprites_memory_t m_sprites_memory;

    // Created picture data
    std::vector<color_index_line> m_created_picture;
    std::vector<line_target> m_created_picture_targets;

    // Mutation statistics
    int m_mutation_stats[E_MUTATION_MAX] = { 0 };

    // Thread configuration
    int m_thread_count = 1;

    // Start time for statistics
    time_t m_time_start;

    // Statistics tracking
    statistics_list m_statistics;

    // DLAS specific fields
    double m_cost_max = DBL_MAX;                  // Maximum cost threshold
    int m_N = 0;                                  // Count of cost_max entries
    std::vector<double> m_previous_results;       // History list for DLAS
    size_t m_previous_results_index = 0;          // Current index in history
    double m_current_cost = DBL_MAX;              // Current accepted cost

    // Regional mutation config
    bool m_use_regional_mutation = false;         // Whether to use regional mutation

    // Auto-save timing
    std::chrono::time_point<std::chrono::steady_clock> m_previous_save_time;

    // Shared caching resources
    insn_sequence_cache m_insn_seq_cache;         // For caching instruction sequences
    linear_allocator m_linear_allocator;          // For memory allocation

    // Error map and target picture reference
    const std::vector<distance_t>* m_picture_all_errors[128];
    const screen_line* m_picture = nullptr;

    // Width and height of the target picture
    unsigned m_width = 0;
    unsigned m_height = 0;

    // On/off map for registers
    const OnOffMap* m_onoff = nullptr;

    // Cache size in bytes
    size_t m_cache_size = 0;

private:
    // Thread management
    std::vector<std::thread> m_worker_threads;
    std::vector<Executor*> m_executors;
};

#endif // EVALUATION_CONTEXT_H
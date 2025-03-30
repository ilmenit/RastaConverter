#ifndef EVALUATION_CONTEXT_H
#define EVALUATION_CONTEXT_H

#include <vector>
#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include "../raster/Program.h"
#include "../color/Distance.h"
#include "../cache/LineCache.h"
#include "../cache/InsnSequenceCache.h"
#include "../utils/LinearAllocator.h"
#include "../OnOffMap.h"

// Forward declarations
class Executor;

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
    bool m_finished = false;

    int m_threads_active = 0;

    // Evaluation limits and counters
    unsigned long long m_save_period = 0;
    unsigned long long m_max_evals = 0;
    unsigned long long m_evaluations = 0;
    unsigned long long m_last_best_evaluation = 0;

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

    // Tracking of previous costs per thread for DLAS
    std::vector<double> m_thread_previous_costs;

    // Auto-save timing
    std::chrono::time_point<std::chrono::steady_clock> m_previous_save_time;

    // Shared caching resources
    insn_sequence_cache m_insn_seq_cache;         // For caching instruction sequences
    linear_allocator m_linear_allocator;          // For memory allocation

    // Error map and target picture reference
    // Change this type to match what RastaConverter is passing
    const std::vector<distance_t>* m_picture_all_errors[128];
    const screen_line* m_picture = nullptr;

    // Width and height of the target picture
    unsigned m_width = 0;
    unsigned m_height = 0;

    // On/off map for registers
    const OnOffMap* m_onoff = nullptr;

    // Cache size in bytes
    size_t m_cache_size = 0;
};

#endif // EVALUATION_CONTEXT_H
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
#include <cfloat>   // For DBL_MAX
#include <algorithm> // For std::find
#include "../raster/Program.h"
#include "../color/Distance.h"
#include "../cache/LineCache.h"
#include "../cache/InsnSequenceCache.h"
#include "../utils/LinearAllocator.h"
#include "../raster/OnOffMap.h"
#include "../config/config.h"

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
    // Second best program for dual mode (frame B)
    raster_picture m_best_pic_B;

    // Sprites memory for the best solution
    sprites_memory_t m_sprites_memory;
    sprites_memory_t m_sprites_memory_B; // for temporal mode

    // Created picture data
    std::vector<color_index_line> m_created_picture;
    std::vector<line_target> m_created_picture_targets;
    std::vector<color_index_line> m_created_picture_B; // dual mode
    std::vector<line_target> m_created_picture_targets_B; // dual mode

    // Last evaluated A/B snapshots (per-iteration, not only on best-improvement)
    // Used by dual-mode mutations to select complementary values against the
    // most recent opposite frame rather than the stale global best.
    std::vector<color_index_line> m_snapshot_picture_A; // latest A color rows
    std::vector<color_index_line> m_snapshot_picture_B; // latest B color rows

    // Mutation statistics
    int m_mutation_stats[E_MUTATION_MAX] = { 0 };

    // Thread configuration
    int m_thread_count = 1;

    // Start time for statistics
    time_t m_time_start;

    // Statistics tracking
    statistics_list m_statistics;
    unsigned m_last_statistics_seconds = 0;

    // DLAS specific fields
    double m_cost_max = DBL_MAX;                  // Maximum cost threshold
    int m_N = 0;                                  // Count of cost_max entries
    std::vector<double> m_previous_results;       // History list for DLAS
    size_t m_previous_results_index = 0;          // Current index in history
    double m_current_cost = DBL_MAX;              // Current accepted cost
    int m_history_length_config = 1;              // Desired history length (/s)

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

    // Statistics helper - call with m_mutex held
    void CollectStatisticsTickUnsafe();

    // --- Dual-frame blend mode configuration/state ---
public:
    bool m_dual_mode = false;
    e_blend_space m_blend_space = E_BLEND_YUV;
    e_distance_function m_blend_distance = E_DISTANCE_YUV;
    double m_blend_gamma = 2.2;
    double m_blend_gamma_inv = 1.0 / 2.2;
    double m_flicker_luma_weight = 0.0;
    double m_flicker_chroma_weight = 0.0;
    e_dual_strategy m_dual_strategy = E_DUAL_STRAT_ALTERNATE;
    e_dual_init m_dual_init = E_DUAL_INIT_DUP;
    double m_dual_mutate_ratio = 0.5;
    // Cross-frame structural ops probabilities
    double m_dual_cross_share_prob = 0.05;  // copy/swap line A<->B probability
    double m_dual_both_frames_prob = 0.0;   // reserved for future small pair tweak
    // Staged dual params
    unsigned long long m_dual_stage_evals = 5000; // per-thread iterations per stage
    bool m_dual_stage_start_B = false;            // start focusing B first
    // Global staged state shared by all threads
    std::atomic<unsigned long long> m_dual_stage_counter{0};
    std::atomic<bool> m_dual_stage_focus_B{false};

    // Flicker weight ramp
    unsigned long long m_blink_ramp_evals = 0; // 0 disables ramp
    double m_flicker_luma_weight_initial = 1.0; // starting WL if ramp enabled

    // Palette YUV and target per-pixel YUV (for YUV blend/distance fast path)
    float m_palette_y[128] = {0};
    float m_palette_u[128] = {0};
    float m_palette_v[128] = {0};
    // Palette linear RGB for rgb-linear blending
    float m_palette_lin_r[128] = {0};
    float m_palette_lin_g[128] = {0};
    float m_palette_lin_b[128] = {0};
    std::vector<float> m_target_y; // size = width*height
    std::vector<float> m_target_u;
    std::vector<float> m_target_v;
    // Flicker heatmap (per-pixel luma delta) for diagnostics
    std::vector<unsigned char> m_flicker_heatmap; // size = width*height, 0..255
    // Target Lab for CIE distances (optional)
    std::vector<float> m_target_L; // Lab L
    std::vector<float> m_target_a;
    std::vector<float> m_target_b;

    // Prepare YUV precomputations (call after m_picture and dimensions are set)
    void PrecomputeDualTransforms();

    // Dual-mode generation counters: increment when frame A or B changes (for cache invalidation)
    std::atomic<unsigned long long> m_dual_generation_A{0};
    std::atomic<unsigned long long> m_dual_generation_B{0};

    // Optional pair tables (128x128) for dual-mode fast path
    bool m_have_pair_tables = false;
    std::vector<float> m_pair_Ysum; // size 16384
    std::vector<float> m_pair_Usum;
    std::vector<float> m_pair_Vsum;
    std::vector<float> m_pair_dY;
    std::vector<float> m_pair_dC;

    // Report evaluation for dual pair (A,B)
    bool ReportEvaluationResultDual(double result,
                                    raster_picture* picA,
                                    raster_picture* picB,
                                    const std::vector<const line_cache_result*>& line_results_A,
                                    const std::vector<const line_cache_result*>& line_results_B,
                                    const sprites_memory_t& sprites_memory_A,
                                    const sprites_memory_t& sprites_memory_B,
                                    Mutator* mutator,
                                    bool mutatedB,
                                    bool didCrossShare,
                                    bool didCrossSwap);

    // Dual-specific statistics (displayed in GUI)
    std::atomic<unsigned long long> m_stat_dualComplementValue{0};
    std::atomic<unsigned long long> m_stat_dualSeedAdd{0};
    std::atomic<unsigned long long> m_stat_crossCopyLine{0};
    std::atomic<unsigned long long> m_stat_crossSwapLine{0};
    std::atomic<unsigned long long> m_stat_bothFramesTweak{0}; // reserved for future

private:
    // Thread management
    std::vector<std::thread> m_worker_threads;
    std::vector<Executor*> m_executors;
};

#endif // EVALUATION_CONTEXT_H
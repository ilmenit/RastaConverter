#include "EvaluationContext.h"
#include "../execution/Executor.h"
#include "../mutation/Mutator.h"
#include <cfloat>
#include <algorithm>
#include <thread>
#include <iostream>
#include <sstream>
#include <cmath>
#include "../TargetPicture.h"

EvaluationContext::EvaluationContext()
    : m_best_result(DBL_MAX)
    , m_previous_results_index(0)
    , m_cost_max(DBL_MAX)
    , m_N(0)
    , m_current_cost(DBL_MAX)
    , m_time_start(time(NULL))
    , m_use_regional_mutation(false)
{
    // Initialize atomic counter to 0
    m_threads_active.store(0);
    
    memset(m_mutation_stats, 0, sizeof(m_mutation_stats));
    memset(&m_sprites_memory, 0, sizeof(m_sprites_memory));
    memset(&m_sprites_memory_B, 0, sizeof(m_sprites_memory_B));
    m_previous_save_time = std::chrono::steady_clock::now();

    // Initialize empty created picture to avoid crashes during display
    m_created_picture.clear();
    m_created_picture_targets.clear();
}

EvaluationContext::~EvaluationContext()
{
    JoinWorkerThreads();
}
void EvaluationContext::CollectStatisticsTickUnsafe()
{
    time_t now = time(NULL);
    unsigned seconds = (unsigned)(now - m_time_start);
    if (seconds > m_last_statistics_seconds) {
        m_last_statistics_seconds = seconds;
        statistics_point pt;
        pt.evaluations = (unsigned)std::min<unsigned long long>(m_evaluations, UINT32_MAX);
        pt.seconds = seconds;
        pt.distance = m_best_result;
        m_statistics.push_back(pt);
    }
}

// Helper: RGB to YUV (same coefficients as RGByuvDistance)
static inline void rgb_to_yuv_fast(const rgb& c, float& y, float& u, float& v) {
    float r = (float)c.r;
    float g = (float)c.g;
    float b = (float)c.b;
    y = 0.299f*r + 0.587f*g + 0.114f*b;
    float dy = 0.0f; // not needed here
    u = (b - y) * 0.565f;
    v = (r - y) * 0.713f;
}

static inline void srgb_to_linear(float srgb, float gamma, float& linear)
{
    // general gamma (approx); sRGB exact curve not required here
    linear = powf(std::max(0.0f, srgb / 255.0f), gamma);
}

void EvaluationContext::PrecomputeDualTransforms()
{
    if (!m_dual_mode) return;
    // Palette transforms
    for (int i = 0; i < 128; ++i) {
        rgb_to_yuv_fast(atari_palette[i], m_palette_y[i], m_palette_u[i], m_palette_v[i]);
        float lr, lg, lb;
        srgb_to_linear((float)atari_palette[i].r, (float)m_blend_gamma, lr);
        srgb_to_linear((float)atari_palette[i].g, (float)m_blend_gamma, lg);
        srgb_to_linear((float)atari_palette[i].b, (float)m_blend_gamma, lb);
        m_palette_lin_r[i] = lr;
        m_palette_lin_g[i] = lg;
        m_palette_lin_b[i] = lb;
    }
    // Target transforms
    const unsigned total = m_width * m_height;
    m_target_y.resize(total);
    m_target_u.resize(total);
    m_target_v.resize(total);
    m_target_L.resize(total);
    m_target_a.resize(total);
    m_target_b.resize(total);
    if (m_picture) {
        unsigned idx = 0;
        for (unsigned y = 0; y < m_height; ++y) {
            const screen_line& row = m_picture[y];
            for (unsigned x = 0; x < m_width; ++x) {
                float Y, U, V;
                rgb_to_yuv_fast(row[x], Y, U, V);
                m_target_y[idx] = Y;
                m_target_u[idx] = U;
                m_target_v[idx] = V;
                // Lab for CIE distances
                Lab labpx; RGB2LAB(row[x], labpx);
                m_target_L[idx] = (float)labpx.L;
                m_target_a[idx] = (float)labpx.a;
                m_target_b[idx] = (float)labpx.b;
                ++idx;
            }
        }
    }
}

bool EvaluationContext::MarkFinished(const char* reason, const char* file, int line)
{
    bool expected = false;
    if (m_finished.compare_exchange_strong(expected, true)) {
        // First time we set finished -> capture reason and context.
        // Avoid deadlock if caller already holds m_mutex (common in STOP paths).
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        const bool acquired = lock.try_lock();
        m_finish_reason = reason ? reason : "(no-reason)";
        m_finish_file = file ? file : "?";
        m_finish_line = line;
        m_finish_evals_at = m_evaluations;
        // Capture a rough thread id string for debugging (keep simple)
        m_finish_thread = "tid";
        #ifdef THREAD_DEBUG
        std::cout << "[FIN] MarkFinished: reason='" << m_finish_reason
                  << "' at " << m_finish_file << ":" << m_finish_line
                  << ", evals=" << m_finish_evals_at
                  << ", threads_active=" << m_threads_active.load()
                  << ", initialized=" << (m_initialized ? 1 : 0)
                  << ", best_result=" << m_best_result
                  << ", cost_max=" << m_cost_max
                  << ", N=" << m_N
                  << ", current_cost=" << m_current_cost
                  << ", previous_idx=" << m_previous_results_index
                  << ", thread=" << m_finish_thread
                  << std::endl;
        #endif
        // Notify without requiring the lock
        m_condvar_update.notify_all();
        return true;
    }
    return false;
}

void EvaluationContext::ReportInitialScore(double score)
{
    // Do not need to acquire the mutex here, as it should already be locked by the caller
    std::cout << "Reporting initial score: " << score << std::endl;
    
    if (!m_initialized) {
        // Initialize DLAS parameters with the first score
        const double init_margin = score * 0.1; // 10% margin
        m_cost_max = score + init_margin;
        m_current_cost = score;
        
        // Use the user-configured solutions parameter for history size
        // Use configured history length if provided; fallback to thread count at least 1
        int solutions_size = (m_history_length_config > 0) ? m_history_length_config : (m_thread_count > 1 ? m_thread_count : 1);
        
        // Make sure we have enough space and all entries are initialized
        if (m_previous_results.empty()) {
            m_previous_results.resize(solutions_size, m_cost_max);
        } else if (m_previous_results.size() != solutions_size) {
            m_previous_results.resize(solutions_size, m_cost_max);
        }
        
        m_N = solutions_size;
        
        // Only now mark as initialized
        m_initialized = true;
        m_update_initialized = true;
        m_condvar_update.notify_all();
        
        std::cout << "DLAS initialization complete. Cost max: " << m_cost_max 
                  << ", Solutions size: " << solutions_size << std::endl;
    }
}

bool EvaluationContext::ReportEvaluationResult(double result, 
                                             raster_picture* pic,
                                             const std::vector<const line_cache_result*>& line_results,
                                             const sprites_memory_t& sprites_memory,
                                             Mutator* mutator)
{
    bool accepted = false;
    
    // Do not need to acquire the mutex here, as it should already be locked by the caller
    
    // Validate parameters
    if (!pic || !m_initialized || m_previous_results.empty()) {
        std::cerr << "Invalid parameters in ReportEvaluationResult" << std::endl;
        return false;
    }
    
    // Check termination condition first - this should be checked regardless of other logic
    if (m_max_evals > 0 && m_evaluations >= m_max_evals) {
        std::cout << "[FIN] EvaluationContext: max_evals reached (" << m_evaluations << "/" << m_max_evals << ")" << std::endl;
        CTX_MARK_FINISHED((*this), "max_evals_reached");
        return false;
    }
    
    // Store previous cost before potential update
    double prev_cost = m_current_cost;
    
    // Calculate index for circular array (protect against empty vector)
    if (m_previous_results.size() == 0) {
        // This should never happen if initialization is correct
        std::cerr << "Previous results vector is empty in ReportEvaluationResult" << std::endl;
        return false;
    }
    
    size_t l = m_previous_results_index % m_previous_results.size();
    
    // DLAS acceptance criteria
    if (result == m_current_cost || result < m_cost_max) {
        // Accept the candidate solution
        m_current_cost = result;
        accepted = true;
    }
    
    // DLAS replacement strategy
    if (m_current_cost > m_previous_results[l]) {
        m_previous_results[l] = m_current_cost;
    }
    else if (m_current_cost < m_previous_results[l] && m_current_cost < prev_cost) {
        // Track if we're removing a max value
        if (m_previous_results[l] == m_cost_max) {
            --m_N;
        }
        
        // Replace the value
        m_previous_results[l] = m_current_cost;
        
        // Recompute max and N if needed
        if (m_N <= 0) {
            // Find new cost_max
            m_cost_max = *std::max_element(
                m_previous_results.begin(),
                m_previous_results.end()
            );
            
            // Recount occurrences of max
            m_N = std::count(
                m_previous_results.begin(),
                m_previous_results.end(),
                m_cost_max
            );
        }
    }
    
    // Always increment index
    ++m_previous_results_index;
    
    // Update best solution if better than current best
    if (result < m_best_result) {
        m_last_best_evaluation = m_evaluations;
        m_best_pic = *pic;
        m_best_pic.uncache_insns();
        m_best_result = result;

        // Update visualization state
        try {
            m_created_picture.resize(m_height);
            m_created_picture_targets.resize(m_height);

            for (int y = 0; y < (int)m_height; ++y) {
                if (y < (int)line_results.size() && line_results[y] != nullptr) {
                    const line_cache_result& lcr = *line_results[y];
                    m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_width);
                    m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_width);
                }
                else {
                    // Handle null or missing line result
                    if (!m_created_picture[y].empty()) {
                        // Keep existing data if available
                    }
                    else {
                        // Initialize with zeros (black)
                        m_created_picture[y].assign(m_width, 0);
                        m_created_picture_targets[y].assign(m_width, E_COLBAK);
                    }
                }
            }

            // Copy sprites memory
            memcpy(&m_sprites_memory, &sprites_memory, sizeof(m_sprites_memory));
        }
        catch (const std::exception& e) {
            std::cerr << "Error updating visualization data: " << e.what() << std::endl;
        }
        
        // Get mutation statistics from the mutator
        if (mutator) {
            try {
                const int* current_mutations = mutator->GetCurrentMutations();
                for (int i = 0; i < E_MUTATION_MAX; ++i) {
                    if (current_mutations[i]) {
                        m_mutation_stats[i] += current_mutations[i];
                    }
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Error updating mutation statistics: " << e.what() << std::endl;
            }
        }

        m_update_improvement = true;
        m_condvar_update.notify_all();
        
        // Log best solution improvement (can be suppressed by build flag)
        #ifndef SUPPRESS_IMPROVEMENT_LOGS
        std::cout << "New best solution: " << result << " (evaluation #" << m_evaluations << ")" << std::endl;
        #endif
        
        // Check for perfect solution (zero distance)
        if (result == 0) {
            std::cout << "[FIN] Perfect solution found!" << std::endl;
            CTX_MARK_FINISHED((*this), "perfect_solution");
        }
    }
    
    // Check for auto-save condition
    if (m_save_period > 0 && m_evaluations % m_save_period == 0) {
        m_update_autosave = true;
        m_condvar_update.notify_all();
    }
    
    return accepted;
}

bool EvaluationContext::ReportEvaluationResultDual(double result,
                                                       raster_picture* picA,
                                                       raster_picture* picB,
                                                       const std::vector<const line_cache_result*>& line_results_A,
                                                       const std::vector<const line_cache_result*>& line_results_B,
                                                       const sprites_memory_t& sprites_memory_A,
                                                       const sprites_memory_t& sprites_memory_B,
                                                       Mutator* mutator)
{
    // Reuse DLAS acceptance and best-tracking, but update both best pictures and a blended preview
    bool accepted = false;
    if (!picA || !picB || !m_initialized || m_previous_results.empty()) {
        std::cerr << "Invalid parameters in ReportEvaluationResultDual" << std::endl;
        return false;
    }

    if (m_max_evals > 0 && m_evaluations >= m_max_evals) {
        std::cout << "[FIN] EvaluationContext: max_evals reached (" << m_evaluations << "/" << m_max_evals << ")" << std::endl;
        CTX_MARK_FINISHED((*this), "max_evals_reached");
        return false;
    }

    double prev_cost = m_current_cost;
    size_t l = m_previous_results_index % m_previous_results.size();
    if (result == m_current_cost || result < m_cost_max) {
        m_current_cost = result;
        accepted = true;
    }
    if (m_current_cost > m_previous_results[l]) {
        m_previous_results[l] = m_current_cost;
    } else if (m_current_cost < m_previous_results[l] && m_current_cost < prev_cost) {
        if (m_previous_results[l] == m_cost_max) --m_N;
        m_previous_results[l] = m_current_cost;
        if (m_N <= 0) {
            m_cost_max = *std::max_element(m_previous_results.begin(), m_previous_results.end());
            m_N = std::count(m_previous_results.begin(), m_previous_results.end(), m_cost_max);
        }
    }
    ++m_previous_results_index;

    if (result < m_best_result) {
        m_last_best_evaluation = m_evaluations;
        m_best_pic = *picA; m_best_pic.uncache_insns();
        m_best_pic_B = *picB; m_best_pic_B.uncache_insns();
        m_best_result = result;

        // Update visualization: store A and B created pictures (GUI can blend)
        try {
            m_created_picture.resize(m_height);
            m_created_picture_targets.resize(m_height);
            m_created_picture_B.resize(m_height);
            m_created_picture_targets_B.resize(m_height);
            for (int y = 0; y < (int)m_height; ++y) {
                if (y < (int)line_results_A.size() && line_results_A[y] != nullptr) {
                    const line_cache_result& lcr = *line_results_A[y];
                    m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_width);
                    m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_width);
                } else {
                    if (m_created_picture[y].empty()) {
                        m_created_picture[y].assign(m_width, 0);
                        m_created_picture_targets[y].assign(m_width, E_COLBAK);
                    }
                }
                if (y < (int)line_results_B.size() && line_results_B[y] != nullptr) {
                    const line_cache_result& lcrB = *line_results_B[y];
                    m_created_picture_B[y].assign(lcrB.color_row, lcrB.color_row + m_width);
                    m_created_picture_targets_B[y].assign(lcrB.target_row, lcrB.target_row + m_width);
                } else {
                    if (m_created_picture_B[y].empty()) {
                        m_created_picture_B[y].assign(m_width, 0);
                        m_created_picture_targets_B[y].assign(m_width, E_COLBAK);
                    }
                }
            }
            memcpy(&m_sprites_memory, &sprites_memory_A, sizeof(m_sprites_memory));
            memcpy(&m_sprites_memory_B, &sprites_memory_B, sizeof(m_sprites_memory_B));
        } catch (const std::exception& e) {
            std::cerr << "Error updating temporal visualization data: " << e.what() << std::endl;
        }

        if (mutator) {
            try {
                const int* current_mutations = mutator->GetCurrentMutations();
                for (int i = 0; i < E_MUTATION_MAX; ++i) {
                    if (current_mutations[i]) m_mutation_stats[i] += current_mutations[i];
                }
            } catch (...) {}
        }
        m_update_improvement = true;
        m_condvar_update.notify_all();
        #ifndef SUPPRESS_IMPROVEMENT_LOGS
        std::cout << "New best dual solution: " << result << " (#" << m_evaluations << ")" << std::endl;
        #endif
        if (result == 0) {
            std::cout << "[FIN] Perfect dual solution found!" << std::endl;
            CTX_MARK_FINISHED((*this), "perfect_solution");
        }
    }

    if (m_save_period > 0 && m_evaluations % m_save_period == 0) {
        m_update_autosave = true;
        m_condvar_update.notify_all();
    }
    return accepted;
}

void EvaluationContext::RegisterExecutor(Executor* executor)
{
    if (!executor) {
        std::cerr << "Cannot register null executor" << std::endl;
        return;
    }
    
    std::unique_lock<std::mutex> lock(m_mutex);
    m_executors.push_back(executor);
    
    // We don't increment m_threads_active here anymore - that's handled by StartWorkerThreads
    #if defined(THREAD_DEBUG) && !defined(NDEBUG)
    std::cout << "[CTX] Registered executor. Active threads: " << m_threads_active.load() << std::endl;
    #endif
}

void EvaluationContext::UnregisterExecutor(Executor* executor)
{
    if (!executor) {
        std::cerr << "Cannot unregister null executor" << std::endl;
        return;
    }
    
    std::unique_lock<std::mutex> lock(m_mutex);
    auto it = std::find(m_executors.begin(), m_executors.end(), executor);
    if (it != m_executors.end()) {
        m_executors.erase(it);
    #if defined(THREAD_DEBUG) && !defined(NDEBUG)
    std::cout << "[CTX] Unregistered executor" << std::endl;
    #endif
    }
    else {
        std::cerr << "Attempted to unregister executor that wasn't registered" << std::endl;
    }
}

int EvaluationContext::StartWorkerThreads(std::function<void(int)> threadFunc)
{
    // First, move out any previously created threads and join them outside the lock
    std::vector<std::thread> old_threads;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        old_threads.swap(m_worker_threads);
    }

    // Join previously finished threads to avoid std::terminate on reassignment
    for (auto& t : old_threads) {
        if (t.joinable()) {
            try { t.join(); } catch (...) {}
        }
    }

    // Now create the new set of worker threads
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // Prepare storage for worker threads
        m_worker_threads.resize(m_thread_count);

        // Set active thread count - this is now the source of truth for thread counting
        m_threads_active.store(m_thread_count);
        #if defined(THREAD_DEBUG) && !defined(NDEBUG)
        std::cout << "[CTX] Starting " << m_thread_count << " worker threads" << std::endl;
        #endif

        for (int i = 0; i < m_thread_count; i++) {
            try {
                m_worker_threads[i] = std::thread(threadFunc, i);
            }
            catch (const std::exception& e) {
                #if defined(THREAD_DEBUG) && !defined(NDEBUG)
                std::cerr << "[CTX] Error creating worker thread " << i << ": " << e.what() << std::endl;
                #endif
                // Adjust active count for failed threads
                m_threads_active.fetch_sub(1);
            }
        }
    }

    return m_threads_active.load();
}

void EvaluationContext::JoinWorkerThreads()
{
    std::vector<std::thread> threads_to_join;
    
    // Move threads to local vector to avoid potential deadlock
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        threads_to_join.swap(m_worker_threads);
    }
    
    // Join threads outside the lock
    for (auto& thread : threads_to_join) {
        if (thread.joinable()) {
            try {
                thread.join();
            }
            catch (const std::exception& e) {
                std::cerr << "Error joining worker thread: " << e.what() << std::endl;
            }
        }
    }
    
    #if defined(THREAD_DEBUG) && !defined(NDEBUG)
    std::cout << "[CTX] Joined all worker threads" << std::endl;
    #endif
}
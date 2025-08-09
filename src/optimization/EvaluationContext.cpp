#include "EvaluationContext.h"
#include "../execution/Executor.h"
#include "../mutation/Mutator.h"
#include <cfloat>
#include <algorithm>
#include <thread>
#include <iostream>
#include <sstream>

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
    m_previous_save_time = std::chrono::steady_clock::now();

    // Initialize empty created picture to avoid crashes during display
    m_created_picture.clear();
    m_created_picture_targets.clear();
}

EvaluationContext::~EvaluationContext()
{
    JoinWorkerThreads();
}

bool EvaluationContext::MarkFinished(const char* reason, const char* file, int line)
{
    bool expected = false;
    if (m_finished.compare_exchange_strong(expected, true)) {
        // First time we set finished -> capture reason and context
        std::unique_lock<std::mutex> lock(m_mutex);
        m_finish_reason = reason ? reason : "(no-reason)";
        m_finish_file = file ? file : "?";
        m_finish_line = line;
        m_finish_evals_at = m_evaluations;
        // Capture a rough thread id string for debugging
        std::ostringstream oss;
        // Represent thread id as pointer value on MSVC-friendly path
        auto tid = std::this_thread::get_id();
        m_finish_thread = "tid"; // simple tag; avoid operator<< ambiguity across stdlib versions
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
        int solutions_size = m_thread_count > 1 ? m_thread_count : 1;
        
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

void EvaluationContext::RegisterExecutor(Executor* executor)
{
    if (!executor) {
        std::cerr << "Cannot register null executor" << std::endl;
        return;
    }
    
    std::unique_lock<std::mutex> lock(m_mutex);
    m_executors.push_back(executor);
    
    // We don't increment m_threads_active here anymore - that's handled by StartWorkerThreads
    std::cout << "[CTX] Registered executor. Active threads: " << m_threads_active.load() << std::endl;
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
    std::cout << "[CTX] Unregistered executor" << std::endl;
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
        std::cout << "[CTX] Starting " << m_thread_count << " worker threads" << std::endl;

        for (int i = 0; i < m_thread_count; i++) {
            try {
                m_worker_threads[i] = std::thread(threadFunc, i);
            }
            catch (const std::exception& e) {
                std::cerr << "[CTX] Error creating worker thread " << i << ": " << e.what() << std::endl;
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
    
    std::cout << "[CTX] Joined all worker threads" << std::endl;
}
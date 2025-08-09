#include "DLAS.h"
#include "../mutation/RasterMutator.h"
#include <cmath>
#include <fstream>
#include <algorithm>
#include <functional>
#include <chrono>
#include <iostream>
extern bool quiet;

DLAS::DLAS(EvaluationContext* context, Executor* executor, Mutator* mutator, int solutions)
    : m_context(context)
    , m_executor(executor)
    , m_mutator(mutator)
    , m_running(false)
    , m_solutions(solutions > 0 ? solutions : 1)
    , m_init_completed(false)
{
    if (!context) {
        std::cerr << "Error: DLAS constructor received null EvaluationContext" << std::endl;
    }
    if (!executor) {
        std::cerr << "Error: DLAS constructor received null Executor" << std::endl;
    }
    if (!mutator) {
        std::cerr << "Error: DLAS constructor received null Mutator" << std::endl;
    }
}

DLAS::~DLAS()
{
    // Signal all threads to stop
    m_running = false;

    // Wake up any threads waiting on the initialization condition
    {
        std::unique_lock<std::mutex> init_lock(m_init_mutex);
        m_init_completed = true;  // Just to make waiting threads continue
        m_init_condvar.notify_all();
    }

    // Wake up any threads waiting on the evaluation context
    if (m_context) {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        std::cout << "[FIN] DLAS destructor: setting finished and notifying; threads_active="
                  << m_context->m_threads_active.load() << std::endl;
        if (!m_context->m_finished.load()) CTX_MARK_FINISHED((*m_context), "dlas_destructor");
        m_context->m_condvar_update.notify_all();
    }

    // Wait for the threads to finish
    if (m_context) {
        m_context->JoinWorkerThreads();
    }

    // Clear thread mutators
    m_thread_mutators.clear();
}

void DLAS::Initialize(const raster_picture& initialSolution)
{
    if (!m_context) {
        std::cerr << "Error: Cannot initialize DLAS with null context" << std::endl;
        return;
    }

    std::unique_lock<std::mutex> lock(m_context->m_mutex);

    // Store the initial solution
    m_context->m_best_pic = initialSolution;

    // Make sure it's properly cached
    try {
        m_context->m_best_pic.recache_insns(m_context->m_insn_seq_cache, m_context->m_linear_allocator);
    }
    catch (const std::exception& e) {
        std::cerr << "Error initializing DLAS: Failed to cache instructions: " << e.what() << std::endl;
    }

    // Initialize thread-specific mutators
    try {
        m_thread_mutators.resize(m_context->m_thread_count);
        unsigned long long seed = time(NULL);
        for (int i = 0; i < m_context->m_thread_count; ++i)
        {
            m_thread_mutators[i] = std::make_unique<RasterMutator>(m_context, i);
            m_thread_mutators[i]->Init(seed + i * 123456789ULL);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error initializing thread mutators: " << e.what() << std::endl;
    }
}

double DLAS::EvaluateInitialSolution()
{
    if (quiet) std::cout << "Evaluating initial solution..." << std::endl;
    
    if (!m_context || !m_executor) {
        std::cerr << "Error: Cannot evaluate initial solution with null context or executor" << std::endl;
        return DBL_MAX;
    }

    try {
        // Create a copy of the initial solution
        raster_picture pic = m_context->m_best_pic;

        // Allocate storage for line results
        std::vector<const line_cache_result*> line_results(m_context->m_height);

        // Evaluate the initial solution
        double result = m_executor->ExecuteRasterProgram(&pic, line_results.data());
        if (quiet) std::cout << "Initial solution evaluation: " << result << std::endl;

        // Update the visualization data for the initial solution
        {
            std::unique_lock<std::mutex> lock(m_context->m_mutex);

            // Set this as the best result since it's the first
            m_context->m_best_result = result;

            // Copy created picture data
            m_context->m_created_picture.resize(m_context->m_height);
            m_context->m_created_picture_targets.resize(m_context->m_height);

            // Keep track of the last valid line result for fallback
            const line_cache_result* last_valid_result = nullptr;

            for (int y = 0; y < (int)m_context->m_height; ++y) {
                if (line_results[y] != nullptr) {
                    // Store this as the last valid result
                    last_valid_result = line_results[y];

                    // Copy the line data
                    const line_cache_result& lcr = *line_results[y];
                    m_context->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_context->m_width);
                    m_context->m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_context->m_width);
                }
                else {
                    // Handle null pointer case
                    if (last_valid_result != nullptr) {
                        // Use the last valid result as a fallback
                        m_context->m_created_picture[y].assign(last_valid_result->color_row,
                            last_valid_result->color_row + m_context->m_width);
                        m_context->m_created_picture_targets[y].assign(last_valid_result->target_row,
                            last_valid_result->target_row + m_context->m_width);
                    }
                    else {
                        // No valid results yet, initialize with zeros (black)
                        m_context->m_created_picture[y].assign(m_context->m_width, 0);
                        m_context->m_created_picture_targets[y].assign(m_context->m_width, E_COLBAK);
                    }
                }
            }

            // Copy sprite memory
            memcpy(&m_context->m_sprites_memory, &m_executor->GetSpritesMemory(), sizeof(m_context->m_sprites_memory));

            // Report the initial score to initialize DLAS
            m_context->ReportInitialScore(result);

            // Make sure it's marked as initialized
            m_context->m_initialized = true;
            m_context->m_update_initialized = true;
            m_context->m_condvar_update.notify_all();
        }

        return result;
    }
    catch (const std::exception& e) {
        std::cerr << "Error evaluating initial solution: " << e.what() << std::endl;
        return DBL_MAX;
    }
    catch (...) {
        std::cerr << "Unknown error evaluating initial solution" << std::endl;
        return DBL_MAX;
    }
}

// Fix for DLAS::RunWorker thread completion section
void DLAS::RunWorker(int threadId)
{
    #ifdef THREAD_DEBUG
    std::cout << "[DLAS] Starting worker thread " << threadId << std::endl;
    #endif
    
    // Helper to safely decrement active-thread count on any early return
    auto signal_thread_exit = [this, threadId]() {
        if (m_context) {
            std::unique_lock<std::mutex> lock(m_context->m_mutex);
            int prev = m_context->m_threads_active.fetch_sub(1);
            int remaining = prev - 1;
            if (remaining < 0) {
                // Saturate at 0 to avoid negative active count due to double-decrement bugs
                m_context->m_threads_active.store(0);
                remaining = 0;
                #ifdef THREAD_DEBUG
                std::cout << "[DLAS] Worker " << threadId << " early-exit caused negative thread count; clamped to 0" << std::endl;
                #endif
            } else {
                #ifdef THREAD_DEBUG
                std::cout << "[DLAS] Worker " << threadId << " early-exit. Remaining threads: " << remaining << std::endl;
                #endif
            }
            m_context->m_condvar_update.notify_all();
        }
    };

    if (!m_running) {
        std::cerr << "Worker " << threadId << " exiting - not running" << std::endl;
        signal_thread_exit();
        return;
    }

    if (!m_context) {
        std::cerr << "Worker " << threadId << " exiting - null context" << std::endl;
        signal_thread_exit();
        return;
    }

    try {
        // If this is thread 0, evaluate the initial solution first
        if (threadId == 0 && !m_context->m_initialized) {
            if (quiet) std::cout << "Thread 0 evaluating initial solution" << std::endl;
            double initial_result = EvaluateInitialSolution();

            if (initial_result == DBL_MAX) {
                std::cerr << "Initial solution evaluation failed" << std::endl;
                m_running = false;
                signal_thread_exit();
                return;
            }

            // After evaluation, signal that initialization is complete
            {
                std::unique_lock<std::mutex> init_lock(m_init_mutex);
                m_init_completed = true;
                m_init_condvar.notify_all();
            }
            #ifdef THREAD_DEBUG
            if (quiet) std::cout << "Thread 0 completed initialization" << std::endl;
            #endif
        }
        else if (!m_context->m_initialized) {
            if (quiet) std::cout << "Thread " << threadId << " waiting for initialization" << std::endl;
            
            // Other threads wait for initialization to complete
            std::unique_lock<std::mutex> init_lock(m_init_mutex);
            m_init_condvar.wait(init_lock, [this] { return m_init_completed || !m_running; });

            #ifdef THREAD_DEBUG
            if (quiet) std::cout << "Thread " << threadId << " continuing after initialization wait" << std::endl;
            #endif

            // Double-check initialization with the context lock
            if (!m_context->m_initialized) {
                std::unique_lock<std::mutex> context_lock(m_context->m_mutex);
                if (!m_context->m_initialized) {
                    // Still not initialized - just wait
                    #ifdef THREAD_DEBUG
                    std::cout << "Thread " << threadId << " still waiting for initialization with context lock" << std::endl;
                    #endif
                    m_context->m_condvar_update.wait(context_lock, [this] {
                        return m_context->m_initialized || !m_running;
                        });
                }
            }
        }

        // If not initialized, wait (do not exit). If not running, exit.
        if (!m_running) {
            signal_thread_exit();
            return;
        }
        if (!m_context->m_initialized) {
            // Wait until initialization completes or running is turned off
            std::unique_lock<std::mutex> init_lock(m_init_mutex);
            m_init_condvar.wait(init_lock, [this] { return m_init_completed || !m_running; });
            if (!m_running) {
                signal_thread_exit();
                return;
            }
        }

        // Create per-thread caches to avoid cross-thread races
        linear_allocator thread_allocator;
        insn_sequence_cache thread_cache;

        // Copy best picture to work with
        raster_picture pic = m_context->m_best_pic;
        double best_result = m_context->m_best_result;

        // Create local executor for this thread
        Executor local_executor;
        try {
            local_executor.Init(m_context->m_width,
                m_context->m_height,
                m_context->m_picture_all_errors,
                m_context->m_picture,
                m_context->m_onoff,
                m_context,
                m_solutions,
                time(NULL) + threadId * 123456789ULL,
                m_context->m_cache_size,
                m_thread_mutators[threadId].get(),
                threadId);
        }
        catch (const std::exception& e) {
            std::cerr << "Error initializing executor in thread " << threadId << ": " << e.what() << std::endl;
            signal_thread_exit();
            return;
        }

        // Track if the executor is registered to ensure cleanup
        bool executor_registered = false;
        
        try {
            // Register the executor with the context and log periodically for visibility
            #ifdef THREAD_DEBUG
            std::cout << "[DLAS] Thread " << threadId << " registering executor" << std::endl;
            #endif
            local_executor.Start();
            executor_registered = true;

            std::vector<const line_cache_result*> line_results(m_context->m_height);
            // Hysteresis/cooldown for cache clearing to avoid thrash
            auto last_cache_clear_time = std::chrono::steady_clock::now();
            unsigned long long last_cache_clear_evals = 0;

            // Main optimization loop
            int iteration_count = 0;
            #ifdef THREAD_DEBUG
            std::cout << "[DLAS] Thread " << threadId << " starting main optimization loop" << std::endl;
            #endif
            
            while (m_running && !m_context->m_finished.load())
            {
                // Periodic status update
                if (++iteration_count % 2000 == 0) {
                    std::cout << "[DLAS] Thread " << threadId << " iterations=" << iteration_count
                              << " evals=" << m_context->m_evaluations
                              << " finished=" << m_context->m_finished.load()
                              << " active=" << m_context->m_threads_active.load() << std::endl;
                }
                
                // Check if we need to clear caches due to memory usage with cooldown to prevent thrash
                // IMPORTANT: Monitor the executor's allocator (line caches), not just the recache allocator
                if (local_executor.GetCacheMemoryUsage() > m_context->m_cache_size)
                {
                    auto now = std::chrono::steady_clock::now();
                    bool cooldown_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cache_clear_time).count() > 1000;
                    bool evals_progressed = (m_context->m_evaluations - last_cache_clear_evals) > 5000;

                    if (cooldown_elapsed && evals_progressed)
                    {
                        std::unique_lock<std::mutex> cache_lock(m_context->m_cache_mutex);

                        // Check again after acquiring the lock (another thread might have cleared)
                        if (local_executor.GetCacheMemoryUsage() > m_context->m_cache_size)
                        {
                            // Clear the executor (line) caches first
                            local_executor.ClearLineCaches();

                            // Also clear the recache allocators (less critical but safe)
                            thread_cache.clear();
                            thread_allocator.clear();
                            pic.recache_insns(thread_cache, thread_allocator);

                            // Update cooldown trackers
                            last_cache_clear_time = now;
                            last_cache_clear_evals = m_context->m_evaluations;
                        }
                    }
                }

                // Create a new candidate by mutation
                raster_picture new_pic = pic;
                try {
                    m_thread_mutators[threadId]->MutateProgram(&new_pic);
                }
                catch (const std::exception& e) {
                    std::cerr << "Error in mutation in thread " << threadId << ": " << e.what() << std::endl;
                    continue;
                }

                try {
                    // Evaluate the new candidate
                    double result = (double)local_executor.ExecuteRasterProgram(&new_pic, line_results.data());

                    // Update global state with the evaluation
                    std::unique_lock<std::mutex> lock(m_context->m_mutex);

                    ++m_context->m_evaluations;

                    // Check for termination condition (respect max_evals only when > 0)
                    if (((m_context->m_max_evals > 0) && (m_context->m_evaluations >= m_context->m_max_evals)) || m_context->m_finished.load()) {
                        std::cout << "[FIN] DLAS worker " << threadId << ": finish condition met (finished="
                                  << m_context->m_finished.load() << ", max_evals=" << m_context->m_max_evals
                                  << ", evals=" << m_context->m_evaluations << ")" << std::endl;
                        CTX_MARK_FINISHED((*m_context), "dlas_worker_finish_guard");
                        break;
                    }

                    // Report to DLAS algorithm for acceptance decision and best solution tracking
                    bool accepted = m_context->ReportEvaluationResult(
                        result,
                        &new_pic,
                        line_results,
                        local_executor.GetSpritesMemory(),
                        m_thread_mutators[threadId].get()
                    );

                    // If accepted, update our local copy
                    if (accepted) {
                        pic = new_pic;
                    }

                    // Update local best if global best improved
                    if (m_context->m_best_result < best_result)
                    {
                        pic = m_context->m_best_pic;
                        best_result = m_context->m_best_result;
                        // Re-cache using this worker's thread-local caches, not the DLAS-wide members
                        pic.recache_insns(thread_cache, thread_allocator);
                    }

                    // Allow other threads to process by releasing the lock
                    lock.unlock();
                }
                catch (const std::bad_alloc& e) {
                    std::cerr << "[OOM] Memory allocation error in thread " << threadId
                              << ": " << e.what() << ", exec_cache_used="
                              << local_executor.GetCacheMemoryUsage() << "/" << m_context->m_cache_size
                              << std::endl;
                    
                    // Clear executor caches first, then recache program data
                    try {
                        local_executor.ClearLineCaches();
                        thread_cache.clear();
                        thread_allocator.clear();
                        pic.recache_insns(thread_cache, thread_allocator);
                    }
                    catch (...) {
                        std::cerr << "[OOM] Failed to recover from memory error in thread " << threadId << std::endl;
                        break;
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Error in evaluation in thread " << threadId << ": " << e.what() << std::endl;
                    continue;
                }
                catch (...) {
                    std::cerr << "Unknown non-std exception in evaluation in thread " << threadId << std::endl;
                    continue;
                }
            }
            
            #ifdef THREAD_DEBUG
            std::cout << "[DLAS] Thread " << threadId << " exiting main loop after " << iteration_count << " iterations" << std::endl;
            #endif
        }
        catch (const std::exception& e) {
            std::cerr << "Error in worker thread " << threadId << ": " << e.what() << std::endl;
            
            // Ensure we unregister the executor before re-throwing
            if (executor_registered && m_context) {
                try {
                    m_context->UnregisterExecutor(&local_executor);
                }
                catch (...) {
                    // Suppress any exceptions in cleanup
                }
            }
        }
        catch (...) {
            std::cerr << "Unknown error in worker thread " << threadId << std::endl;
            
            // Ensure we unregister the executor
            if (executor_registered && m_context) {
                try {
                    m_context->UnregisterExecutor(&local_executor);
                }
                catch (...) {
                    // Suppress any exceptions in cleanup
                }
            }
        }

            // Unregister and signal completion
        if (executor_registered && m_context) {
            try {
                m_context->UnregisterExecutor(&local_executor);
            }
            catch (const std::exception& e) {
                std::cerr << "Error unregistering executor in thread " << threadId << ": " << e.what() << std::endl;
            }
        }

        // Signal that this thread has completed
        if (m_context) {
                std::unique_lock<std::mutex> lock(m_context->m_mutex);

                // Decrement active thread count - using fetch_sub for atomic decrement
                int prev = m_context->m_threads_active.fetch_sub(1);
                int remaining = prev - 1;
                if (remaining < 0) {
                    m_context->m_threads_active.store(0);
                    remaining = 0;
                    #ifdef THREAD_DEBUG
                    std::cout << "[DLAS] Thread " << threadId << " completed; negative thread count detected; clamped to 0" << std::endl;
                    #endif
                } else {
                    #ifdef THREAD_DEBUG
                    std::cout << "[DLAS] Thread " << threadId << " completed. Remaining threads: " << remaining << std::endl;
                    #endif
                }

                // If this was the last thread and we're not finished, do NOT set finished here.
                // Leave decision to control thread (Run) which may re-spawn workers.

                // Always notify all waiting threads to prevent lost notifications
                m_context->m_condvar_update.notify_all();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Unexpected error in worker thread " << threadId << ": " << e.what() << std::endl;
        // Unified decrement
        signal_thread_exit();
    }
    catch (...) {
        std::cerr << "Unknown unexpected error in worker thread " << threadId << std::endl;
        // Unified decrement
        signal_thread_exit();
    }
    
    #ifdef THREAD_DEBUG
    std::cout << "[DLAS] Worker thread " << threadId << " finished" << std::endl;
    #endif
}

// Fix for DLAS::Run method
void DLAS::Run()
{
    if (quiet) std::cout << "Starting DLAS optimization" << std::endl;
    
    m_running = true;
    m_init_completed = false;

    // Reset the evaluation context state
    if (m_context) {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        m_context->m_finished.store(false);
        // Do not touch m_initialized here; it is set by EvaluateInitialSolution
    }
    else {
        std::cerr << "Error: Cannot run DLAS with null context" << std::endl;
        return;
    }

    // Create worker threads through the evaluation context
    try {
        m_context->StartWorkerThreads(std::bind(&DLAS::RunWorker, this, std::placeholders::_1));
    }
    catch (const std::exception& e) {
        std::cerr << "Error starting worker threads: " << e.what() << std::endl;
        m_running = false;
        return;
    }

    // Wait for all threads to complete
    try {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        std::cout << "[CTL] Control thread entering wait loop" << std::endl;
        
        // CRITICAL FIX: Improved wait condition and handling
        // Wait until either:
        // 1. m_running becomes false (external stop request)
        // 2. m_finished becomes true (goal reached or max evals reached)
        // 3. No more active threads (all completed or failed)
        
wait_again:
        while (m_running && !m_context->m_finished.load() && m_context->m_threads_active.load() > 0) {
            // Wait with a timeout to prevent indefinite waiting
            auto status = m_context->m_condvar_update.wait_for(lock, std::chrono::seconds(5));
            
            // If we timed out, log the status and continue waiting
            if (status == std::cv_status::timeout) {
                #ifdef THREAD_DEBUG
                std::cout << "[DLAS] Wait timeout - active threads: " << m_context->m_threads_active.load() 
                          << ", running: " << m_running 
                          << ", finished: " << m_context->m_finished.load() << std::endl;
                #endif
            }
        }

        std::cout << "[CTL] Wait finished: running=" << m_running
                  << ", finished=" << m_context->m_finished.load()
                  << ", active_threads=" << m_context->m_threads_active.load() << std::endl;

        // If threads all finished unexpectedly and we have no max evals set, re-spawn workers
        if (!m_context->m_finished.load() && m_context->m_threads_active.load() <= 0 && m_running) {
            #ifdef THREAD_DEBUG
            std::cout << "[DLAS] All workers exited but not finished; attempting to re-spawn workers" << std::endl;
            #endif
            // Recreate workers
            lock.unlock();
            try {
                int started = m_context->StartWorkerThreads(std::bind(&DLAS::RunWorker, this, std::placeholders::_1));
                {
                    std::unique_lock<std::mutex> relock(m_context->m_mutex);
                    std::cout << "[DLAS] Re-spawned workers: " << started << std::endl;
                }
            } catch (const std::system_error& e) {
                // Recoverable thread creation failure. Log and retry after short sleep.
                std::cerr << "[DLAS] Re-spawn failed with std::system_error: " << e.what() << ". Retrying in 1s..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            } catch (const std::bad_alloc& e) {
                // Memory pressure during spawn; try to free caches and retry
                std::cerr << "[DLAS] Re-spawn failed with bad_alloc: " << e.what() << ". Clearing caches and retrying..." << std::endl;
                // Best-effort: signal workers (none active) and clear global caches if any
                // Note: per-thread caches are cleared on start; here we can only log.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (const std::exception& e) {
                std::cerr << "[DLAS] Re-spawn failed: " << e.what() << ". Will retry." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (...) {
                std::cerr << "[DLAS] Re-spawn failed with unknown error. Will retry." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            // Continue waiting
            goto wait_again;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error waiting for threads to complete: " << e.what() << std::endl;
        m_running = false;
    }

    // Join all threads
    std::cout << "[CTL] Joining worker threads" << std::endl;
    m_context->JoinWorkerThreads();
    m_running = false;
    std::cout << "[CTL] DLAS control thread exiting" << std::endl;
}

void DLAS::Start()
{
    // Set running flag before starting the thread
    m_running = true;  

    // Start in a dedicated control thread and keep a handle for diagnostics
    try {
        m_control_thread = std::thread(&DLAS::Run, this);
    } catch (const std::exception& e) {
        std::cerr << "[CTL] Failed to start DLAS control thread: " << e.what() << std::endl;
        m_running = false;
    }
}

void DLAS::Stop()
{
    m_running = false;
    if (m_context) {
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        std::cout << "[FIN] DLAS::Stop(): setting finished and notifying" << std::endl;
        CTX_MARK_FINISHED((*m_context), "dlas_stop");
    }
    // Join control thread if running
    if (m_control_thread.joinable()) {
        try { m_control_thread.join(); } catch (...) {}
    }
}

bool DLAS::IsFinished() const
{
    return !m_running || (m_context && m_context->m_finished.load());
}

const raster_picture& DLAS::GetBestSolution() const
{
    if (!m_context) {
        static raster_picture empty_pic;
        std::cerr << "Error: Cannot get best solution with null context" << std::endl;
        return empty_pic;
    }
    return m_context->m_best_pic;
}

void DLAS::SaveState(const std::string& filename) const
{
    if (!m_context) {
        std::cerr << "Error: Cannot save state with null context" << std::endl;
        return;
    }

    std::unique_lock<std::mutex> lock(m_context->m_mutex);

    FILE* f = fopen(filename.c_str(), "wt+");
    if (!f) {
        std::cerr << "Error: Could not open file " << filename << " for writing" << std::endl;
        return;
    }

    fprintf(f, "%lu\n", (unsigned long)m_context->m_previous_results.size());
    fprintf(f, "%lu\n", (unsigned long)m_context->m_previous_results_index);
    fprintf(f, "%Lf\n", (long double)m_context->m_cost_max);
    fprintf(f, "%d\n", m_context->m_N);
    fprintf(f, "%Lf\n", (long double)m_context->m_current_cost);

    for (size_t i = 0; i < m_context->m_previous_results.size(); ++i)
    {
        fprintf(f, "%Lf\n", (long double)m_context->m_previous_results[i]);
    }

    fclose(f);
}

bool DLAS::LoadState(const std::string& filename)
{
    if (!m_context) {
        std::cerr << "Error: Cannot load state with null context" << std::endl;
        return false;
    }

    std::unique_lock<std::mutex> lock(m_context->m_mutex);

    FILE* f = fopen(filename.c_str(), "rt");
    if (!f) {
        std::cerr << "Error: Could not open file " << filename << " for reading" << std::endl;
        return false;
    }

    unsigned long no_elements;
    unsigned long index;
    long double cost_max;
    int N;
    long double current_cost;

    if (fscanf(f, "%lu\n", &no_elements) != 1 ||
        fscanf(f, "%lu\n", &index) != 1 ||
        fscanf(f, "%Lf\n", &cost_max) != 1 ||
        fscanf(f, "%d\n", &N) != 1 ||
        fscanf(f, "%Lf\n", &current_cost) != 1) {
        std::cerr << "Error: Failed to read LAHC state from file " << filename << std::endl;
        fclose(f);
        return false;
    }

    m_context->m_previous_results_index = index;
    m_context->m_cost_max = cost_max;
    m_context->m_N = N;
    m_context->m_current_cost = current_cost;

    m_context->m_previous_results.clear();

    for (size_t i = 0; i < (size_t)no_elements; ++i)
    {
        long double dst = 0;
        if (fscanf(f, "%Lf\n", &dst) != 1) {
            std::cerr << "Error: Failed to read result " << i << " from file " << filename << std::endl;
            fclose(f);
            return false;
        }
        m_context->m_previous_results.push_back(dst);
    }

    // Mark as initialized since we loaded state
    m_context->m_initialized = true;
    m_init_completed = true;

    fclose(f);
    return true;
}
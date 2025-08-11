#include "DLAS.h"
#include "../mutation/RasterMutator.h"
#include <cmath>
#include <fstream>
#include <algorithm>
#include <functional>
#include <chrono>
#include <iostream>
#include <cmath>
// Needed for atari_palette and distances
#include "../TargetPicture.h"
#include "../color/Distance.h"
// Local helpers (keep in sync with EvaluationContext)
static inline void rgb_to_yuv_fast_local(const rgb& c, float& y, float& u, float& v) {
    float r = (float)c.r;
    float g = (float)c.g;
    float b = (float)c.b;
    y = 0.299f*r + 0.587f*g + 0.114f*b;
    float dy = 0.0f; (void)dy;
    u = (b - y) * 0.565f;
    v = (r - y) * 0.713f;
}
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
#ifdef THREAD_DEBUG
        std::cout << "[FIN] DLAS destructor: setting finished and notifying; threads_active="
                  << m_context->m_threads_active.load() << std::endl;
#endif
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
    #ifdef THREAD_DEBUG
    std::cout << "Evaluating initial solution..." << std::endl;
    #endif
    
    if (!m_context || !m_executor) {
        std::cerr << "Error: Cannot evaluate initial solution with null context or executor" << std::endl;
        return DBL_MAX;
    }

    try {
            if (m_context->m_dual_mode) {
                // Initialize B if needed (default dup)
                if (m_context->m_best_pic_B.raster_lines.empty()) {
                    m_context->m_best_pic_B = m_context->m_best_pic;
                }
                raster_picture picA = m_context->m_best_pic;
                raster_picture picB = m_context->m_best_pic_B;
                std::vector<const line_cache_result*> line_results_A(m_context->m_height);
                std::vector<const line_cache_result*> line_results_B(m_context->m_height);
                // Render B plain first to get other-frame pixels, then render A pair-aware against B
                (void)m_executor->ExecuteRasterProgram(&picB, line_results_B.data());
                sprites_memory_t memB;
                memcpy(&memB, &m_executor->GetSpritesMemory(), sizeof(memB));
                (void)m_executor->ExecuteRasterProgram(&picA, line_results_A.data(), Executor::DUAL_A, &line_results_B);
                sprites_memory_t memA;
                memcpy(&memA, &m_executor->GetSpritesMemory(), sizeof(memA));

            // Helpers for color conversions
            auto yuv_to_rgb_approx = [](float Y, float U, float V) -> rgb {
                float R = Y + (V / 0.713f);
                float B = Y + (U / 0.565f);
                float G = (Y - 0.299f * R - 0.114f * B) / 0.587f;
                auto clamp = [](float v) -> unsigned char { if (v < 0) v = 0; if (v > 255) v = 255; return (unsigned char)(v + 0.5f); };
                rgb out; out.r = clamp(R); out.g = clamp(G); out.b = clamp(B); out.a = 0; return out;
            };

            // Pair cost with support for blend_space and blend_distance
            const unsigned W = m_context->m_width;
            const unsigned H = m_context->m_height;
            double total = 0.0;
            // Effective flicker luma weight with optional ramp
            float wl = (float)m_context->m_flicker_luma_weight;
            if (m_context->m_blink_ramp_evals > 0) {
                double t = std::min<double>(1.0, (double)m_context->m_evaluations / (double)m_context->m_blink_ramp_evals);
                wl = (float)((1.0 - t) * m_context->m_flicker_luma_weight_initial + t * m_context->m_flicker_luma_weight);
            }
            const float wc = (float)m_context->m_flicker_chroma_weight;
            const float Tl = (float)m_context->m_flicker_luma_thresh;
            const float Tc = (float)m_context->m_flicker_chroma_thresh;
            const int pl = m_context->m_flicker_exp_luma;
            const int pc = m_context->m_flicker_exp_chroma;
            const float* ty = m_context->m_target_y.data();
            const float* tu = m_context->m_target_u.data();
            const float* tv = m_context->m_target_v.data();
            m_context->m_flicker_heatmap.assign(W*H, 0);
            for (unsigned y = 0; y < H; ++y) {
                const line_cache_result* lA = line_results_A[y];
                const line_cache_result* lB = line_results_B[y];
                if (!lA || !lB) continue;
                const unsigned char* rowA = lA->color_row;
                const unsigned char* rowB = lB->color_row;
                for (unsigned x = 0; x < W; ++x) {
                    const unsigned idx = y * W + x;
                    const unsigned char a = rowA[x];
                    const unsigned char b = rowB[x];
                    // Luma/chroma deltas for flicker (YUV always)
                    const float Ya = m_context->m_palette_y[a];
                    const float Ua = m_context->m_palette_u[a];
                    const float Va = m_context->m_palette_v[a];
                    const float Yb = m_context->m_palette_y[b];
                    const float Ub = m_context->m_palette_u[b];
                    const float Vb = m_context->m_palette_v[b];
                    float dY = fabsf(Ya - Yb);
                    float dC = sqrtf((Ua - Ub)*(Ua - Ub) + (Va - Vb)*(Va - Vb));
                    // YUV fast-path: blend in YUV and compute squared distance to target precomputed YUV
                        float Ybl = 0.5f*(Ya+Yb), Ubl = 0.5f*(Ua+Ub), Vbl = 0.5f*(Va+Vb);
                        float dy = Ybl - ty[idx]; float du = Ubl - tu[idx]; float dv = Vbl - tv[idx];
                    double base = (double)(dy*dy + du*du + dv*dv);
                    float yl = dY - Tl; if (yl < 0) yl = 0;
                    float yc = dC - Tc; if (yc < 0) yc = 0;
                    double flick = 0.0;
                    if (wl > 0) { double t = yl; if (pl == 2) t = t*t; else if (pl == 3) t = t*t*t; else t = pow(t, (double)pl); flick += wl * t; }
                    if (wc > 0) { double t = yc; if (pc == 2) t = t*t; else if (pc == 3) t = t*t*t; else t = pow(t, (double)pc); flick += wc * t; }
                    total += base + flick;
                    // Populate heatmap with scaled luma delta (0..255 after simple clamp)
                    float scaled = dY * (1.0f/2.0f); if (scaled > 255.0f) scaled = 255.0f; if (scaled < 0.0f) scaled = 0.0f;
                    m_context->m_flicker_heatmap[idx] = (unsigned char)(scaled + 0.5f);
                }
            }
            #ifdef THREAD_DEBUG
            std::cout << "Initial dual pair evaluation: " << total << std::endl;
            #endif
            {
                std::unique_lock<std::mutex> lock(m_context->m_mutex);
                m_context->m_best_result = total;
                m_context->m_best_pic = picA;
                m_context->m_best_pic_B = picB;
                m_context->m_created_picture.resize(m_context->m_height);
                m_context->m_created_picture_targets.resize(m_context->m_height);
                m_context->m_created_picture_B.resize(m_context->m_height);
                m_context->m_created_picture_targets_B.resize(m_context->m_height);
                const line_cache_result* lastA = nullptr;
                const line_cache_result* lastB = nullptr;
                for (int y = 0; y < (int)m_context->m_height; ++y) {
                    const line_cache_result* lA = line_results_A[y];
                    if (lA) {
                        lastA = lA;
                        m_context->m_created_picture[y].assign(lA->color_row, lA->color_row + m_context->m_width);
                        m_context->m_created_picture_targets[y].assign(lA->target_row, lA->target_row + m_context->m_width);
                    } else if (lastA) {
                        m_context->m_created_picture[y].assign(lastA->color_row, lastA->color_row + m_context->m_width);
                        m_context->m_created_picture_targets[y].assign(lastA->target_row, lastA->target_row + m_context->m_width);
                    } else {
                        m_context->m_created_picture[y].assign(m_context->m_width, 0);
                        m_context->m_created_picture_targets[y].assign(m_context->m_width, E_COLBAK);
                    }

                    const line_cache_result* lB = line_results_B[y];
                    if (lB) {
                        lastB = lB;
                        m_context->m_created_picture_B[y].assign(lB->color_row, lB->color_row + m_context->m_width);
                        m_context->m_created_picture_targets_B[y].assign(lB->target_row, lB->target_row + m_context->m_width);
                    } else if (lastB) {
                        m_context->m_created_picture_B[y].assign(lastB->color_row, lastB->color_row + m_context->m_width);
                        m_context->m_created_picture_targets_B[y].assign(lastB->target_row, lastB->target_row + m_context->m_width);
                    } else {
                        m_context->m_created_picture_B[y].assign(m_context->m_width, 0);
                        m_context->m_created_picture_targets_B[y].assign(m_context->m_width, E_COLBAK);
                    }
                }
                memcpy(&m_context->m_sprites_memory, &memA, sizeof(m_context->m_sprites_memory));
                memcpy(&m_context->m_sprites_memory_B, &memB, sizeof(m_context->m_sprites_memory_B));
                m_context->ReportInitialScore(total);
                m_context->m_initialized = true;
                m_context->m_update_initialized = true;
                m_context->m_condvar_update.notify_all();
            }
            return total;
        } else {
            // Single-frame path
        // Create a copy of the initial solution
        raster_picture pic = m_context->m_best_pic;
        std::vector<const line_cache_result*> line_results(m_context->m_height);
        double result = m_executor->ExecuteRasterProgram(&pic, line_results.data());
        #ifdef THREAD_DEBUG
        std::cout << "Initial solution evaluation: " << result << std::endl;
        #endif
        {
            std::unique_lock<std::mutex> lock(m_context->m_mutex);
            m_context->m_best_result = result;
            m_context->m_created_picture.resize(m_context->m_height);
            m_context->m_created_picture_targets.resize(m_context->m_height);
            const line_cache_result* last_valid_result = nullptr;
            for (int y = 0; y < (int)m_context->m_height; ++y) {
                if (line_results[y] != nullptr) {
                    last_valid_result = line_results[y];
                    const line_cache_result& lcr = *line_results[y];
                    m_context->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_context->m_width);
                    m_context->m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_context->m_width);
                }
                else {
                    if (last_valid_result != nullptr) {
                        m_context->m_created_picture[y].assign(last_valid_result->color_row,
                            last_valid_result->color_row + m_context->m_width);
                        m_context->m_created_picture_targets[y].assign(last_valid_result->target_row,
                            last_valid_result->target_row + m_context->m_width);
                    }
                    else {
                        m_context->m_created_picture[y].assign(m_context->m_width, 0);
                        m_context->m_created_picture_targets[y].assign(m_context->m_width, E_COLBAK);
                    }
                }
            }
            memcpy(&m_context->m_sprites_memory, &m_executor->GetSpritesMemory(), sizeof(m_context->m_sprites_memory));
            m_context->ReportInitialScore(result);
            m_context->m_initialized = true;
            m_context->m_update_initialized = true;
            m_context->m_condvar_update.notify_all();
        }
        return result;
        }
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
        #ifdef THREAD_DEBUG
        std::cout << "[DLAS] Worker " << threadId << " exiting - not running" << std::endl;
        #endif
        signal_thread_exit();
        return;
    }

    if (!m_context) {
        #ifdef THREAD_DEBUG
        std::cout << "[DLAS] Worker " << threadId << " exiting - null context" << std::endl;
        #endif
        signal_thread_exit();
        return;
    }

    try {
        // If this is thread 0, evaluate the initial solution first
        if (threadId == 0 && !m_context->m_initialized) {
            #ifdef THREAD_DEBUG
            std::cout << "[DLAS] Thread 0 evaluating initial solution" << std::endl;
            #endif
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
            std::cout << "[DLAS] Thread 0 completed initialization" << std::endl;
            #endif
        }
        else if (!m_context->m_initialized) {
            #ifdef THREAD_DEBUG
            std::cout << "[DLAS] Thread " << threadId << " waiting for initialization" << std::endl;
            #endif
            
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
                    std::cout << "[DLAS] Thread " << threadId << " still waiting for initialization with context lock" << std::endl;
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

        // Copy best picture(s) to work with
        raster_picture pic = m_context->m_best_pic;
        raster_picture picB_local;
                    if (m_context->m_dual_mode) {
            picB_local = m_context->m_best_pic_B.raster_lines.empty() ? m_context->m_best_pic : m_context->m_best_pic_B;
        }
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
            std::vector<const line_cache_result*> line_results_B(m_context->m_height);
            sprites_memory_t memA, memB;
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
                #ifdef THREAD_DEBUG
                if (++iteration_count % 20000 == 0) {
                    std::cout << "[DLAS] Thread " << threadId << " iterations=" << iteration_count
                              << " evals=" << m_context->m_evaluations
                              << " finished=" << m_context->m_finished.load()
                              << " active=" << m_context->m_threads_active.load() << std::endl;
                }
                #else
                ++iteration_count;
                #endif
                
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
                raster_picture new_picB = picB_local;
                bool mutate_B = false;
                if (m_context->m_dual_mode) {
                    int r = local_executor.Random(1000);
                    mutate_B = (r < (int)(m_context->m_dual_mutate_ratio * 1000.0));
                }
                try {
                    if (m_context->m_dual_mode && mutate_B) {
                        m_thread_mutators[threadId]->SetDualFrameRole(true); // frame B
                        m_thread_mutators[threadId]->MutateProgram(&new_picB);
                    } else {
                        m_thread_mutators[threadId]->SetDualFrameRole(false); // frame A
                        m_thread_mutators[threadId]->MutateProgram(&new_pic);
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Error in mutation in thread " << threadId << ": " << e.what() << std::endl;
                    continue;
                }

                // Low-probability cross-frame structural ops (copy/swap current line)
                bool didCrossShare = false;
                if (m_context->m_dual_mode && m_context->m_dual_cross_share_prob > 0.0) {
                    int rshare = local_executor.Random(1000000);
                    if ((double)rshare / 1000000.0 < m_context->m_dual_cross_share_prob) {
                        int y = m_thread_mutators[threadId]->GetCurrentlyMutatedLine();
                        if (y >= 0 && y < (int)new_pic.raster_lines.size() && y < (int)new_picB.raster_lines.size()) {
                            bool doSwap = (local_executor.Random(2) == 0);
                            if (doSwap) {
                                new_pic.raster_lines[y].swap(new_picB.raster_lines[y]);
                                new_pic.raster_lines[y].cache_key = NULL;
                                new_picB.raster_lines[y].cache_key = NULL;
                                m_context->m_stat_crossSwapLine++;
                                didCrossShare = true;
                            } else {
                                new_picB.raster_lines[y] = new_pic.raster_lines[y];
                                new_picB.raster_lines[y].cache_key = NULL;
                                m_context->m_stat_crossCopyLine++;
                                didCrossShare = true;
                            }
                        }
                    }
                }

                // If any structural cross-frame ops occurred, ensure recache before evaluation
                if (m_context->m_dual_mode && didCrossShare) {
                    // Re-cache only lines marked invalid
                    new_pic.recache_insns_if_needed(thread_cache, thread_allocator);
                    new_picB.recache_insns_if_needed(thread_cache, thread_allocator);
                }

                try {
                    // Evaluate the new candidate
                    double result = 0.0;
                    if (m_context->m_dual_mode) {
                        if (mutate_B) {
                            // Fixed A first (plain), then B pair-aware against A
                            (void)local_executor.ExecuteRasterProgram(&new_pic, line_results.data());
                            memcpy(&memA, &local_executor.GetSpritesMemory(), sizeof(memA));
                            (void)local_executor.ExecuteRasterProgram(&new_picB, line_results_B.data(), Executor::DUAL_B, &line_results);
                            memcpy(&memB, &local_executor.GetSpritesMemory(), sizeof(memB));
                        } else {
                            // Fixed B first (plain), then A pair-aware against B
                            (void)local_executor.ExecuteRasterProgram(&new_picB, line_results_B.data());
                            memcpy(&memB, &local_executor.GetSpritesMemory(), sizeof(memB));
                            (void)local_executor.ExecuteRasterProgram(&new_pic, line_results.data(), Executor::DUAL_A, &line_results_B);
                            memcpy(&memA, &local_executor.GetSpritesMemory(), sizeof(memA));
                        }
                        // After an accepted improvement, generation counters are bumped in ReportEvaluationResultDual
                        const unsigned W = m_context->m_width;
                        const unsigned H = m_context->m_height;
                        double total = 0.0;
                        float wl = (float)m_context->m_flicker_luma_weight;
                        if (m_context->m_blink_ramp_evals > 0) {
                            double t = std::min<double>(1.0, (double)m_context->m_evaluations / (double)m_context->m_blink_ramp_evals);
                            wl = (float)((1.0 - t) * m_context->m_flicker_luma_weight_initial + t * m_context->m_flicker_luma_weight);
                        } else {
                            // Default: slightly relaxed luma penalty early to promote exploration in dual mode
                            if (m_context->m_evaluations < 50000ULL) wl = (float)(0.7 * wl);
                        }
                        const float wc = (float)m_context->m_flicker_chroma_weight;
                        const float Tl = (float)m_context->m_flicker_luma_thresh;
                        const float Tc = (float)m_context->m_flicker_chroma_thresh;
                        const int pl = m_context->m_flicker_exp_luma;
                        const int pc = m_context->m_flicker_exp_chroma;
                        const float* ty = m_context->m_target_y.data();
                        const float* tu = m_context->m_target_u.data();
                        const float* tv = m_context->m_target_v.data();
                        m_context->m_flicker_heatmap.assign(W*H, 0);
                        for (unsigned y = 0; y < H; ++y) {
                            const line_cache_result* lA = line_results[y];
                            const line_cache_result* lB = line_results_B[y];
                            if (!lA || !lB) continue;
                            const unsigned char* rowA = lA->color_row;
                            const unsigned char* rowB = lB->color_row;
                            for (unsigned x = 0; x < W; ++x) {
                                const unsigned idx = y * W + x;
                                const unsigned char a = rowA[x];
                                const unsigned char b = rowB[x];
                                const float Ya = m_context->m_palette_y[a];
                                const float Ua = m_context->m_palette_u[a];
                                const float Va = m_context->m_palette_v[a];
                                const float Yb = m_context->m_palette_y[b];
                                const float Ub = m_context->m_palette_u[b];
                                const float Vb = m_context->m_palette_v[b];
                                const float Ybl = 0.5f * (Ya + Yb);
                                const float Ubl = 0.5f * (Ua + Ub);
                                const float Vbl = 0.5f * (Va + Vb);
                                const float dy = Ybl - ty[idx];
                                const float du = Ubl - tu[idx];
                                const float dv = Vbl - tv[idx];
                                double base = (double)(dy*dy + du*du + dv*dv);
                                float dY = fabsf(Ya - Yb);
                                float dC = sqrtf((Ua - Ub)*(Ua - Ub) + (Va - Vb)*(Va - Vb));
                                float yl = dY - Tl; if (yl < 0) yl = 0;
                                float yc = dC - Tc; if (yc < 0) yc = 0;
                                double flick = 0.0;
                                if (wl > 0) { double t = yl; if (pl == 2) t = t*t; else if (pl == 3) t = t*t*t; else t = pow(t, (double)pl); flick += wl * t; }
                                if (wc > 0) { double t = yc; if (pc == 2) t = t*t; else if (pc == 3) t = t*t*t; else t = pow(t, (double)pc); flick += wc * t; }
                                total += base + flick;
                                float scaled = dY * (1.0f/2.0f); if (scaled > 255.0f) scaled = 255.0f; if (scaled < 0.0f) scaled = 0.0f;
                                m_context->m_flicker_heatmap[idx] = (unsigned char)(scaled + 0.5f);
                            }
                        }
                        result = total;
                    } else {
                        result = (double)local_executor.ExecuteRasterProgram(&new_pic, line_results.data());
                    }

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
                    bool accepted = false;
                    if (m_context->m_dual_mode) {
                        accepted = m_context->ReportEvaluationResultDual(
                            result,
                            &new_pic,
                            &new_picB,
                            line_results,
                            line_results_B,
                            memA,
                            memB,
                            m_thread_mutators[threadId].get());
                    } else {
                        accepted = m_context->ReportEvaluationResult(
                        result,
                        &new_pic,
                        line_results,
                        local_executor.GetSpritesMemory(),
                        m_thread_mutators[threadId].get()
                    );
                    }

                    // If accepted, update our local copy
                    if (accepted) {
                        if (m_context->m_dual_mode) {
                            if (mutate_B) picB_local = new_picB; else pic = new_pic;
                        } else {
                            pic = new_pic;
                        }
                    }

                    // Update local best if global best improved
                    if (m_context->m_best_result < best_result)
                    {
                        pic = m_context->m_best_pic;
                        if (m_context->m_dual_mode) { picB_local = m_context->m_best_pic_B.raster_lines.empty() ? m_context->m_best_pic : m_context->m_best_pic_B; }
                        best_result = m_context->m_best_result;
                        // Re-cache using this worker's thread-local caches, not the DLAS-wide members
                        pic.recache_insns(thread_cache, thread_allocator);
                        if (m_context->m_dual_mode) { picB_local.recache_insns(thread_cache, thread_allocator); }
                    }

                    // Update statistics (under lock)
                    m_context->CollectStatisticsTickUnsafe();

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
    #ifdef THREAD_DEBUG
    std::cout << "Starting DLAS optimization" << std::endl;
    #endif
    
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
        #ifdef THREAD_DEBUG
        std::cout << "[CTL] Control thread entering wait loop" << std::endl;
        #endif
        
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

        #ifdef THREAD_DEBUG
        std::cout << "[CTL] Wait finished: running=" << m_running
                  << ", finished=" << m_context->m_finished.load()
                  << ", active_threads=" << m_context->m_threads_active.load() << std::endl;
        #endif

        // If threads all finished unexpectedly and we have no max evals set, re-spawn workers
        if (!m_context->m_finished.load() && m_context->m_threads_active.load() <= 0 && m_running) {
            #ifdef THREAD_DEBUG
            std::cout << "[DLAS] All workers exited but not finished; attempting to re-spawn workers" << std::endl;
            #endif
            // Recreate workers
            lock.unlock();
            try {
                int started = m_context->StartWorkerThreads(std::bind(&DLAS::RunWorker, this, std::placeholders::_1));
                #ifdef THREAD_DEBUG
                std::unique_lock<std::mutex> relock(m_context->m_mutex);
                std::cout << "[DLAS] Re-spawned workers: " << started << std::endl;
                #endif
            } catch (const std::system_error& e) {
                // Recoverable thread creation failure. Log and retry after short sleep.
                #ifdef THREAD_DEBUG
                std::cerr << "[DLAS] Re-spawn failed with std::system_error: " << e.what() << ". Retrying in 1s..." << std::endl;
                #endif
                std::this_thread::sleep_for(std::chrono::seconds(1));
            } catch (const std::bad_alloc& e) {
                // Memory pressure during spawn; try to free caches and retry
                #ifdef THREAD_DEBUG
                std::cerr << "[DLAS] Re-spawn failed with bad_alloc: " << e.what() << ". Clearing caches and retrying..." << std::endl;
                #endif
                // Best-effort: signal workers (none active) and clear global caches if any
                // Note: per-thread caches are cleared on start; here we can only log.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (const std::exception& e) {
                #ifdef THREAD_DEBUG
                std::cerr << "[DLAS] Re-spawn failed: " << e.what() << ". Will retry." << std::endl;
                #endif
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } catch (...) {
                #ifdef THREAD_DEBUG
                std::cerr << "[DLAS] Re-spawn failed with unknown error. Will retry." << std::endl;
                #endif
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
    #ifdef THREAD_DEBUG
    std::cout << "[CTL] Joining worker threads" << std::endl;
    #endif
    m_context->JoinWorkerThreads();
    m_running = false;
    #ifdef THREAD_DEBUG
    std::cout << "[CTL] DLAS control thread exiting" << std::endl;
    #endif
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
        // Avoid deadlock if UI holds the mutex; just mark finished and notify.
        #ifdef THREAD_DEBUG
        std::cout << "[FIN] DLAS::Stop(): setting finished and notifying" << std::endl;
        #endif
        CTX_MARK_FINISHED((*m_context), "dlas_stop");
        // Best-effort wakeup for any waiters
        m_context->m_condvar_update.notify_all();
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
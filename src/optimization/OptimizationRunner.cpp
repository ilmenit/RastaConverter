#include "OptimizationRunner.h"
#include <chrono>
#include <cfloat>
#include <iostream>

OptimizationRunner::OptimizationRunner(EvaluationContext* ctx,
                                       std::unique_ptr<AcceptancePolicy> policy,
                                       Mutator* referenceMutator)
    : m_ctx(ctx)
    , m_policy(std::move(policy))
    , m_reference_mutator(referenceMutator)
    , m_evaluator(ctx)
{
}

void OptimizationRunner::run()
{
    if (!m_ctx) return;
    m_running.store(true);

    // Reset finish flag; init best picture is assumed set by caller
    {
        std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
        m_ctx->m_finished.store(false);
    }

    // Evaluate initial solution(s)
    {
        // Use a temporary single-thread executor for initialization
        Executor initExec;
        initExec.Init(
            m_ctx->m_width,
            m_ctx->m_height,
            m_ctx->m_picture_all_errors,
            m_ctx->m_picture,
            m_ctx->m_onoff,
            m_ctx,
            std::max(1, m_ctx->m_history_length_config),
            (unsigned long long)time(NULL) + 911ULL,
            m_ctx->m_cache_size,
            m_reference_mutator,
            -1);
        initExec.Start();

        if (m_ctx->m_dual_mode) {
            // Ensure B initialized
            if (m_ctx->m_best_pic_B.raster_lines.empty()) m_ctx->m_best_pic_B = m_ctx->m_best_pic;
            raster_picture picA = m_ctx->m_best_pic;
            raster_picture picB = m_ctx->m_best_pic_B;
            auto dual = m_evaluator.evaluateDual(initExec, picA, picB, /*mutateB=*/false);
            {
                std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
                m_ctx->m_best_result = dual.cost;
                m_ctx->m_best_pic = picA;
                m_ctx->m_best_pic_B = picB;
                // Increment generation for lock-free best picture access
                m_ctx->m_best_generation.fetch_add(1, std::memory_order_release);
                m_ctx->m_created_picture.resize(m_ctx->m_height);
                m_ctx->m_created_picture_targets.resize(m_ctx->m_height);
                m_ctx->m_created_picture_B.resize(m_ctx->m_height);
                m_ctx->m_created_picture_targets_B.resize(m_ctx->m_height);
                for (int y = 0; y < (int)m_ctx->m_height; ++y) {
                    const line_cache_result* lA = dual.lineResultsA[y];
                    const line_cache_result* lB = dual.lineResultsB[y];
                    if (lA) {
                        m_ctx->m_created_picture[y].assign(lA->color_row, lA->color_row + m_ctx->m_width);
                        m_ctx->m_created_picture_targets[y].assign(lA->target_row, lA->target_row + m_ctx->m_width);
                        // also seed fresh snapshots on init
                        if ((int)m_ctx->m_snapshot_picture_A.size() <= y) m_ctx->m_snapshot_picture_A.resize(m_ctx->m_height);
                        m_ctx->m_snapshot_picture_A[y] = m_ctx->m_created_picture[y];
                    } else {
                        if (m_ctx->m_created_picture[y].empty()) {
                            m_ctx->m_created_picture[y].assign(m_ctx->m_width, 0);
                            m_ctx->m_created_picture_targets[y].assign(m_ctx->m_width, E_COLBAK);
                        }
                    }
                    if (lB) {
                        m_ctx->m_created_picture_B[y].assign(lB->color_row, lB->color_row + m_ctx->m_width);
                        m_ctx->m_created_picture_targets_B[y].assign(lB->target_row, lB->target_row + m_ctx->m_width);
                        if ((int)m_ctx->m_snapshot_picture_B.size() <= y) m_ctx->m_snapshot_picture_B.resize(m_ctx->m_height);
                        m_ctx->m_snapshot_picture_B[y] = m_ctx->m_created_picture_B[y];
                    } else {
                        if (m_ctx->m_created_picture_B[y].empty()) {
                            m_ctx->m_created_picture_B[y].assign(m_ctx->m_width, 0);
                            m_ctx->m_created_picture_targets_B[y].assign(m_ctx->m_width, E_COLBAK);
                        }
                    }
                }
                std::memcpy(&m_ctx->m_sprites_memory, &dual.spritesMemoryA, sizeof(m_ctx->m_sprites_memory));
                std::memcpy(&m_ctx->m_sprites_memory_B, &dual.spritesMemoryB, sizeof(m_ctx->m_sprites_memory_B));
                // Initialize policy from context and initial score
                m_policy->init(*m_ctx);
                m_policy->onInitialScore(dual.cost, *m_ctx);
                m_ctx->ReportInitialScore(dual.cost);
                m_ctx->m_initialized = true;
                m_ctx->m_update_initialized = true;
                m_ctx->m_condvar_update.notify_all();
            }
        } else {
            raster_picture pic = m_ctx->m_best_pic;
            auto single = m_evaluator.evaluateSingle(initExec, pic);
            {
                std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
                m_ctx->m_best_result = single.cost;
                m_ctx->m_created_picture.resize(m_ctx->m_height);
                m_ctx->m_created_picture_targets.resize(m_ctx->m_height);
                const line_cache_result* last = nullptr;
                for (int y = 0; y < (int)m_ctx->m_height; ++y) {
                    const line_cache_result* l = single.lineResults[y];
                    if (l) { last = l; }
                    if (l) {
                        m_ctx->m_created_picture[y].assign(l->color_row, l->color_row + m_ctx->m_width);
                        m_ctx->m_created_picture_targets[y].assign(l->target_row, l->target_row + m_ctx->m_width);
                    } else if (last) {
                        m_ctx->m_created_picture[y].assign(last->color_row, last->color_row + m_ctx->m_width);
                        m_ctx->m_created_picture_targets[y].assign(last->target_row, last->target_row + m_ctx->m_width);
                    } else {
                        m_ctx->m_created_picture[y].assign(m_ctx->m_width, 0);
                        m_ctx->m_created_picture_targets[y].assign(m_ctx->m_width, E_COLBAK);
                    }
                }
                // Copy sprites memory only here on initialization (improvement-equivalent)
                std::memcpy(&m_ctx->m_sprites_memory, &initExec.GetSpritesMemory(), sizeof(m_ctx->m_sprites_memory));
                m_policy->init(*m_ctx);
                m_policy->onInitialScore(single.cost, *m_ctx);
                m_ctx->ReportInitialScore(single.cost);
                m_ctx->m_initialized = true;
                m_ctx->m_update_initialized = true;
                m_ctx->m_condvar_update.notify_all();
            }
        }
    }

    // Start workers
    m_ctx->StartWorkerThreads([this](int tid) { this->worker(tid); });

    // Wait for completion
    {
        std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
        while (m_running.load() && !m_ctx->m_finished.load() && m_ctx->m_threads_active.load() > 0) {
            m_ctx->m_condvar_update.wait_for(lock, std::chrono::seconds(5));
        }
    }
    m_ctx->JoinWorkerThreads();
    m_running.store(false);
}

void OptimizationRunner::worker(int threadId)
{
    if (!m_ctx || !m_running.load()) return;

    // Use optimized single-frame or dual-frame worker based on mode
    if (m_ctx->m_dual_mode) {
        workerDual(threadId);
    } else {
        workerSingle(threadId);
    }
}

void OptimizationRunner::workerSingle(int threadId)
{
    RasterMutator mutator(m_ctx, threadId);
    mutator.Init((unsigned long long)time(NULL) + (unsigned long long)threadId * 187927ULL);

    Executor exec;
    exec.Init(
        m_ctx->m_width,
        m_ctx->m_height,
        m_ctx->m_picture_all_errors,
        m_ctx->m_picture,
        m_ctx->m_onoff,
        m_ctx,
        std::max(1, m_ctx->m_history_length_config),
        (unsigned long long)time(NULL) + (unsigned long long)threadId * 911ULL,
        m_ctx->m_cache_size,
        &mutator,
        threadId);
    exec.Start();

    // Single-frame optimized - no dual structures
    raster_picture current = m_ctx->m_best_pic;
    double currentCost = m_ctx->m_best_result;
    
    // Lock-free best picture tracking to avoid hot-path contention
    raster_picture local_best_pic = m_ctx->m_best_pic;
    unsigned long long local_best_generation = m_ctx->m_best_generation.load(std::memory_order_acquire);

    // Reuse line results buffer to avoid per-iteration allocations
    std::vector<const line_cache_result*> reusableLineResults(m_ctx->m_height);
    SingleEvalResult reusableResult;
    reusableResult.lineResults.resize(m_ctx->m_height);
    
    // Statistics throttling - only collect every N iterations to avoid per-iteration time() calls
    constexpr int STATS_THROTTLE_INTERVAL = 10000;
    int stats_counter = 0;

    while (m_running.load() && !m_ctx->m_finished.load()) {
        // Simple mutation - no dual-frame logic
        raster_picture candidate = current;
        
        // Honor mutation base selection with lock-free best picture access
        if (m_ctx->m_mutation_base == E_MUT_BASE_BEST) {
            // Check if best picture has been updated (lock-free)
            unsigned long long current_generation = m_ctx->m_best_generation.load(std::memory_order_acquire);
            if (current_generation != local_best_generation) {
                // Best picture changed, update our local copy
                std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
                // Re-read generation inside lock to avoid race condition
                current_generation = m_ctx->m_best_generation.load(std::memory_order_relaxed);
                local_best_pic = m_ctx->m_best_pic;
                local_best_generation = current_generation;
            }
            candidate = local_best_pic;
        }
        
        mutator.MutateProgram(&candidate);

        // Simple evaluation using reusable buffer - no allocations
        m_evaluator.evaluateSingle(exec, candidate, reusableResult);
        double candCost = reusableResult.cost;

        // Single tight critical section - optimized to reduce mutex contention
        {
            std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
            
            ++m_ctx->m_evaluations;
            
            // Throttled statistics collection - avoid per-iteration time() calls
            if (++stats_counter >= STATS_THROTTLE_INTERVAL) {
                stats_counter = 0;
                m_ctx->CollectStatisticsTickUnsafe();
            }
            
            if ((m_ctx->m_max_evals > 0 && m_ctx->m_evaluations >= m_ctx->m_max_evals) || m_ctx->m_finished.load()) {
                CTX_MARK_FINISHED((*m_ctx), "runner_max_evals");
                break;
            }
            
            bool accept = m_policy->accept(currentCost, candCost, *m_ctx);
            if (accept) {
                currentCost = candCost;
                current = candidate;
                m_policy->onAccepted(candCost, *m_ctx);
            }
            
            if (candCost < m_ctx->m_best_result) {
                // Copy sprites memory only when improving; pass executor's current view to avoid extra memcpy per eval
                m_ctx->ReportEvaluationResult(candCost, &candidate, reusableResult.lineResults, exec.GetSpritesMemory(), &mutator);
            }
            
            m_policy->postIterationUpdate(*m_ctx);
        }
    }
}

void OptimizationRunner::workerDual(int threadId)
{
    RasterMutator mutator(m_ctx, threadId);
    mutator.Init((unsigned long long)time(NULL) + (unsigned long long)threadId * 187927ULL);

    Executor exec;
    exec.Init(
        m_ctx->m_width,
        m_ctx->m_height,
        m_ctx->m_picture_all_errors,
        m_ctx->m_picture,
        m_ctx->m_onoff,
        m_ctx,
        std::max(1, m_ctx->m_history_length_config),
        (unsigned long long)time(NULL) + (unsigned long long)threadId * 911ULL,
        m_ctx->m_cache_size,
        &mutator,
        threadId);
    exec.Start();

    // Dual-frame structures
    raster_picture currentA = m_ctx->m_best_pic;
    raster_picture currentB = m_ctx->m_best_pic_B.raster_lines.empty() ? m_ctx->m_best_pic : m_ctx->m_best_pic_B;
    double currentCost = m_ctx->m_best_result;

    // Reusable dual buffers to avoid allocations
    DualEvalResult reusableDualResult;
    reusableDualResult.lineResultsA.resize(m_ctx->m_height);
    reusableDualResult.lineResultsB.resize(m_ctx->m_height);

    // Initialize global staged state once per worker start
    if (m_ctx->m_dual_strategy == E_DUAL_STRAT_STAGED && threadId == 0) {
        m_ctx->m_dual_stage_focus_B.store(m_ctx->m_dual_stage_start_B, std::memory_order_relaxed);
        m_ctx->m_dual_stage_counter.store(0ULL, std::memory_order_relaxed);
    }

    // Statistics throttling for dual mode as well
    constexpr int STATS_THROTTLE_INTERVAL = 10000;
    int stats_counter = 0;

    while (m_running.load() && !m_ctx->m_finished.load()) {
        // Mutation choice
        raster_picture candA = currentA;
        raster_picture candB = currentB;
        bool mutateB = false;
        
        // Dual-frame mutation strategy
        if (m_ctx->m_dual_strategy == E_DUAL_STRAT_STAGED) {
            // Global staged focus to keep all threads coordinated
            bool focusB = m_ctx->m_dual_stage_focus_B.load(std::memory_order_relaxed);
            mutateB = focusB;
            unsigned long long cnt = m_ctx->m_dual_stage_counter.fetch_add(1ULL, std::memory_order_relaxed) + 1ULL;
            const unsigned long long stage_len = std::max(1ULL, m_ctx->m_dual_stage_evals);
            if (cnt >= stage_len) {
                m_ctx->m_dual_stage_counter.store(0ULL, std::memory_order_relaxed);
                bool nextFocusB = !focusB;
                m_ctx->m_dual_stage_focus_B.store(nextFocusB, std::memory_order_relaxed);
                // Notify acceptance policy of stage switch to relax history
                {
                    std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
                    m_policy->onStageSwitch(currentCost, *m_ctx, nextFocusB);
                }
            }
        } else {
            int r = exec.Random(1000);
            mutateB = (r < (int)(m_ctx->m_dual_mutate_ratio * 1000.0));
        }
        
        // Perform mutation
        if (mutateB) {
            mutator.SetDualFrameRole(true);
            if (m_ctx->m_mutation_base == E_MUT_BASE_BEST) {
                std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
                candB = m_ctx->m_best_pic_B.raster_lines.empty() ? m_ctx->m_best_pic : m_ctx->m_best_pic_B;
            }
            mutator.MutateProgram(&candB);
        } else {
            mutator.SetDualFrameRole(false);
            if (m_ctx->m_mutation_base == E_MUT_BASE_BEST) {
                std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
                candA = m_ctx->m_best_pic;
            }
            mutator.MutateProgram(&candA);
        }

        // Optional cross-share ops
        bool didCrossShare = false;
        bool didCrossSwap = false;
        if (m_ctx->m_dual_cross_share_prob > 0.0) {
            int rshare = exec.Random(1000000);
            if ((double)rshare / 1000000.0 < m_ctx->m_dual_cross_share_prob) {
                int y = mutator.GetCurrentlyMutatedLine();
                if (y >= 0 && y < (int)candA.raster_lines.size() && y < (int)candB.raster_lines.size()) {
                    bool doSwap = (exec.Random(2) == 0);
                    if (doSwap) {
                        candA.raster_lines[y].swap(candB.raster_lines[y]);
                        candA.raster_lines[y].cache_key = NULL;
                        candB.raster_lines[y].cache_key = NULL;
                        didCrossSwap = true;
                    } else {
                        candB.raster_lines[y] = candA.raster_lines[y];
                        candB.raster_lines[y].cache_key = NULL;
                        didCrossSwap = false;
                    }
                    didCrossShare = true;
                }
            }
        }

        // Evaluate dual-frame - currently uses allocating version
        // TODO: Add non-allocating version for dual evaluation too
        auto dual = m_evaluator.evaluateDual(exec, candA, candB, mutateB);
        double candCost = dual.cost;

        // Single tight critical section - matches legacy pattern
        {
            std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
            
            ++m_ctx->m_evaluations;
            // Throttled statistics collection - avoid per-iteration time() calls
            if (++stats_counter >= STATS_THROTTLE_INTERVAL) {
                stats_counter = 0;
                m_ctx->CollectStatisticsTickUnsafe();
            }
            
            if ((m_ctx->m_max_evals > 0 && m_ctx->m_evaluations >= m_ctx->m_max_evals) || m_ctx->m_finished.load()) {
                CTX_MARK_FINISHED((*m_ctx), "runner_max_evals");
                break;
            }
            
            bool accept = m_policy->accept(currentCost, candCost, *m_ctx);
            if (accept) {
                currentCost = candCost;
                currentA = candA;
                currentB = candB;
                m_policy->onAccepted(candCost, *m_ctx);
            }
            
            if (candCost < m_ctx->m_best_result) {
                m_ctx->ReportEvaluationResultDual(
                    candCost, &candA, &candB,
                    dual.lineResultsA, dual.lineResultsB,
                    dual.spritesMemoryA, dual.spritesMemoryB,
                    &mutator, mutateB, didCrossShare, didCrossSwap);
            }
            
            m_policy->postIterationUpdate(*m_ctx);
        }
    }
}



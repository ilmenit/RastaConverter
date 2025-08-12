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
                    } else {
                        if (m_ctx->m_created_picture[y].empty()) {
                            m_ctx->m_created_picture[y].assign(m_ctx->m_width, 0);
                            m_ctx->m_created_picture_targets[y].assign(m_ctx->m_width, E_COLBAK);
                        }
                    }
                    if (lB) {
                        m_ctx->m_created_picture_B[y].assign(lB->color_row, lB->color_row + m_ctx->m_width);
                        m_ctx->m_created_picture_targets_B[y].assign(lB->target_row, lB->target_row + m_ctx->m_width);
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
                std::memcpy(&m_ctx->m_sprites_memory, &single.spritesMemory, sizeof(m_ctx->m_sprites_memory));
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

    RasterMutator mutator(m_ctx, threadId);
    mutator.Init((unsigned long long)time(NULL) + (unsigned long long)threadId * 187927ULL);

    Executor exec;
    try {
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
    } catch (...) {
        std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
        m_ctx->m_threads_active.fetch_sub(1);
        m_ctx->m_condvar_update.notify_all();
        return;
    }
    exec.Start();

    // Local copies
    raster_picture currentA = m_ctx->m_best_pic;
    raster_picture currentB;
    if (m_ctx->m_dual_mode) currentB = m_ctx->m_best_pic_B.raster_lines.empty() ? m_ctx->m_best_pic : m_ctx->m_best_pic_B;
    double currentCost = m_ctx->m_best_result;

    std::vector<const line_cache_result*> lastLineResultsA(m_ctx->m_height);
    std::vector<const line_cache_result*> lastLineResultsB(m_ctx->m_height);

    // Per-thread staged dual state
    bool stage_focus_B = m_ctx->m_dual_stage_start_B;
    unsigned long long stage_counter = 0ULL;
    const unsigned long long stage_len = std::max(1ULL, m_ctx->m_dual_stage_evals);

    while (m_running.load() && !m_ctx->m_finished.load()) {
        // Mutation choice
        raster_picture candA = currentA;
        raster_picture candB = currentB;
        bool mutateB = false;
        if (m_ctx->m_dual_mode) {
            if (m_ctx->m_dual_strategy == E_DUAL_STRAT_STAGED) {
                // Focus one frame for a block of iterations, then switch
                mutateB = stage_focus_B;
                if (++stage_counter >= stage_len) {
                    stage_focus_B = !stage_focus_B;
                    stage_counter = 0ULL;
                }
            } else {
                int r = exec.Random(1000);
                mutateB = (r < (int)(m_ctx->m_dual_mutate_ratio * 1000.0));
            }
        }
        try {
            if (m_ctx->m_dual_mode && mutateB) {
                mutator.SetDualFrameRole(true);
                mutator.MutateProgram(&candB);
            } else {
                mutator.SetDualFrameRole(false);
                mutator.MutateProgram(&candA);
            }
        } catch (...) {
            continue;
        }

        // Optional cross-share ops
        bool didCrossShare = false;
        bool didCrossSwap = false;
        if (m_ctx->m_dual_mode && m_ctx->m_dual_cross_share_prob > 0.0) {
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
        // Do not recache into the shared context. Let each executor recache lazily
        // in its own per-thread cache when executing the candidates.

        // Evaluate
        double candCost = 0.0;
        if (m_ctx->m_dual_mode) {
            auto dual = m_evaluator.evaluateDual(exec, candA, candB, mutateB);
            candCost = dual.cost;

            // Enter critical section for ctx counters and acceptance + best-tracking
            std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
            ++m_ctx->m_evaluations;
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

            // Best tracking remains centralized in EvaluationContext for UI/state consistency
            if (candCost < m_ctx->m_best_result) {
                m_ctx->ReportEvaluationResultDual(
                    candCost,
                    &candA,
                    &candB,
                    dual.lineResultsA,
                    dual.lineResultsB,
                    dual.spritesMemoryA,
                    dual.spritesMemoryB,
                    &mutator,
                    /*mutatedB=*/mutateB,
                    /*didCrossShare=*/didCrossShare,
                    /*didCrossSwap=*/didCrossSwap);
            }

            m_ctx->CollectStatisticsTickUnsafe();
            m_policy->postIterationUpdate(*m_ctx);
            lock.unlock();
        } else {
            auto single = m_evaluator.evaluateSingle(exec, candA);
            candCost = single.cost;

            std::unique_lock<std::mutex> lock(m_ctx->m_mutex);
            ++m_ctx->m_evaluations;
            if ((m_ctx->m_max_evals > 0 && m_ctx->m_evaluations >= m_ctx->m_max_evals) || m_ctx->m_finished.load()) {
                CTX_MARK_FINISHED((*m_ctx), "runner_max_evals");
                break;
            }

            bool accept = m_policy->accept(currentCost, candCost, *m_ctx);
            if (accept) {
                currentCost = candCost;
                currentA = candA;
                m_policy->onAccepted(candCost, *m_ctx);
            }

            if (candCost < m_ctx->m_best_result) {
                m_ctx->ReportEvaluationResult(
                    candCost,
                    &candA,
                    single.lineResults,
                    single.spritesMemory,
                    &mutator);
            }
            m_ctx->CollectStatisticsTickUnsafe();
            m_policy->postIterationUpdate(*m_ctx);
            lock.unlock();
        }
    }
}



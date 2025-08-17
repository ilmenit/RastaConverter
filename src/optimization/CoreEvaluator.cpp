#include "CoreEvaluator.h"
#include <cmath>
#include <cstring>

SingleEvalResult CoreEvaluator::evaluateSingle(Executor& exec, raster_picture& pic)
{
    SingleEvalResult out;
    out.lineResults.resize(m_ctx->m_height);
    out.cost = (double)exec.ExecuteRasterProgram(&pic, out.lineResults.data());
    // Avoid copying sprites memory here; callers can read from exec.GetSpritesMemory() when needed
    return out;
}

void CoreEvaluator::evaluateSingle(Executor& exec, raster_picture& pic, SingleEvalResult& result)
{
    // Use pre-allocated buffer - no heap allocations
    result.cost = (double)exec.ExecuteRasterProgram(&pic, result.lineResults.data());
    // Avoid copying sprites memory here; callers can read from exec.GetSpritesMemory() when needed
}

DualEvalResult CoreEvaluator::evaluateDual(Executor& exec, raster_picture& picA, raster_picture& picB, bool mutateB)
{
    DualEvalResult out;
    out.lineResultsA.resize(m_ctx->m_height);
    out.lineResultsB.resize(m_ctx->m_height);

    // Coordinate-descent style: render fixed plain, mutated pair-aware against fixed
    distance_accum_t cost_accum = 0;
    if (mutateB) {
        (void)exec.ExecuteRasterProgram(&picA, out.lineResultsA.data());
        std::memcpy(&out.spritesMemoryA, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryA));
        cost_accum = exec.ExecuteRasterProgram(&picB, out.lineResultsB.data(), Executor::DUAL_B, &out.lineResultsA);
        std::memcpy(&out.spritesMemoryB, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryB));
    } else {
        (void)exec.ExecuteRasterProgram(&picB, out.lineResultsB.data());
        std::memcpy(&out.spritesMemoryB, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryB));
        cost_accum = exec.ExecuteRasterProgram(&picA, out.lineResultsA.data(), Executor::DUAL_A, &out.lineResultsB);
        std::memcpy(&out.spritesMemoryA, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryA));
    }

    // The dual-aware executor accumulates the blended base + flicker cost. Use it directly.
    out.cost = (double)cost_accum;

    // Publish fresh A/B line results into context snapshots for better complementary picks
    if (m_ctx->m_dual_mode) {
        const unsigned W = m_ctx->m_width;
        const unsigned H = m_ctx->m_height;
        if (m_ctx->m_snapshot_picture_A.size() != H) m_ctx->m_snapshot_picture_A.resize(H);
        if (m_ctx->m_snapshot_picture_B.size() != H) m_ctx->m_snapshot_picture_B.resize(H);
        // Only copy pointers' rows that actually changed to avoid extra work
        for (unsigned y = 0; y < H; ++y) {
            const line_cache_result* lA = out.lineResultsA[y];
            if (lA) {
                auto &rowA = m_ctx->m_snapshot_picture_A[y];
                if (rowA.size() != W) rowA.resize(W);
                memcpy(rowA.data(), lA->color_row, W);
            }
            const line_cache_result* lB = out.lineResultsB[y];
            if (lB) {
                auto &rowB = m_ctx->m_snapshot_picture_B[y];
                if (rowB.size() != W) rowB.resize(W);
                memcpy(rowB.data(), lB->color_row, W);
            }
        }
    }

    // Flicker heatmap removed for performance - was expensive per-pixel computation
    return out;
}



#include "CoreEvaluator.h"
#include <cmath>
#include <cstring>

SingleEvalResult CoreEvaluator::evaluateSingle(Executor& exec, raster_picture& pic)
{
    SingleEvalResult out;
    out.lineResults.resize(m_ctx->m_height);
    out.cost = (double)exec.ExecuteRasterProgram(&pic, out.lineResults.data());
    std::memcpy(&out.spritesMemory, &exec.GetSpritesMemory(), sizeof(out.spritesMemory));
    return out;
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

    // Maintain flicker heatmap (luma delta per pixel) for UI/save, cheap path using pair tables
    const unsigned W = m_ctx->m_width;
    const unsigned H = m_ctx->m_height;
    if (m_ctx->m_flicker_heatmap.size() != (size_t)W * (size_t)H) {
        m_ctx->m_flicker_heatmap.assign((size_t)W * (size_t)H, 0);
    }
    for (unsigned y = 0; y < H; ++y) {
        const line_cache_result* lA = out.lineResultsA[y];
        const line_cache_result* lB = out.lineResultsB[y];
        if (!lA || !lB) continue;
        const unsigned char* rowA = lA->color_row;
        const unsigned char* rowB = lB->color_row;
        for (unsigned x = 0; x < W; ++x) {
            const unsigned idx = y * W + x;
            const unsigned char a = rowA[x];
            const unsigned char b = rowB[x];
            float dY = 0.0f;
            if (m_ctx->m_have_pair_tables && !m_ctx->m_pair_dY.empty()) {
                dY = m_ctx->m_pair_dY[((unsigned)a << 7) | (unsigned)b];
            } else {
                dY = fabsf(m_ctx->m_palette_y[a] - m_ctx->m_palette_y[b]);
            }
            float scaled = dY * (1.0f / 2.0f);
            if (scaled > 255.0f) scaled = 255.0f; if (scaled < 0.0f) scaled = 0.0f;
            m_ctx->m_flicker_heatmap[idx] = (unsigned char)(scaled + 0.5f);
        }
    }
    return out;
}



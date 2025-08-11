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
    if (mutateB) {
        (void)exec.ExecuteRasterProgram(&picA, out.lineResultsA.data());
        std::memcpy(&out.spritesMemoryA, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryA));
        (void)exec.ExecuteRasterProgram(&picB, out.lineResultsB.data(), Executor::DUAL_B, &out.lineResultsA);
        std::memcpy(&out.spritesMemoryB, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryB));
    } else {
        (void)exec.ExecuteRasterProgram(&picB, out.lineResultsB.data());
        std::memcpy(&out.spritesMemoryB, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryB));
        (void)exec.ExecuteRasterProgram(&picA, out.lineResultsA.data(), Executor::DUAL_A, &out.lineResultsB);
        std::memcpy(&out.spritesMemoryA, &exec.GetSpritesMemory(), sizeof(out.spritesMemoryA));
    }

    // Compute pair objective exactly as in DLAS dual math using YUV fast path and flicker ramp
    const unsigned W = m_ctx->m_width;
    const unsigned H = m_ctx->m_height;
    double total = 0.0;
    float wl = (float)m_ctx->m_flicker_luma_weight;
    if (m_ctx->m_blink_ramp_evals > 0) {
        double t = std::min<double>(1.0, (double)m_ctx->m_evaluations / (double)m_ctx->m_blink_ramp_evals);
        wl = (float)((1.0 - t) * m_ctx->m_flicker_luma_weight_initial + t * m_ctx->m_flicker_luma_weight);
    } else {
        if (m_ctx->m_evaluations < 50000ULL) wl = (float)(0.7 * wl);
    }
    const float wc = (float)m_ctx->m_flicker_chroma_weight;
    const float Tl = (float)m_ctx->m_flicker_luma_thresh;
    const float Tc = (float)m_ctx->m_flicker_chroma_thresh;
    const int pl = m_ctx->m_flicker_exp_luma;
    const int pc = m_ctx->m_flicker_exp_chroma;
    const float* ty = m_ctx->m_target_y.data();
    const float* tu = m_ctx->m_target_u.data();
    const float* tv = m_ctx->m_target_v.data();
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
            const float Ya = m_ctx->m_palette_y[a];
            const float Ua = m_ctx->m_palette_u[a];
            const float Va = m_ctx->m_palette_v[a];
            const float Yb = m_ctx->m_palette_y[b];
            const float Ub = m_ctx->m_palette_u[b];
            const float Vb = m_ctx->m_palette_v[b];
            const float Ybl = 0.5f * (Ya + Yb);
            const float Ubl = 0.5f * (Ua + Ub);
            const float Vbl = 0.5f * (Va + Vb);
            const float dy = Ybl - ty[idx];
            const float du = Ubl - tu[idx];
            const float dv = Vbl - tv[idx];
            double base = (double)(dy * dy + du * du + dv * dv);
            const float dY = fabsf(Ya - Yb);
            const float dC = sqrtf((Ua - Ub) * (Ua - Ub) + (Va - Vb) * (Va - Vb));
            float yl = dY - Tl; if (yl < 0) yl = 0;
            float yc = dC - Tc; if (yc < 0) yc = 0;
            double flick = 0.0;
            if (wl > 0) { double t = yl; if (pl == 2) t = t*t; else if (pl == 3) t = t*t*t; else t = pow(t, (double)pl); flick += wl * t; }
            if (wc > 0) { double t = yc; if (pc == 2) t = t*t; else if (pc == 3) t = t*t*t; else t = pow(t, (double)pc); flick += wc * t; }
            total += base + flick;
            float scaled = dY * (1.0f / 2.0f);
            if (scaled > 255.0f) scaled = 255.0f; if (scaled < 0.0f) scaled = 0.0f;
            m_ctx->m_flicker_heatmap[idx] = (unsigned char)(scaled + 0.5f);
        }
    }
    out.cost = total;
    return out;
}



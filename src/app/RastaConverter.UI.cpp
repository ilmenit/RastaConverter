#include "RastaConverter.h"
#include "TargetPicture.h"
#include "color/rgb.h"
#include <algorithm>
#include <cstdio>

// External const array for mutation names
extern const char* mutation_names[E_MUTATION_MAX];

void RastaConverter::ShowInputBitmap()
{
    unsigned int width = FreeImage_GetWidth(m_imageProcessor.GetInputBitmap());
    unsigned int height = FreeImage_GetHeight(m_imageProcessor.GetInputBitmap());
    gui.DisplayBitmap(0, 0, m_imageProcessor.GetInputBitmap());
    gui.DisplayText(0, height + 10, "Source");
    // Label center panel depending on mode
    const auto& ctx = m_optimizer.GetEvaluationContext();
    if (ctx.m_dual_mode) {
        gui.DisplayText(width * 4, height + 200, "[A]=A  [Z]=B  [B]=Blended");
    } else {
        gui.DisplayText(width * 2, height + 10, "Current output");
        gui.DisplayText(width * 4, height + 10, "Destination");
    }
}

void RastaConverter::ShowDestinationLine(int y)
{
    if (!cfg.preprocess_only && !cfg.dual_mode)
    {
        unsigned int where_x = 2*FreeImage_GetWidth(m_imageProcessor.GetInputBitmap());
        gui.DisplayBitmapLine(where_x, y, y, m_imageProcessor.GetDestinationBitmap());
    }
}

void RastaConverter::ShowDestinationBitmap()
{
    // show when not in dual mode, otherwise leave empty
    if (!cfg.dual_mode) {
        gui.DisplayBitmap(FreeImage_GetWidth(m_imageProcessor.GetDestinationBitmap()) * 2, 
                      0, 
                      m_imageProcessor.GetDestinationBitmap());
    }
}

void RastaConverter::ShowLastCreatedPicture()
{
    // Snapshot evaluation data under lock to avoid data races
    std::vector<std::vector<unsigned char>> created_picture_copy;
    std::vector<std::vector<unsigned char>> created_picture_copy_B;
    bool temporal = false;
    unsigned snap_width = 0;
    unsigned snap_height = 0;
    {
        auto& ctx = m_optimizer.GetEvaluationContext();
        std::unique_lock<std::mutex> lock{ ctx.m_mutex };
        snap_width = ctx.m_width;
        snap_height = ctx.m_height;
        created_picture_copy = ctx.m_created_picture; // copy
        if (ctx.m_dual_mode) {
            temporal = true;
            created_picture_copy_B = ctx.m_created_picture_B;
        }
    }
    // Check if the created picture exists and has the right dimensions
    if (created_picture_copy.empty() || created_picture_copy.size() < snap_height) {
        // Fill with black if not initialized yet (fast path)
        RGBQUAD black = { 0, 0, 0, 0 };
        FreeImage_FillBackground(output_bitmap, &black, 0);
    } else {
        // Draw the created picture (blend A/B in dual mode) using direct scanline writes
        const unsigned pitch = FreeImage_GetPitch(output_bitmap);
        for (int y = 0; y < (int)snap_height; ++y) {
            if (created_picture_copy[y].size() < snap_width) continue;
            // Write directly to scanline for this y; keep same orientation as previous per-pixel path
            BYTE* row = FreeImage_GetScanLine(output_bitmap, y);
            BYTE* p = row;
            if (temporal && y < (int)created_picture_copy_B.size() && created_picture_copy_B[y].size() >= snap_width) {
                if (dual_view_mode == 1) {
                    // Show A only
                    const unsigned char* src = created_picture_copy[y].data();
                    for (unsigned x = 0; x < snap_width; ++x) {
                        const rgb c = atari_palette[src[x]];
                        // 24-bit BGR
                        *p++ = c.b; *p++ = c.g; *p++ = c.r;
                    }
                } else if (dual_view_mode == 2) {
                    // Show B only
                    const unsigned char* srcB = created_picture_copy_B[y].data();
                    for (unsigned x = 0; x < snap_width; ++x) {
                        const rgb c = atari_palette[srcB[x]];
                        *p++ = c.b; *p++ = c.g; *p++ = c.r;
                    }
                } else {
                    // Blended A/B (simple RGB average for preview)
                    const unsigned char* srcA = created_picture_copy[y].data();
                    const unsigned char* srcB = created_picture_copy_B[y].data();
                    for (unsigned x = 0; x < snap_width; ++x) {
                        const rgb ca = atari_palette[srcA[x]];
                        const rgb cb = atari_palette[srcB[x]];
                        const unsigned char r = (unsigned char)(((int)ca.r + (int)cb.r) >> 1);
                        const unsigned char g = (unsigned char)(((int)ca.g + (int)cb.g) >> 1);
                        const unsigned char b = (unsigned char)(((int)ca.b + (int)cb.b) >> 1);
                        *p++ = b; *p++ = g; *p++ = r;
                    }
                }
            } else {
                // Single frame path
                const unsigned char* src = created_picture_copy[y].data();
                for (unsigned x = 0; x < snap_width; ++x) {
                    const rgb c = atari_palette[src[x]];
                    *p++ = c.b; *p++ = c.g; *p++ = c.r;
                }
            }
            // Ensure we advance to next scanline start
            (void)pitch; // pitch may exceed 3*width; FreeImage_GetScanLine already gives correct row start
        }
    }

    // Display the picture
    gui.DisplayBitmap(FreeImage_GetWidth(m_imageProcessor.GetInputBitmap()), 0, output_bitmap);
}

void RastaConverter::ShowMutationStats()
{
    // Snapshot under lock
    unsigned long long evaluations = 0;
    unsigned long long last_best_eval = 0;
    double best_result = 0.0;
    int stats[E_MUTATION_MAX] = {0};
    {
        auto& ctx = m_optimizer.GetEvaluationContext();
        std::unique_lock<std::mutex> lock{ ctx.m_mutex };
        evaluations = ctx.m_evaluations;
        last_best_eval = ctx.m_last_best_evaluation;
        best_result = ctx.m_best_result;
        for (int i = 0; i < E_MUTATION_MAX; ++i) stats[i] = ctx.m_mutation_stats[i];
    }

    // Layout params
    const int paneW = (int)FreeImage_GetWidth(m_imageProcessor.GetInputBitmap());
    const int baseY = 250;
    const int rowH = 20;
    const int colA_X = 0;          // center pane left edge
    const int colB_X = paneW * 2;      // right pane left edge
    const int summaryX = paneW * 4;    // far right area

    // Two-column list of mutation stats
    const int per_col = (E_MUTATION_MAX + 1) / 2;
    for (int i = 0; i < E_MUTATION_MAX; ++i) {
        int col = (i < per_col) ? 0 : 1;
        int row = (i < per_col) ? i : (i - per_col);
        int x = (col == 0) ? colA_X : colB_X;
        int y = baseY + rowH * row;
        gui.DisplayText(x, y, std::string(mutation_names[i]) +
                               std::string("  ") +
                               std::to_string(stats[i]));
    }

    // Dual-mode additional stats below the second column
    const auto& ctx = m_optimizer.GetEvaluationContext();
    if (ctx.m_dual_mode) {
        int dualBaseY = baseY + rowH * per_col + 10;
        int x = colB_X;
        gui.DisplayText(x, dualBaseY + 0,  std::string("DualComplementValue  ") + std::to_string((unsigned long long)ctx.m_stat_dualComplementValue.load()));
        gui.DisplayText(x, dualBaseY + 20, std::string("DualSeedAdd         ") + std::to_string((unsigned long long)ctx.m_stat_dualSeedAdd.load()));
        gui.DisplayText(x, dualBaseY + 40, std::string("CrossCopyLine       ") + std::to_string((unsigned long long)ctx.m_stat_crossCopyLine.load()));
        gui.DisplayText(x, dualBaseY + 60, std::string("CrossSwapLine       ") + std::to_string((unsigned long long)ctx.m_stat_crossSwapLine.load()));
    }

    // Move general summary far to the right to free space for mutation lists
    // Helper to format large integers with thousands separators for readability
    auto format_with_commas = [](unsigned long long value) -> std::string {
        std::string s = std::to_string(value);
        std::string out;
        out.reserve(s.size() + s.size() / 3);
        int groupCount = 0;
        for (int i = (int)s.size() - 1; i >= 0; --i) {
            out.push_back(s[i]);
            if (++groupCount == 3 && i != 0) {
                out.push_back(',');
                groupCount = 0;
            }
        }
        std::reverse(out.begin(), out.end());
        return out;
    };

    gui.DisplayText(summaryX, baseY + 0,  std::string("Evaluations: ") + format_with_commas(evaluations));
    gui.DisplayText(summaryX, baseY + 20, std::string("LastBest: ") + format_with_commas(last_best_eval));
    gui.DisplayText(summaryX, baseY + 40, std::string("Rate: ") + std::to_string((unsigned long long)m_optimizer.GetRate()) + std::string("                "));
    {
        double norm = m_outputManager.NormalizeScore(best_result);
        std::string normText;
        if (std::isfinite(norm) && norm < 1e12) {
            char buf[64];
            // Print with 6 decimals max for readability
            std::snprintf(buf, sizeof(buf), "%.6f", norm);
            normText = buf;
        } else {
            normText = "-"; // hide unreasonable values (e.g., uninitialized)
        }
        gui.DisplayText(summaryX, baseY + 60, std::string("Norm. Dist: ") + normText + std::string("                "));
    }
}



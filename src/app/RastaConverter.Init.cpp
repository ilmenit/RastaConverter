#include "RastaConverter.h"
#include "mutation/RasterMutator.h"

void RastaConverter::Init()
{
    if (!cfg.continue_processing)
    {
        // Create initial raster picture based on configuration
        raster_picture m(m_imageProcessor.GetHeight());
        init_finished = false;

        const auto& colorIndexes = m_imageProcessor.GetColorIndexesOnDstPicture();
        
        if (colorIndexes.size() < 5)
            m_programGenerator.CreateLowColorRasterPicture(&m, colorIndexes);
        else if (cfg.init_type == E_INIT_RANDOM)
            m_programGenerator.CreateRandomRasterPicture(&m, m_imageProcessor.GetPicture());
        else if (cfg.init_type == E_INIT_EMPTY)
            m_programGenerator.CreateEmptyRasterPicture(&m);
        else // LESS or SMART
            m_programGenerator.CreateSmartRasterPicture(
                &m, 
                cfg.init_type, 
                m_imageProcessor.GetPicture(), 
                colorIndexes
            );
        
        // Set initial program for optimization
        if (cfg.dual_mode) {
            // In dual mode, allow full 128-color palette on every line to avoid constraint by single-frame destination
            std::vector<std::vector<unsigned char>> fullPalettePerLine;
            fullPalettePerLine.resize(m_imageProcessor.GetHeight());
            for (int y = 0; y < m_imageProcessor.GetHeight(); ++y) {
                auto& vec = fullPalettePerLine[y];
                vec.resize(128);
                for (int i = 0; i < 128; ++i) vec[i] = (unsigned char)(i * 2);
            }
            m_optimizer.SetInitialProgram(m, fullPalettePerLine);

            // Initialize frame B according to dual_init now that A is set
            if (cfg.dual_init == E_DUAL_INIT_DUP) {
                auto &ctx = m_optimizer.GetEvaluationContext();
                std::unique_lock<std::mutex> lock{ ctx.m_mutex };
                ctx.m_best_pic_B = ctx.m_best_pic;
            } else if (cfg.dual_init == E_DUAL_INIT_RANDOM || cfg.dual_init == E_DUAL_INIT_ANTI) {
                auto &ctx = m_optimizer.GetEvaluationContext();
                raster_picture tempB;
                {
                    std::unique_lock<std::mutex> lock{ ctx.m_mutex };
                    tempB = ctx.m_best_pic; // copy A
                }
                // Apply randomized divergence to B outside of ctx lock
                RasterMutator initMut(&ctx, /*thread_id=*/0);
                unsigned long long seed = (unsigned long long)((cfg.continue_processing && cfg.have_resume_seed) ? cfg.resume_seed : cfg.initial_seed);
                initMut.Init(seed ^ 0xC0FFEEULL);
                initMut.SetDualFrameRole(true);
                const int iterations = (cfg.dual_init == E_DUAL_INIT_ANTI) ? (m_imageProcessor.GetHeight() * 2) : (std::max)(1, m_imageProcessor.GetHeight() / 2);
                for (int it = 0; it < iterations; ++it) {
                    try { initMut.MutateProgram(&tempB); } catch (...) {}
                }
                {
                    std::unique_lock<std::mutex> lock{ ctx.m_mutex };
                    ctx.m_best_pic_B = tempB;
                }
            }
        } else {
            m_optimizer.SetInitialProgram(m, m_imageProcessor.GetPossibleColorsForEachLine());
        }
    }

    init_finished = true;
}



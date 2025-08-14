#include "RastaConverter.h"
#include "color/Distance.h"
#include "target/TargetPicture.h"
#include "utils/mt19937int.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <climits>
#include <cmath>
#include <atomic>
#include "optimization/EvaluationContext.h"

// External const array for mutation names
extern const char* mutation_names[E_MUTATION_MAX];

RastaConverter::RastaConverter()
    : init_finished(false)
    , output_bitmap(nullptr)
{
}

RastaConverter::~RastaConverter()
{
    if (output_bitmap) {
        FreeImage_Unload(output_bitmap);
    }
}

void RastaConverter::SetConfig(Configuration& config)
{
    cfg = config;
}

void RastaConverter::Message(std::string message)
{
    if (!cfg.preprocess_only) {
        time_t t;
        t = time(NULL);
        string current_time = ctime(&t);
        current_time = current_time.substr(0, current_time.length() - 1);
        gui.DisplayText(0, 440, current_time + string(": ") + message + string("                    "));
    }
}

void RastaConverter::Error(std::string error)
{
    if (cfg.quiet) {
        std::cerr << error << std::endl;
        std::exit(1);
    }
    gui.Error(error);
    exit(1);
}

void RastaConverter::LoadAtariPalette()
{
    Message("Loading palette");
    if (!::LoadAtariPalette(cfg.palette_file))
        Error("Error opening .act palette file");
}

bool RastaConverter::ProcessInit()
{
    // Initialize GUI
    if (!cfg.quiet) {
        gui.Init(cfg.command_line);
    }

    // Load Atari palette
    LoadAtariPalette();
    
    // Initialize image processor
    m_imageProcessor.Initialize(cfg);
    
    // Load input bitmap
    if (!m_imageProcessor.LoadInputBitmap())
        Error("Error loading Input Bitmap!");
    
    // Initialize local structure
    m_imageProcessor.InitLocalStructure();
    
    // Show input image
    if (!cfg.preprocess_only)
        ShowInputBitmap();
    
    // Set preprocessing distance function
    SetDistanceFunction(cfg.pre_dstf);
    
    // Prepare destination picture (apply dithering and transformations)
    // In GUI mode, run asynchronously to avoid freezing the window event loop.
    if (!cfg.quiet)
    {
        // Enable progressive preview of destination rows during preprocessing
        m_imageProcessor.ResetRowProgress();
        std::atomic<bool> preprocess_done{false};
        std::exception_ptr preprocess_exc;
        std::thread prep_thread([this, &preprocess_done, &preprocess_exc]() {
            try {
                this->m_imageProcessor.PrepareDestinationPicture();
            } catch (...) {
                preprocess_exc = std::current_exception();
            }
            preprocess_done.store(true, std::memory_order_release);
        });

        // Pump UI events while preprocessing runs
        while (!preprocess_done.load(std::memory_order_acquire))
        {
            // draw a small status note under source to show activity
            gui.DisplayText(0, (int)FreeImage_GetHeight(m_imageProcessor.GetInputBitmap()) + 30, "Preprocessing... (dither)   ");
            // Progressive destination preview: show any rows marked as done
            // Only in non-dual mode we display the destination pane
            if (!cfg.dual_mode) {
                const int H = m_imageProcessor.GetHeight();
                for (int y = 0; y < H; ++y) {
                    if (m_imageProcessor.IsRowDone(y)) {
                        ShowDestinationLine(y);
                    }
                }
            }
            (void)gui.NextFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        prep_thread.join();
        if (preprocess_exc) {
            try { std::rethrow_exception(preprocess_exc); } catch (const std::exception& ex) { Error(ex.what()); } catch (...) { Error("Preprocess failed"); }
        }
        // Clear the preprocessing status line and present
        gui.DisplayText(0, (int)FreeImage_GetHeight(m_imageProcessor.GetInputBitmap()) + 30, std::string(200, ' '));
        gui.DisplayBitmap(0, 0, m_imageProcessor.GetInputBitmap());
    }
    else
    {
        m_imageProcessor.PrepareDestinationPicture();
    }
    
    // Save the preprocessed image
    m_outputManager.Initialize(cfg.output_file, m_imageProcessor.GetWidth(), m_imageProcessor.GetHeight());
    m_outputManager.SavePicture(cfg.output_file + "-src.png", m_imageProcessor.GetInputBitmap());
    m_outputManager.SavePicture(cfg.output_file + "-dst.png", m_imageProcessor.GetDestinationBitmap());

    // Ensure Destination preview is drawn early (before entering MainLoop)
    if (!cfg.preprocess_only) {
        ShowDestinationBitmap();
    }
    
    // Exit if only preprocessing was requested
    if (cfg.preprocess_only)
        return false; // signal to caller to skip MainLoop after saving preprocessed images
    
    // Load OnOff map if specified
    if (!cfg.on_off_file.empty())
        LoadOnOffFile(cfg.on_off_file.c_str());
    
    // Set post-processing distance function
    SetDistanceFunction(cfg.dstf);
    
    // Generate picture error map
    m_imageProcessor.GeneratePictureErrorMap();
    
    // Find possible colors for each line
    m_imageProcessor.FindPossibleColors();
    
    // Initialize program generator
    m_programGenerator.Initialize(m_imageProcessor.GetWidth(), m_imageProcessor.GetHeight());
    
    // Initialize optimizer with region-based mutation from config
    bool useRegionalMutation = (cfg.mutation_strategy == E_MUTATION_REGIONAL);
    
    const std::vector<distance_t>* pictureAllErrors[128];
    for (int i = 0; i < 128; ++i)
        pictureAllErrors[i] = &m_imageProcessor.GetPictureAllErrors()[i];
    
    // Respect legacy behavior: if no OnOff file provided, do not apply any per-line register disabling
    const OnOffMap* onoffPtr = cfg.on_off_file.empty() ? nullptr : &m_onOffMap;

    m_optimizer.Initialize(
        cfg.threads, 
        m_imageProcessor.GetWidth(), 
        m_imageProcessor.GetHeight(), 
        &m_imageProcessor.GetPicture(),
        pictureAllErrors,
        cfg.max_evals,
        cfg.save_period,
        cfg.cache_size,
        (cfg.continue_processing && cfg.have_resume_seed) ? cfg.resume_seed : cfg.initial_seed,
        onoffPtr,
        useRegionalMutation,
        cfg.optimizer_type
    );

    // Propagate dual-frame configuration into evaluation context and precompute YUV if needed
    {
        auto& ctx = m_optimizer.GetEvaluationContext();
        std::unique_lock<std::mutex> lock{ ctx.m_mutex };
        ctx.m_dual_mode = cfg.dual_mode;
        // Only YUV blend space/distance are supported in current build
        ctx.m_blend_space = E_BLEND_YUV;
        ctx.m_blend_distance = E_DISTANCE_YUV;
        ctx.m_blend_gamma = cfg.blend_gamma;
        ctx.m_flicker_luma_weight = cfg.flicker_luma_weight;
        ctx.m_flicker_chroma_weight = cfg.flicker_chroma_weight;
        // Strategy and init from config
        ctx.m_dual_strategy = cfg.dual_strategy;
        ctx.m_dual_init = cfg.dual_init;
        ctx.m_dual_mutate_ratio = cfg.dual_mutate_ratio;
        ctx.m_dual_cross_share_prob = cfg.dual_cross_share_prob;
        ctx.m_dual_both_frames_prob = cfg.dual_both_frames_prob;
        ctx.m_dual_stage_evals = cfg.dual_stage_evals;
        ctx.m_dual_stage_start_B = cfg.dual_stage_start_B;
        // Sensible defaults for dual mode if user did not provide a ramp
        ctx.m_blink_ramp_evals = (cfg.blink_ramp_evals > 0) ? cfg.blink_ramp_evals : 250000ULL;
        ctx.m_flicker_luma_weight_initial = cfg.flicker_luma_weight_initial > 0.0 ? cfg.flicker_luma_weight_initial : 0.6;
        // Initialize staged focus globals for all threads
        ctx.m_dual_stage_focus_B.store(ctx.m_dual_stage_start_B, std::memory_order_relaxed);
        ctx.m_dual_stage_counter.store(0ULL, std::memory_order_relaxed);
        // Prepare YUV data if using dual mode
        // Precompute dual transforms using source image, not destination
        const std::vector<screen_line>* dualTarget = &m_imageProcessor.GetSourcePicture();
        if (cfg.dual_mode) {
            // In dual mode, keep the context picture pointing to the source so any
            // sampling (e.g., in the mutator) uses the original rescaled image.
            ctx.m_picture = dualTarget->data();
            lock.unlock();
            // Precompute transforms after setting m_picture
            m_optimizer.GetEvaluationContext().PrecomputeDualTransforms();
            lock.lock();
            // If history is still the minimal default (1), increase it for dual mode to help acceptance
            if (m_optimizer.GetEvaluationContext().m_history_length_config <= 1) {
                m_optimizer.GetEvaluationContext().m_history_length_config = 16;
            }
        }
    }
    
    // If resuming, load optimizer state now that optimizer is constructed
    if (cfg.continue_processing) {
        const std::string base = cfg.output_file.empty() ? std::string("output.png") : cfg.output_file;
        m_optimizer.LoadState(base + ".lahc");
    }
    
    // Allocate output bitmap
    output_bitmap = FreeImage_Allocate(
        m_imageProcessor.GetWidth(), 
        m_imageProcessor.GetHeight(), 
        24
    );
    
    return true;
}



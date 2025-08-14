#include "RastaConverter.h"
#include "TargetPicture.h"
#include <chrono>
#include <iostream>

void RastaConverter::MainLoop()
{
    Message("Optimization started.");
    
    // Initialize optimization
    Init();
    
    // Start optimization
    m_optimizer.Run();
    
    // Track timing and performance
    bool pending_update = false;
    
    // Run until finished
    bool running = true;
    while (running && !m_optimizer.IsFinished())
    {
        #ifdef UI_DEBUG
        static auto last_heartbeat = std::chrono::steady_clock::now();
        auto now_hb = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now_hb - last_heartbeat).count() >= 5) {
            auto& evalContext = m_optimizer.GetEvaluationContext();
            std::unique_lock<std::mutex> lock{ evalContext.m_mutex };
            std::cout << "[HB] UI loop heartbeat: finished=" << (evalContext.m_finished.load() ? 1 : 0)
                      << ", threads_active=" << evalContext.m_threads_active.load()
                      << ", evals=" << evalContext.m_evaluations << std::endl;
            last_heartbeat = now_hb;
        }
        #endif
        // Update statistics
        m_optimizer.UpdateRate();
        
        // Check for auto-save
        if (m_optimizer.CheckAutoSave()) {
            SaveBestSolution();
            Message("Auto-saved.");
        }
        
        // Update display periodically
        if (pending_update)
        {
            pending_update = false;
            ShowLastCreatedPicture();
        }
        
        // Show mutation statistics
        ShowMutationStats();
        
        // Process UI commands
        auto ui_cmd = gui.NextFrame();
        #ifdef UI_DEBUG
        std::cout << "[UI] NextFrame returned " << (int)ui_cmd << std::endl;
        #endif
        switch (ui_cmd)
        {
        case GUI_command::SAVE:
            SaveBestSolution();
            Message("Saved.");
            break;
        case GUI_command::STOP:
            std::cout << "[RC] STOP command received from GUI" << std::endl;
            running = false;
            break;
        case GUI_command::REDRAW:
            ShowInputBitmap();
            ShowDestinationBitmap();
            ShowLastCreatedPicture();
            ShowMutationStats();
            break;
        case GUI_command::SHOW_BLENDED:
            dual_view_mode = 0; pending_update = true; break;
        case GUI_command::SHOW_A:
            dual_view_mode = 1; pending_update = true; break;
        case GUI_command::SHOW_B:
            dual_view_mode = 2; pending_update = true; break;
        default:
            break;
        }
        
        // Check if optimization state has changed
        auto& evalContext = m_optimizer.GetEvaluationContext();
        bool do_init_redraw = false;
        bool do_autosave = false;
        {
            std::unique_lock<std::mutex> lock{ evalContext.m_mutex };
            
            // Handle initialization completion (first frame data available)
            if (evalContext.m_update_initialized)
            {
                evalContext.m_update_initialized = false;
                do_init_redraw = true;
            }
            
            // Handle improvement updates
            if (evalContext.m_update_improvement)
            {
                evalContext.m_update_improvement = false;
                pending_update = true;
            }
            
            // Handle autosave updates
            if (evalContext.m_update_autosave)
            {
                evalContext.m_update_autosave = false;
                do_autosave = true;
            }
            
            // Wait for updates or timeout
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            evalContext.m_condvar_update.wait_until(lock, deadline);

            // Heartbeat: detect stagnation where no threads are active but not finished
            #ifdef THREAD_DEBUG
            if (!evalContext.m_finished.load() && evalContext.m_threads_active.load() <= 0) {
                std::cout << "[RC] No active workers but not finished. Requesting DLAS re-spawn." << std::endl;
            }
            #endif
        }

        // Perform UI updates and autosave outside of the lock to avoid deadlocks
        if (do_init_redraw)
        {
            ShowInputBitmap();
            ShowDestinationBitmap();
            ShowLastCreatedPicture();
            ShowMutationStats();
        }
        if (do_autosave)
        {
            SaveBestSolution();
            Message("Auto-saved.");
        }
    }
    
    // Stop optimization (controller will forward Stop to optimizer and log)
    m_optimizer.Stop();
    
    // Log exit state
    {
        auto& evalContext = m_optimizer.GetEvaluationContext();
        std::unique_lock<std::mutex> lock{ evalContext.m_mutex };
        #ifdef THREAD_DEBUG
        std::cout << "[RC] MainLoop exiting: running=false, ctx.finished=" << (evalContext.m_finished.load() ? 1 : 0)
                  << ", threads_active=" << evalContext.m_threads_active.load()
                  << ", evals=" << evalContext.m_evaluations << std::endl;
        #endif
        if (evalContext.m_finished.load()) {
            #ifdef THREAD_DEBUG
            std::cout << "[RC] Finish reason='" << evalContext.m_finish_reason
                      << "' at " << evalContext.m_finish_file << ":" << evalContext.m_finish_line
                      << ", evals_at=" << evalContext.m_finish_evals_at
                      << ", threads_active_at=" << evalContext.m_threads_active.load()
                      << ", best_result=" << evalContext.m_best_result
                      << ", cost_max=" << evalContext.m_cost_max
                      << ", N=" << evalContext.m_N
                      << ", current_cost=" << evalContext.m_current_cost
                      << ", previous_idx=" << evalContext.m_previous_results_index
                      << std::endl;
            #endif
        }
    }

    // Save final solution
    SaveBestSolution();
}



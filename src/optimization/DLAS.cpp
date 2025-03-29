#include "DLAS.h"
#include "../mutation/RasterMutator.h"
#include <cmath>
#include <fstream>
#include <algorithm>

DLAS::DLAS(EvaluationContext* context, Executor* executor, Mutator* mutator)
    : m_context(context)
    , m_executor(executor)
    , m_mutator(mutator)
    , m_running(false)
    , m_solutions(1)
{
}

void DLAS::Initialize(const raster_picture& initialSolution)
{
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    
    m_context->m_best_pic = initialSolution;
    
    // Evaluate the initial solution
    std::vector<const line_cache_result*> line_results(m_context->m_height);
    raster_picture pic_copy = initialSolution;
    pic_copy.recache_insns(m_context->m_insn_seq_cache, m_context->m_linear_allocator);
    double result = m_executor->ExecuteRasterProgram(&pic_copy, line_results.data());
    
    // Initialize DLAS parameters
    const double init_margin = result * 0.1; // 10% margin
    m_context->m_cost_max = result + init_margin;
    m_context->m_current_cost = result;
    
    // Initialize thread-specific previous results
    int solutions = m_context->m_thread_count > 0 ? m_context->m_thread_count : 1;
    m_context->m_thread_previous_costs.resize(solutions, result);
    m_context->m_previous_results.resize(solutions, m_context->m_cost_max);
    m_context->m_N = solutions;
    
    // Save results for visualization
    m_context->m_created_picture.resize(m_context->m_height);
    m_context->m_created_picture_targets.resize(m_context->m_height);
    
    for (int y = 0; y < (int)m_context->m_height; ++y)
    {
        const line_cache_result& lcr = *line_results[y];
        m_context->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_context->m_width);
        m_context->m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_context->m_width);
    }
    
    memcpy(&m_context->m_sprites_memory, &m_executor->GetSpritesMemory(), sizeof(m_context->m_sprites_memory));
    
    m_context->m_best_result = result;
    m_context->m_initialized = true;
    m_context->m_update_initialized = true;
    m_context->m_condvar_update.notify_one();
}

void DLAS::RunWorker(int threadId)
{
    // Copy best picture to work with
    raster_picture pic = m_context->m_best_pic;
    double best_result = m_context->m_best_result;
    
    // Create local executor for this thread
    Executor local_executor;
    local_executor.Init(m_context->m_width, 
                        m_context->m_height, 
                        m_context->m_picture_all_errors, 
                        m_context->m_picture, 
                        m_context->m_onoff, 
                        m_context, 
                        m_solutions, 
                        time(NULL) + threadId * 123456789ULL, 
                        m_context->m_cache_size, 
                        threadId);
    
    // Use thread-specific mutator
    Mutator* mutator = m_thread_mutators[threadId].get();
    
    std::vector<const line_cache_result*> line_results(m_context->m_height);
    
    while (m_running)
    {
        // Check if we need to clear caches due to memory usage
        if (m_context->m_linear_allocator.size() > m_context->m_cache_size)
        {
            std::unique_lock<std::mutex> cache_lock(m_context->m_cache_mutex);
            
            // Check again after acquiring the lock (another thread might have cleared)
            if (m_context->m_linear_allocator.size() > m_context->m_cache_size)
            {
                // Clear the shared caches
                m_context->m_insn_seq_cache.clear();
                m_context->m_linear_allocator.clear();
                
                // Recache the best picture
                m_context->m_best_pic.recache_insns(m_context->m_insn_seq_cache, m_context->m_linear_allocator);
            }
        }
        
        // Create a new candidate by mutation
        raster_picture new_pic = pic;
        mutator->MutateProgram(&new_pic);
        
        // Evaluate the new candidate
        double result = local_executor.ExecuteRasterProgram(&new_pic, line_results.data());
        
        // Update global state with the evaluation
        std::unique_lock<std::mutex> lock(m_context->m_mutex);
        
        ++m_context->m_evaluations;
        
        // Store previous cost before potential update
        double prev_cost = m_context->m_current_cost;
        
        // Calculate index for circular array
        int solutions = (int)m_context->m_previous_results.size();
        size_t l = m_context->m_previous_results_index % solutions;
        
        // DLAS acceptance criteria
        if (result == m_context->m_current_cost || result < m_context->m_cost_max)
        {
            // Accept the candidate solution
            pic = new_pic;
            m_context->m_current_cost = result;
            
            // Update best solution if better
            if (result < m_context->m_best_result)
            {
                m_context->m_last_best_evaluation = m_context->m_evaluations;
                m_context->m_best_pic = new_pic;
                m_context->m_best_pic.uncache_insns();
                m_context->m_best_result = result;
                
                // Update visualization state
                m_context->m_created_picture.resize(m_context->m_height);
                m_context->m_created_picture_targets.resize(m_context->m_height);
                
                for (int y = 0; y < (int)m_context->m_height; ++y)
                {
                    const line_cache_result& lcr = *line_results[y];
                    m_context->m_created_picture[y].assign(lcr.color_row, lcr.color_row + m_context->m_width);
                    m_context->m_created_picture_targets[y].assign(lcr.target_row, lcr.target_row + m_context->m_width);
                }
                
                memcpy(&m_context->m_sprites_memory, &local_executor.GetSpritesMemory(), sizeof(m_context->m_sprites_memory));
                
                // Update mutation statistics
                const int* current_mutations = mutator->GetCurrentMutations();
                for (int i = 0; i < E_MUTATION_MAX; ++i)
                {
                    if (current_mutations[i])
                    {
                        m_context->m_mutation_stats[i] += current_mutations[i];
                    }
                }
                
                m_context->m_update_improvement = true;
                m_context->m_condvar_update.notify_one();
            }
            
            // DLAS replacement strategy
            if (m_context->m_current_cost > m_context->m_previous_results[l])
            {
                m_context->m_previous_results[l] = m_context->m_current_cost;
            }
            else if (m_context->m_current_cost < m_context->m_previous_results[l] &&
                    m_context->m_current_cost < prev_cost)
            {
                // Track if we're removing a max value
                if (m_context->m_previous_results[l] == m_context->m_cost_max)
                {
                    --m_context->m_N;
                }
                
                // Replace the value
                m_context->m_previous_results[l] = m_context->m_current_cost;
                
                // Recompute max and N if needed
                if (m_context->m_N <= 0)
                {
                    // Find new cost_max
                    m_context->m_cost_max = *std::max_element(
                        m_context->m_previous_results.begin(),
                        m_context->m_previous_results.end()
                    );
                    
                    // Recount occurrences of max
                    m_context->m_N = std::count(
                        m_context->m_previous_results.begin(),
                        m_context->m_previous_results.end(),
                        m_context->m_cost_max
                    );
                }
            }
        }
        
        // Always increment index
        ++m_context->m_previous_results_index;
        
        // Handle saving and termination checks
        if (m_context->m_save_period && m_context->m_evaluations % m_context->m_save_period == 0)
        {
            m_context->m_update_autosave = true;
            m_context->m_condvar_update.notify_one();
        }
        
        // Check for auto-save based on time
        if (m_context->m_save_period == -1) // auto
        {
            auto now = std::chrono::steady_clock::now();
            using namespace std::literals::chrono_literals;
            if (now - m_context->m_previous_save_time > 30s)
            {
                m_context->m_previous_save_time = now;
                m_context->m_update_autosave = true;
                m_context->m_condvar_update.notify_one();
            }
        }
        
        if (m_context->m_evaluations >= m_context->m_max_evals)
        {
            m_context->m_finished = true;
            m_context->m_condvar_update.notify_one();
            break;
        }
        
        // Update statistics periodically
        if (m_context->m_evaluations % 10000 == 0)
        {
            statistics_point stats;
            stats.evaluations = (unsigned)m_context->m_evaluations;
            stats.seconds = (unsigned)(time(NULL) - m_context->m_time_start);
            stats.distance = m_context->m_current_cost;
            
            m_context->m_statistics.push_back(stats);
        }
        
        // Update local best if global best improved
        if (m_context->m_best_result < best_result)
        {
            pic = m_context->m_best_pic;
            best_result = m_context->m_best_result;
            pic.recache_insns(m_thread_local_cache, m_thread_local_allocator);
        }
    }
    
    // Signal that this thread has completed
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    --m_context->m_threads_active;
    m_context->m_condvar_update.notify_one();
}

void DLAS::Run()
{
    m_running = true;
    
    // Create thread-specific mutators
    m_thread_mutators.resize(m_context->m_thread_count);
    unsigned long long seed = time(NULL);
    for (int i = 0; i < m_context->m_thread_count; ++i)
    {
        m_thread_mutators[i] = std::make_unique<RasterMutator>(m_context, i);
        m_thread_mutators[i]->Init(seed + i * 123456789ULL);
    }
    
    // Start worker threads
    m_worker_threads.resize(m_context->m_thread_count);
    m_context->m_threads_active = m_context->m_thread_count;
    
    for (int i = 0; i < m_context->m_thread_count; ++i)
    {
        m_worker_threads[i] = std::thread(&DLAS::RunWorker, this, i);
    }
    
    // Wait for all threads to complete
    for (auto& thread : m_worker_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    
    m_running = false;
}

void DLAS::Start()
{
    // Start in a new thread
    std::thread thread(&DLAS::Run, this);
    thread.detach();
}

bool DLAS::IsFinished() const
{
    return m_context->m_finished;
}

const raster_picture& DLAS::GetBestSolution() const
{
    return m_context->m_best_pic;
}

void DLAS::SaveState(const std::string& filename) const
{
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    
    FILE* f = fopen(filename.c_str(), "wt+");
    if (!f)
        return;

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
    std::unique_lock<std::mutex> lock(m_context->m_mutex);
    
    FILE* f = fopen(filename.c_str(), "rt");
    if (!f)
        return false;

    unsigned long no_elements;
    unsigned long index;
    long double cost_max;
    int N;
    long double current_cost;

    fscanf(f, "%lu\n", &no_elements);
    fscanf(f, "%lu\n", &index);
    fscanf(f, "%Lf\n", &cost_max);
    fscanf(f, "%d\n", &N);
    fscanf(f, "%Lf\n", &current_cost);

    m_context->m_previous_results_index = index;
    m_context->m_cost_max = cost_max;
    m_context->m_N = N;
    m_context->m_current_cost = current_cost;
    
    m_context->m_previous_results.clear();

    for (size_t i = 0; i < (size_t)no_elements; ++i)
    {
        long double dst = 0;
        fscanf(f, "%Lf\n", &dst);
        m_context->m_previous_results.push_back(dst);
    }
    
    fclose(f);
    return true;
}
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <vector>
#include <deque>
#include <unordered_set>
#include <assert.h>
#include "../raster/Program.h"
#include "../raster/RegisterState.h"
#include "../color/Distance.h"
#include "../cache/LineCache.h"
#include "SpriteManager.h"
#include "../OnOffMap.h"

// Forward declarations
class EvaluationContext;
class Mutator;

// Type definitions
typedef std::vector<unsigned char> color_index_line;
typedef std::vector<unsigned char> line_target;    // target of the pixel f.e. COLBAK

/**
 * Handles execution of raster programs
 */
class Executor {
public:
    Executor();
    ~Executor() = default;
    
    /**
     * Initialize the executor
     */
    void Init(unsigned width, unsigned height, 
              const std::vector<distance_t>* pictureAllErrors[128],
              const screen_line* picture, 
              const OnOffMap* onoff, 
              EvaluationContext* gstate, 
              int solutions, 
              unsigned long long randseed, 
              size_t cache_size,
              Mutator* mutator,
              int thread_id = 0);
    
    /**
     * Register this executor with the evaluation context
     */
    void Start();
    
    /**
     * Run the executor thread
     */
    void Run();
    
    /**
     * Execute a raster program and return the error/distance
     * 
     * @param pic The raster program to execute
     * @param results Array to store line results (must be pre-allocated with height elements)
     * @param dual_role When dual-frame mode is active, indicate which frame is being rendered.
     *                  This enables pair-aware per-pixel selection inside the executor.
     *                  Defaults to DUAL_NONE for single-frame behavior.
     * @return Total error for the program
     */
    enum dual_render_role_t { DUAL_NONE = 0, DUAL_A = 1, DUAL_B = 2 };
    distance_accum_t ExecuteRasterProgram(raster_picture* pic,
                                          const line_cache_result** results,
                                          dual_render_role_t dual_role = DUAL_NONE,
                                          const std::vector<const line_cache_result*>* other_results = nullptr);
    
    /**
     * Find the closest color register for a pixel
     * 
     * @param spriterow Current sprite row
     * @param index Index in the error map
     * @param x X coordinate
     * @param y Y coordinate
     * @param restart_line Set to true if line needs to be restarted
     * @param best_error Output parameter for the error
     * @return The closest color register
     */
    e_target FindClosestColorRegister(sprites_row_memory_t& spriterow, 
                                    int index, int x, int y, 
                                    bool& restart_line, 
                                    distance_t& best_error);
    
    /**
     * Turn off registers according to the on/off map
     * 
     * @param pic The raster program to modify
     */
    void TurnOffRegisters(raster_picture* pic);
    
    /**
     * Execute a single instruction
     * 
     * @param instr The instruction to execute
     * @param sprite_check_x X position for sprite check
     * @param spriterow Sprite row data
     * @param total_line_error Total error accumulator
     */
    void ExecuteInstruction(const SRasterInstruction& instr, 
                           int sprite_check_x, 
                           sprites_row_memory_t& spriterow, 
                           distance_accum_t& total_line_error);
    
    /**
     * Reset the sprite shift start array
     */
    void ResetSpriteShiftStartArray();
    
    /**
     * Start sprite shift
     * 
     * @param mem_reg Memory register index
     */
    void StartSpriteShift(int mem_reg);
    
    /**
     * Store the current register state
     */
    void StoreLineRegs();
    
    /**
     * Restore the previous register state
     */
    void RestoreLineRegs();
    
    /**
     * Capture the current register state
     * 
     * @param rs Register state to fill
     */
    void CaptureRegisterState(register_state& rs) const;
    
    /**
     * Apply a saved register state
     * 
     * @param rs Register state to apply
     */
    void ApplyRegisterState(const register_state& rs);
    
    /**
     * Clear the line caches
     */
    void ClearLineCaches();

    /**
     * Get current memory usage of the executor's cache allocator
     */
    size_t GetCacheMemoryUsage() const;
    
    /**
     * Get the created picture
     */
    const std::vector<color_index_line>& GetCreatedPicture() const { return m_created_picture; }
    
    /**
     * Get the created picture targets
     */
    const std::vector<line_target>& GetCreatedPictureTargets() const { return m_created_picture_targets; }
    
    /**
     * Get the sprites memory
     */
    const sprites_memory_t& GetSpritesMemory() const { return m_sprites_memory; }
    
    /**
     * Update LRU status of a line
     * 
     * @param line_index Index of the line to mark as recently used
     */
    void UpdateLRU(int line_index);
    
    /**
     * Generate a random number
     * 
     * @param range Upper limit of the random number (exclusive)
     * @return Random number between 0 and range-1
     */
    int Random(int range);

    /**
     * Get the thread ID of this executor
     */
    int GetThreadId() const { return m_thread_id; }

private:
    EvaluationContext* m_gstate;
    int m_thread_id;
    
    // LRU tracking
    std::deque<int> m_lru_lines;
    std::unordered_set<int> m_lru_set;
    
    // Register state
    unsigned char m_reg_a, m_reg_x, m_reg_y;
    unsigned char m_mem_regs[E_TARGET_MAX+1]; // +1 for HITCLR
    register_state m_old_reg_state;
    
    // Sprite management
    unsigned char m_sprite_shift_regs[4];
    unsigned char m_sprite_shift_emitted[4];
    unsigned char m_sprite_shift_start_array[256];
    
    // Caching
    linear_allocator* m_linear_allocator_ptr;
    insn_sequence_cache* m_insn_seq_cache_ptr;
    std::vector<line_cache> m_line_caches;
    // Separate caches for dual-aware rendering keyed by 'other' generation snapshot
    std::vector<line_cache> m_line_caches_dual;
    linear_allocator m_thread_local_allocator;
    insn_sequence_cache m_thread_local_cache;
    
    // Output storage
    sprites_memory_t m_sprites_memory;
    std::vector<color_index_line> m_created_picture;
    std::vector<line_target> m_created_picture_targets;
    // Dual-frame: snapshot of the other frame's created picture for pair-aware selection
    std::vector<unsigned char> m_dual_other_pixels; // size width*height when used
    // Dual-frame: transient per-line results pointing into m_created_picture memory
    std::vector<line_cache_result> m_dual_transient_results;
    
    // Parameters
    unsigned m_width;
    unsigned m_height;
    const std::vector<distance_t>* m_picture_all_errors[128];
    const screen_line *m_picture;
    int m_solutions;
    size_t m_cache_size;
    
    // Random number generation
    unsigned long long m_randseed;
    
    // Best solution tracking
    raster_picture m_best_pic;
    double m_best_result;
    
    // OnOff map
    const OnOffMap *m_onoff;
    
    // Mutator instance
    Mutator* m_mutator;

    // Dual-frame render role for current execution call
    dual_render_role_t m_dual_render_role = DUAL_NONE;
    // Dual-frame cached parameters for current execution call
    float m_dual_wl = 0.0f, m_dual_wc = 0.0f;
    float m_dual_Tl = 0.0f, m_dual_Tc = 0.0f;
    int   m_dual_pl = 2, m_dual_pc = 2;
    // Dual-frame: generation counters snapshot for early exit decisions
    unsigned long long m_dual_gen_other_snapshot = 0ULL;
    unsigned long long m_dual_last_other_generation = 0ULL;
    // Fast-access pointers to context pair tables for this call (if available)
    const float* m_pair_Ysum = nullptr;
    const float* m_pair_Usum = nullptr;
    const float* m_pair_Vsum = nullptr;
    const float* m_pair_dY = nullptr;
    const float* m_pair_dC = nullptr;
};

#endif // EXECUTOR_H
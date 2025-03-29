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

// Type definitions
typedef std::vector<unsigned char> color_index_line;
typedef std::vector<unsigned char> line_target;    // target of the pixel f.e. COLBAK

// Forward declarations
class EvaluationContext;

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
    void Init(unsigned width, unsigned height, const distance_t* const* errmap, 
              const screen_line* picture, const OnOffMap* onoff, 
              EvaluationContext* gstate, int solutions, 
              unsigned long long randseed, size_t cache_size, 
              int thread_id = 0);
    
    /**
     * Start execution in a separate thread
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
     * @return Total error for the program
     */
    distance_accum_t ExecuteRasterProgram(raster_picture* pic, 
                                         const line_cache_result** results);
    
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
     * Select a mutation type based on success statistics
     * 
     * @return Selected mutation type
     */
    int SelectMutation();
    
    /**
     * Generate a random number
     * 
     * @param range Upper limit of the random number (exclusive)
     * @return Random number between 0 and range-1
     */
    int Random(int range);

private:
    EvaluationContext* m_gstate;
    int m_thread_id;
    
    // LRU tracking
    std::deque<int> m_lru_lines;
    std::unordered_set<int> m_lru_set;
    
    // Mutation statistics
    int m_mutation_success_count[E_MUTATION_MAX];
    int m_mutation_attempt_count[E_MUTATION_MAX];
    
    // Batch mutation methods
    void BatchMutateLine(raster_line& prog, raster_picture& pic, int count);
    void MutateLine(raster_line& prog, raster_picture& pic);
    void MutateOnce(raster_line& prog, raster_picture& pic);
    void MutateRasterProgram(raster_picture* pic);
    
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
    linear_allocator m_thread_local_allocator;
    insn_sequence_cache m_thread_local_cache;
    
    // Output storage
    sprites_memory_t m_sprites_memory;
    std::vector<color_index_line> m_created_picture;
    std::vector<line_target> m_created_picture_targets;
    
    // Parameters
    unsigned m_width;
    unsigned m_height;
    const distance_t *const *m_picture_all_errors;
    const screen_line *m_picture;
    int m_currently_mutated_y;
    int m_solutions;
    size_t m_cache_size;
    
    // Random number generation
    unsigned long long m_randseed;
    
    // Best solution tracking
    raster_picture m_best_pic;
    double m_best_result;
    
    // Current mutation counts
    int m_current_mutations[E_MUTATION_MAX];
    
    // OnOff map
    const OnOffMap *m_onoff;
};

#endif // EXECUTOR_H
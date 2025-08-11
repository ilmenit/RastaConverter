#ifndef RASTER_MUTATOR_H
#define RASTER_MUTATOR_H

#include "Mutator.h"
#include "../raster/Program.h"
#include <vector>

// Forward declarations
class EvaluationContext;

/**
 * Implements mutations for raster programs
 */
class RasterMutator : public Mutator {
public:
    /**
     * Constructor
     * 
     * @param context Global evaluation context
     * @param thread_id Thread ID for region-based mutation 
     */
    RasterMutator(EvaluationContext* context, int thread_id = 0);
    
    /**
     * Initialize with a random seed
     * 
     * @param seed Random seed
     */
    void Init(unsigned long long seed) override;
    
    /**
     * Mutate a program
     * 
     * @param program The program to mutate
     */
    void MutateProgram(raster_picture* program) override;
    
    /**
     * Get mutation statistics
     * 
     * @return Statistics about attempted mutations
     */
    const MutationStats& GetStats() const override { return m_stats; }
    
    /**
     * Reset mutation statistics
     */
    void ResetStats() override;
    
    /**
     * Get currently mutated line index
     * 
     * @return Currently mutated line
     */
    int GetCurrentlyMutatedLine() const override { return m_currently_mutated_y; }
    
    /**
     * Set the currently mutated line index
     * 
     * @param lineIndex New line index
     */
    void SetCurrentlyMutatedLine(int lineIndex) override { m_currently_mutated_y = lineIndex; }
    
    /**
     * Get current mutations count for each type
     * 
     * @return Array of mutation counts
     */
    const int* GetCurrentMutations() const override { return m_current_mutations; }

    // Indicate which frame is currently being mutated in dual mode
    void SetDualFrameRole(bool isFrameB) override { m_is_mutating_B = isFrameB; }

    // Instrumentation for UI: expose whether last mutation used complementary or seed-add
    bool GetAndResetUsedComplementaryPick() override { bool t = m_used_complementary_pick; m_used_complementary_pick = false; return t; }
    bool GetAndResetUsedSeedAdd() override { bool t = m_used_seed_add; m_used_seed_add = false; return t; }

private:
    // Helper methods
    void MutateLine(raster_line& prog, raster_picture& pic);
    void MutateOnce(raster_line& prog, raster_picture& pic);
    void BatchMutateLine(raster_line& prog, raster_picture& pic, int count);
    int SelectMutation();
    int Random(int range);
    // Dual-mode helpers
    bool TryComplementaryPick(int y, int x, unsigned char& outValue);
    int ComputePixelXForInstructionIndex(const raster_line& prog, int insnIndex);

private:
    unsigned m_width;
    unsigned m_height;
    const screen_line* m_picture;
    EvaluationContext* m_gstate;
    int m_thread_id;
    int m_currently_mutated_y;
    bool m_is_mutating_B = false;
    // UI instrumentation flags for per-mutation feedback
    bool m_used_complementary_pick = false;
    bool m_used_seed_add = false;
    
    // Mutation statistics
    MutationStats m_stats;
    int m_current_mutations[E_MUTATION_MAX];
    
    // Random number generation
    unsigned long long m_randseed;
};

#endif // RASTER_MUTATOR_H
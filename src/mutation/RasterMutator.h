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

private:
    // Helper methods
    void MutateLine(raster_line& prog, raster_picture& pic);
    void MutateOnce(raster_line& prog, raster_picture& pic);
    void BatchMutateLine(raster_line& prog, raster_picture& pic, int count);
    int SelectMutation();
    int Random(int range);

private:
    unsigned m_width;
    unsigned m_height;
    const screen_line* m_picture;
    EvaluationContext* m_gstate;
    int m_thread_id;
    int m_currently_mutated_y;
    
    // Mutation statistics
    MutationStats m_stats;
    int m_current_mutations[E_MUTATION_MAX];
    
    // Random number generation
    unsigned long long m_randseed;
};

#endif // RASTER_MUTATOR_H
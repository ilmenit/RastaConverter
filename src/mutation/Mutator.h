#ifndef MUTATOR_H
#define MUTATOR_H

#include "../raster/Program.h"

/**
 * Statistics about attempted mutations 
 */
struct MutationStats {
    int success_count[E_MUTATION_MAX];
    int attempt_count[E_MUTATION_MAX];
    
    MutationStats() {
        memset(success_count, 0, sizeof(success_count));
        memset(attempt_count, 0, sizeof(attempt_count));
    }
};

/**
 * Interface for mutation strategies
 */
class Mutator {
public:
    virtual ~Mutator() = default;
    
    /**
     * Initialize mutator with a random seed
     * 
     * @param seed Random seed to use
     */
    virtual void Init(unsigned long long seed) = 0;
    
    /**
     * Mutate a single program
     * 
     * @param program The program to mutate
     */
    virtual void MutateProgram(raster_picture* program) = 0;
    
    /**
     * Get mutation statistics
     * 
     * @return Statistics about attempted mutations
     */
    virtual const MutationStats& GetStats() const = 0;
    
    /**
     * Reset mutation statistics
     */
    virtual void ResetStats() = 0;
    
    /**
     * Get currently mutated line index
     * 
     * @return Currently mutated line
     */
    virtual int GetCurrentlyMutatedLine() const = 0;
    
    /**
     * Set the currently mutated line index
     * 
     * @param lineIndex New line index
     */
    virtual void SetCurrentlyMutatedLine(int lineIndex) = 0;
    
    /**
     * Get current mutations count for each type
     * 
     * @return Array of mutation counts
     */
    virtual const int* GetCurrentMutations() const = 0;
};

#endif // MUTATOR_H
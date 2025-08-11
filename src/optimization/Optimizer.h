#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <string>
#include "../raster/Program.h"
#include "../execution/Executor.h"
#include "../mutation/Mutator.h"

// Forward declarations
class EvaluationContext;

/**
 * Abstract base class for all optimization algorithms
 */
class Optimizer {
public:
    virtual ~Optimizer() = default;
    
    // Initialize with initial solution
    virtual void Initialize(const raster_picture& initialSolution) = 0;
    
    // Run optimization process
    virtual void Run() = 0;
    
    // Start optimization in separate thread(s)
    virtual void Start() = 0;

    // Request optimization to stop
    virtual void Stop() = 0;
    
    // Check if optimization is complete
    virtual bool IsFinished() const = 0;
    
    // Get best solution found
    virtual const raster_picture& GetBestSolution() const = 0;
    
    // Save current state
    virtual void SaveState(const std::string& filename) const = 0;
    
    // Load previous state
    virtual bool LoadState(const std::string& filename) = 0;

    // Optional: name of the optimizer
    virtual const char* Name() const = 0;
};

#endif // OPTIMIZER_H

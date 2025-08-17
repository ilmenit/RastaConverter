#ifndef CORE_EVALUATOR_H
#define CORE_EVALUATOR_H

#include <vector>
#include "EvaluationContext.h"
#include "../execution/Executor.h"

struct SingleEvalResult {
    double cost = 1e300;
    std::vector<const line_cache_result*> lineResults; // size = H
    sprites_memory_t spritesMemory{};
};

struct DualEvalResult {
    double cost = 1e300; // pair objective
    std::vector<const line_cache_result*> lineResultsA;
    std::vector<const line_cache_result*> lineResultsB;
    sprites_memory_t spritesMemoryA{};
    sprites_memory_t spritesMemoryB{};
};

// Encapsulates all evaluation logic (single and dual) using Executor
class CoreEvaluator {
public:
    explicit CoreEvaluator(EvaluationContext* ctx) : m_ctx(ctx) {}

    // Render and score single frame
    SingleEvalResult evaluateSingle(Executor& exec, raster_picture& pic);
    
    // Render and score single frame using pre-allocated result (no heap allocations)
    void evaluateSingle(Executor& exec, raster_picture& pic, SingleEvalResult& result);

    // Render and score dual frame with coordinate-descent step
    // mutateB: when true, B is the mutated frame; A is fixed and rendered plain first
    DualEvalResult evaluateDual(Executor& exec, raster_picture& picA, raster_picture& picB, bool mutateB);

private:
    EvaluationContext* m_ctx;
};

#endif // CORE_EVALUATOR_H



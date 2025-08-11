#ifndef OPTIMIZATION_RUNNER_H
#define OPTIMIZATION_RUNNER_H

#include <thread>
#include <memory>
#include <vector>
#include "EvaluationContext.h"
#include "CoreEvaluator.h"
#include "AcceptancePolicy.h"
#include "../execution/Executor.h"
#include "../mutation/RasterMutator.h"

// Orchestrates optimization using a CoreEvaluator and an AcceptancePolicy
class OptimizationRunner {
public:
    OptimizationRunner(EvaluationContext* ctx,
                       std::unique_ptr<AcceptancePolicy> policy,
                       Mutator* referenceMutator /* seed source only */);

    // Blocking run; internally starts worker threads via EvaluationContext
    void run();

private:
    void worker(int threadId);

private:
    EvaluationContext* m_ctx;
    std::unique_ptr<AcceptancePolicy> m_policy;
    Mutator* m_reference_mutator; // not owned
    std::atomic<bool> m_running{false};
    CoreEvaluator m_evaluator;
};

#endif // OPTIMIZATION_RUNNER_H



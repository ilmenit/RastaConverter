#ifndef LAHC_H
#define LAHC_H

#include "Optimizer.h"
#include "EvaluationContext.h"
#include <thread>

// Classic Late Acceptance Hill Climbing optimizer
class LAHC : public Optimizer {
public:
    LAHC(EvaluationContext* context, Executor* executor, Mutator* mutator, int historyLength);
    ~LAHC();

    void Initialize(const raster_picture& initialSolution) override;
    void Run() override;
    void Start() override;
    void Stop() override;
    bool IsFinished() const override;
    const raster_picture& GetBestSolution() const override;
    void SaveState(const std::string& filename) const override;
    bool LoadState(const std::string& filename) override;
    const char* Name() const override { return "LAHC"; }

private:
    void RunWorker(int threadId);
    double EvaluateInitialSolution();

private:
    EvaluationContext* m_context;
    Executor* m_executor;
    Mutator* m_mutator;
    bool m_running{false};
    std::thread m_control_thread;
    int m_history_length{1};
};

#endif // LAHC_H



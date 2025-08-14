#ifndef ACCEPTANCE_POLICY_H
#define ACCEPTANCE_POLICY_H

#include <vector>

class EvaluationContext;

// Base acceptance policy interface used by OptimizationRunner
class AcceptancePolicy {
public:
    virtual ~AcceptancePolicy() = default;

    // Initialize policy state from the context after the initial score is known
    virtual void init(EvaluationContext& ctx) = 0;

    // Optionally receive the initial score to seed internal state
    virtual void onInitialScore(double /*score*/, EvaluationContext& /*ctx*/) {}

    // Decide whether to accept candidateCost given previousCost (thread-local current)
    // May update EvaluationContext to keep legacy fields in sync for UI/save-state
    virtual bool accept(double previousCost, double candidateCost, EvaluationContext& ctx) = 0;

    // Optional callback after an acceptance
    virtual void onAccepted(double /*candidateCost*/, EvaluationContext& /*ctx*/) {}

    // Optional callback invoked once per iteration to update history ring, etc.
    virtual void postIterationUpdate(EvaluationContext& /*ctx*/) {}

    // Optional callback: invoked when staged dual-mode flips focus A<->B
    // Default no-op; policies may relax history to allow uphill moves after switch
    virtual void onStageSwitch(double /*currentCost*/, EvaluationContext& /*ctx*/, bool /*focusB*/) {}
};

// Dynamic Late Acceptance Search policy mirroring legacy DLAS logic
class DLASPolicy final : public AcceptancePolicy {
public:
    DLASPolicy() = default;
    void init(EvaluationContext& ctx) override;
    void onInitialScore(double score, EvaluationContext& ctx) override;
    bool accept(double previousCost, double candidateCost, EvaluationContext& ctx) override;
    void onAccepted(double candidateCost, EvaluationContext& ctx) override;
    void postIterationUpdate(EvaluationContext& ctx) override;
    void onStageSwitch(double currentCost, EvaluationContext& ctx, bool focusB) override;

private:
    // DLAS state
    double m_costMax = 1e300;
    std::vector<double> m_history; // circular buffer
    size_t m_historyIndex = 0;
    int m_N = 0;                   // count of entries equal to costMax
    double m_currentCost = 1e300;  // last accepted local cost
};

// Classic Late Acceptance Hill Climbing policy
class LAHCPolicy final : public AcceptancePolicy {
public:
    explicit LAHCPolicy(int historyLength) : m_historyLength(historyLength > 0 ? historyLength : 1) {}
    void init(EvaluationContext& ctx) override;
    void onInitialScore(double score, EvaluationContext& ctx) override;
    bool accept(double previousCost, double candidateCost, EvaluationContext& ctx) override;
    void onAccepted(double candidateCost, EvaluationContext& ctx) override;
    void postIterationUpdate(EvaluationContext& ctx) override;
    void onStageSwitch(double currentCost, EvaluationContext& ctx, bool focusB) override;

private:
    int m_historyLength;
    std::vector<double> m_history; // length L
    size_t m_index = 0;            // rotating index
    double m_currentCost = 1e300;  // last accepted local cost
};

// Skeleton Simulated Annealing policy (compiles; simple greedy for now)
class SimAnnealingPolicy final : public AcceptancePolicy {
public:
    SimAnnealingPolicy() = default;
    void init(EvaluationContext& /*ctx*/) override {}
    bool accept(double previousCost, double candidateCost, EvaluationContext& /*ctx*/) override {
        // Skeleton: accept if not worse (no probabilistic uphill moves yet)
        return candidateCost <= previousCost;
    }
};

#endif // ACCEPTANCE_POLICY_H



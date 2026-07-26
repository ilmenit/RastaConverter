#ifndef OPTIMIZERSTATE_H
#define OPTIMIZERSTATE_H

#include <cstddef>
#include <utility>
#include <vector>

enum class OptimizerKind
{
	LAHC,
	DLAS,
};

struct ImprovementMagnitudeCredit
{
	double delta = 0.0;
	double perOccurrence = 0.0;
	bool improving = false;
};

// Attribute one accepted current-cost improvement across every mutation
// occurrence applied to its candidate. Equal per-occurrence credit makes the
// per-operator totals sum back to the candidate's raw objective decrease.
inline ImprovementMagnitudeCredit CalculateImprovementMagnitudeCredit(
	double previousCost, double candidateCost, unsigned long long appliedOccurrences)
{
	ImprovementMagnitudeCredit credit;
	if (appliedOccurrences == 0 || !(candidateCost < previousCost))
		return credit;
	credit.delta = previousCost - candidateCost;
	credit.perOccurrence = credit.delta / static_cast<double>(appliedOccurrences);
	credit.improving = true;
	return credit;
}

struct OptimizerState
{
	std::vector<double> history;
	std::size_t historyIndex = 0;
	double currentCost = 0.0;
	double costMax = 0.0;
	int maxCount = 0;
	bool initialized = false;

	void Initialize(double initialCost, std::size_t historySize);
	bool Apply(OptimizerKind kind, double candidateCost, double drift = 0.0);
};

// A dual-frame search state is one accepted A/B pair plus one acceptance
// history whose currentCost describes that exact pair. Only the frame selected
// for mutation is replaced after acceptance; switching focus never changes the
// pair or invalidates its cost.
template <typename Frame>
struct DualOptimizerState
{
	Frame currentA;
	Frame currentB;
	OptimizerState optimizer;

	void Initialize(Frame frameA, Frame frameB, double initialCost,
		std::size_t historySize)
	{
		currentA = std::move(frameA);
		currentB = std::move(frameB);
		optimizer.Initialize(initialCost, historySize);
	}

	Frame& Current(bool mutateB)
	{
		return mutateB ? currentB : currentA;
	}

	const Frame& Current(bool mutateB) const
	{
		return mutateB ? currentB : currentA;
	}

	const Frame& Other(bool mutateB) const
	{
		return mutateB ? currentA : currentB;
	}

	bool Apply(OptimizerKind kind, double candidateCost, bool mutateB,
		Frame candidate, double drift = 0.0)
	{
		const bool accepted = ApplyInPlace(kind, candidateCost, drift);
		if (accepted)
			Current(mutateB) = std::move(candidate);
		return accepted;
	}

	// Use when Current(mutateB) was transactionally mutated in place. The
	// caller must restore the frame if the cost/history decision rejects it.
	bool ApplyInPlace(OptimizerKind kind, double candidateCost, double drift = 0.0)
	{
		return optimizer.Apply(kind, candidateCost, drift);
	}
};

#endif

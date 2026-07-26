#include "OptimizerState.h"

#include <algorithm>

void OptimizerState::Initialize(double initialCost, std::size_t historySize)
{
	if (historySize == 0)
		historySize = 1;

	history.assign(historySize, initialCost);
	historyIndex = 0;
	currentCost = initialCost;
	costMax = initialCost;
	maxCount = static_cast<int>(historySize);
	initialized = true;
}

bool OptimizerState::Apply(OptimizerKind kind, double candidateCost, double drift)
{
	if (!initialized || history.empty())
		Initialize(candidateCost, history.empty() ? 1 : history.size());

	const std::size_t index = historyIndex % history.size();
	historyIndex = (index + 1) % history.size();
	const double previousCost = currentCost;

	if (kind == OptimizerKind::LAHC)
	{
		const bool accepted = candidateCost <= currentCost + drift
			|| candidateCost <= history[index] + drift;
		if (accepted)
			currentCost = candidateCost;

		// The history receives the cost of the current solution after the
		// acceptance decision, as specified by LAHC.
		history[index] = currentCost;
		return accepted;
	}

	const bool accepted = candidateCost <= currentCost + drift
		|| candidateCost < costMax + drift;
	if (accepted)
		currentCost = candidateCost;

	const double oldValue = history[index];
	const double current = currentCost;
	if (current > oldValue)
	{
		history[index] = current;
		if (current > costMax)
		{
			costMax = current;
			maxCount = 1;
		}
		else if (current == costMax)
		{
			if (oldValue != costMax)
				++maxCount;
		}
		else if (oldValue == costMax)
		{
			--maxCount;
		}
	}
	else if (current < oldValue && current < previousCost)
	{
		if (oldValue == costMax)
			--maxCount;
		history[index] = current;
	}

	if (maxCount <= 0)
	{
		costMax = *std::max_element(history.begin(), history.end());
		maxCount = static_cast<int>(std::count(history.begin(), history.end(), costMax));
	}

	return accepted;
}

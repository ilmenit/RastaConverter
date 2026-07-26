#include "OptimizerState.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

void TestLahcAcceptedWorseCandidateBecomesCurrent()
{
	OptimizerState state;
	state.Initialize(10.0, 3);

	Require(state.Apply(OptimizerKind::LAHC, 8.0), "LAHC should accept an improvement");
	Require(state.currentCost == 8.0, "accepted improvement must become current");
	Require(state.Apply(OptimizerKind::LAHC, 9.0), "LAHC should accept against older history");
	Require(state.currentCost == 9.0, "accepted worsening candidate must become current");
	Require(!state.Apply(OptimizerKind::LAHC, 11.0), "LAHC should reject above historical cost");
	Require(state.currentCost == 9.0, "rejected candidate must not replace current");
}

void TestLahcHistoryStoresCurrentAfterRejection()
{
	OptimizerState state;
	state.Initialize(10.0, 2);
	Require(state.Apply(OptimizerKind::LAHC, 8.0), "initial improvement should be accepted");
	Require(!state.Apply(OptimizerKind::LAHC, 12.0), "worse candidate should be rejected");
	Require(state.history[1] == 8.0, "history must receive retained current cost");
}

void TestLahcAlwaysAcceptsCurrentImprovement()
{
	OptimizerState state;
	state.Initialize(10.0, 3);
	Require(state.Apply(OptimizerKind::LAHC, 8.0), "first move should be accepted");
	Require(state.Apply(OptimizerKind::LAHC, 9.0), "history should admit worsening move");
	Require(state.Apply(OptimizerKind::LAHC, 9.5), "history should admit another worsening move");
	Require(state.Apply(OptimizerKind::LAHC, 9.0),
		"an improvement over current must be accepted even above the history slot");
	Require(state.currentCost == 9.0, "current improvement must become current");
}

void TestDlasAcceptedCandidateBecomesCurrent()
{
	OptimizerState state;
	state.Initialize(10.0, 3);
	Require(state.Apply(OptimizerKind::DLAS, 8.0), "DLAS should accept improvement");
	Require(state.currentCost == 8.0, "DLAS improvement must become current");
	Require(state.Apply(OptimizerKind::DLAS, 9.0), "DLAS should accept below list maximum");
	Require(state.currentCost == 9.0, "DLAS accepted worsening candidate must become current");
}

void TestDriftCanAdmitCandidate()
{
	OptimizerState state;
	state.Initialize(10.0, 1);
	Require(state.Apply(OptimizerKind::LAHC, 10.25, 0.5), "drift should relax LAHC threshold");
	Require(state.currentCost == 10.25, "drift-accepted candidate must become current");
}

void TestDualAcceptedWorseFrameSurvivesFocusSwitch()
{
	DualOptimizerState<int> state;
	state.Initialize(10, 20, 10.0, 3);

	Require(state.Apply(OptimizerKind::LAHC, 8.0, false, 11),
		"dual A improvement should be accepted");
	Require(state.currentA == 11 && state.currentB == 20,
		"accepting A must retain B from the same pair");
	Require(state.Apply(OptimizerKind::LAHC, 9.0, true, 21),
		"dual B worsening move should be accepted against history");
	Require(state.currentA == 11 && state.currentB == 21,
		"accepted B must remain paired with the current A");

	// Merely switching which frame is mutated must not reset either program or
	// the cost describing their pair.
	Require(state.Current(false) == 11 && state.Other(false) == 21,
		"focus switch back to A must preserve the accepted pair");
	Require(state.optimizer.currentCost == 9.0,
		"dual current cost must continue to describe the accepted A/B pair");
}

void TestDualRejectedFrameDoesNotReplaceCurrentPair()
{
	DualOptimizerState<int> state;
	state.Initialize(10, 20, 10.0, 1);
	Require(!state.Apply(OptimizerKind::DLAS, 12.0, true, 99),
		"dual rejected B candidate must be rejected");
	Require(state.currentA == 10 && state.currentB == 20,
		"dual rejection must preserve both members of the current pair");
}

void TestDualInPlaceAcceptanceUpdatesOnlyOptimizerState()
{
	DualOptimizerState<int> state;
	state.Initialize(10, 20, 10.0, 1);
	state.currentA = 11; // Transactional mutation is already applied by caller.
	Require(state.ApplyInPlace(OptimizerKind::LAHC, 8.0),
		"dual in-place improvement should be accepted");
	Require(state.currentA == 11 && state.currentB == 20,
		"in-place acceptance must not copy or replace either frame");
	Require(state.optimizer.currentCost == 8.0,
		"in-place acceptance must update the pair cost");

	state.currentB = 99; // Caller will restore this mutation after rejection.
	Require(!state.ApplyInPlace(OptimizerKind::LAHC, 12.0),
		"dual in-place worse candidate should be rejected");
	Require(state.optimizer.currentCost == 8.0,
		"in-place rejection must retain the accepted pair cost");
}

void TestImprovementMagnitudeCredit()
{
	const ImprovementMagnitudeCredit credit =
		CalculateImprovementMagnitudeCredit(100.0, 88.0, 3);
	Require(credit.improving, "lower accepted cost must produce improvement credit");
	Require(credit.delta == 12.0, "credit must retain the raw objective decrease");
	Require(credit.perOccurrence == 4.0,
		"credit must divide the decrease equally across applied occurrences");

	const ImprovementMagnitudeCredit worse =
		CalculateImprovementMagnitudeCredit(100.0, 101.0, 2);
	Require(!worse.improving && worse.delta == 0.0,
		"accepted worsening candidates must not receive improvement credit");
	const ImprovementMagnitudeCredit empty =
		CalculateImprovementMagnitudeCredit(100.0, 90.0, 0);
	Require(!empty.improving,
		"candidates without applied mutation occurrences must not receive credit");
}

}

int main()
{
	TestLahcAcceptedWorseCandidateBecomesCurrent();
	TestLahcHistoryStoresCurrentAfterRejection();
	TestLahcAlwaysAcceptsCurrentImprovement();
	TestDlasAcceptedCandidateBecomesCurrent();
	TestDriftCanAdmitCandidate();
	TestDualAcceptedWorseFrameSurvivesFocusSwitch();
	TestDualRejectedFrameDoesNotReplaceCurrentPair();
	TestDualInPlaceAcceptanceUpdatesOnlyOptimizerState();
	TestImprovementMagnitudeCredit();
	std::cout << "OptimizerState tests passed\n";
	return 0;
}

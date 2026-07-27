#include "VisualObjective.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

rgb Gray(unsigned char value)
{
	rgb color{};
	color.r = color.g = color.b = value;
	return color;
}
}

int main()
{
	rgb palette[128]{};
	palette[0] = Gray(0);
	palette[1] = Gray(255);
	palette[2] = Gray(128);

	std::vector<screen_line> reference(3);
	for (screen_line& row : reference)
	{
		row.Resize(4);
		for (size_t x = 0; x < row.size(); ++x)
			row[x] = Gray(128);
	}

	unsigned char exact_storage[3][4] = {};
	unsigned char mixed_storage[3][4] = {};
	unsigned char dark_storage[3][4] = {};
	const unsigned char* exact[3];
	const unsigned char* mixed[3];
	const unsigned char* dark[3];
	for (int y = 0; y < 3; ++y)
	{
		exact[y] = exact_storage[y];
		mixed[y] = mixed_storage[y];
		dark[y] = dark_storage[y];
		for (int x = 0; x < 4; ++x)
		{
			exact_storage[y][x] = 2;
			mixed_storage[y][x] = static_cast<unsigned char>((x + y) & 1);
			dark_storage[y][x] = 0;
		}
	}

	DisplayFilteredObjective objective;
	objective.Init(4, 3, reference.data(), palette);
	Require(objective.IsInitialized(), "objective must initialize");
	Require(objective.DirectMeanScore(exact) == 0,
		"an exact rendered reference must have zero direct mean error");
	Require(objective.DirectMeanScore(dark) > objective.DirectMeanScore(exact),
		"direct mean error must penalize an incorrect unfiltered rendering");
	// The four structural terms these tests used to cover - filtered, composite,
	// edge and worst-region - were removed along with the objectives that added
	// them to the score. What survives is the direct source-referenced readout.

	std::vector<screen_line> dual_reference(1);
	dual_reference[0].Resize(2);
	dual_reference[0][0] = dual_reference[0][1] = Gray(128);
	unsigned char identical_storage[1][2] = {{2, 2}};
	unsigned char alternating_a_storage[1][2] = {{0, 1}};
	unsigned char alternating_b_storage[1][2] = {{1, 0}};
	const unsigned char* identical_rows[1] = {identical_storage[0]};
	const unsigned char* alternating_a[1] = {alternating_a_storage[0]};
	const unsigned char* alternating_b[1] = {alternating_b_storage[0]};
	DualFrameObjective dual;
	dual.Init(2, 1, dual_reference.data(), palette, 0.2, 0.1);
	const DualFrameScore identical = dual.Score(identical_rows, identical_rows);
	Require(identical.visual == 0.0 && identical.flicker == 0.0
		&& identical.total == 0.0,
		"identical exact frames must equal the single-frame reference objective");
	const DualFrameScore alternating = dual.Score(alternating_a, alternating_b);
	const DualFrameScore swapped = dual.Score(alternating_b, alternating_a);
	Require(alternating.visual < dual.Score(identical_rows, alternating_a).visual,
		"paired black/white targets must receive temporal-combination credit");
	Require(alternating.flicker > 0.0 && alternating.total > alternating.visual,
		"the paired objective must expose an explicit nonzero flicker penalty");
	Require(alternating.visual == swapped.visual
		&& alternating.flicker == swapped.flicker
		&& alternating.total == swapped.total,
		"the paired objective must be invariant to A/B frame order");

	std::cout << "VisualObjectiveTests passed\n";
	return 0;
}

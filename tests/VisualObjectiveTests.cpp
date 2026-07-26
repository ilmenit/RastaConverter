#include "VisualObjective.h"

#include <cmath>
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
	Require(objective.Score(exact) == 0, "an exact rendered reference must score zero");
	Require(objective.Score(mixed) < objective.Score(dark),
		"display filtering must credit a local black/white mixture over solid black");
	const distance_accum_t spatial = objective.Score(mixed);
	Require(objective.CompositeScore(1234, mixed, 0.0) == 1234,
		"zero spatial weight must preserve direct source scoring exactly");
	Require(objective.CompositeScore(1234, mixed, 0.25) ==
		1234 + static_cast<distance_accum_t>(std::llround(0.25 * spatial)),
		"composite scoring must add the declared weighted spatial term");
	Require(objective.EdgeScore(exact) == 0,
		"an exact rendered reference must have zero edge error");
	Require(objective.EdgeScore(mixed) > objective.EdgeScore(dark),
		"the edge term must penalize artificial high-frequency structure");
	const distance_accum_t edge = objective.EdgeScore(mixed);
	Require(objective.EdgeCompositeScore(1234, mixed, 0.25) ==
		1234 + static_cast<distance_accum_t>(std::llround(0.25 * edge)),
		"edge composite scoring must add the declared weighted gradient term");
	Require(objective.RegionScore(exact) == 0,
		"an exact rendered reference must have zero worst-region error");
	const distance_accum_t region = objective.RegionScore(dark);
	Require(region > 0, "a uniformly wrong rendering must have worst-region error");
	Require(objective.RegionCompositeScore(1234, dark, 0.25) ==
		1234 + static_cast<distance_accum_t>(std::llround(0.25 * region)),
		"region composite scoring must add the declared weighted CVaR term");

	std::vector<screen_line> local_reference(16);
	unsigned char local_storage[16][16] = {};
	const unsigned char* local_rows[16];
	for (int y = 0; y < 16; ++y)
	{
		local_reference[y].Resize(16);
		local_rows[y] = local_storage[y];
		for (int x = 0; x < 16; ++x)
			local_reference[y][x] = Gray(0);
	}
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			local_storage[y][x] = 1;
	DisplayFilteredObjective local_objective;
	local_objective.Init(16, 16, local_reference.data(), palette);
	Require(local_objective.RegionScore(local_rows) > 0,
		"one bad 8x8 region must be selected by the bounded tail");

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

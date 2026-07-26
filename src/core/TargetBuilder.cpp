#include "TargetBuilder.h"

#include <algorithm>

namespace rasta {

namespace {

/* 8x8 threshold map */
const unsigned char kThresholdMap[8 * 8] = {
	 0, 48, 12, 60,  3, 51, 15, 63,
	32, 16, 44, 28, 35, 19, 47, 31,
	 8, 56,  4, 52, 11, 59,  7, 55,
	40, 24, 36, 20, 43, 27, 39, 23,
	 2, 50, 14, 62,  1, 49, 13, 61,
	34, 18, 46, 30, 33, 17, 45, 29,
	10, 58,  6, 54,  9, 57,  5, 53,
	42, 26, 38, 22, 41, 25, 37, 21};

// Luminance per palette entry, recomputed from the palette in use so a
// different .act file sorts correctly.
struct LumaTable {
	unsigned values[128];

	void Refresh()
	{
		for (unsigned c = 0; c < 128; ++c) {
			values[c] = atari_palette[c].r * 299u + atari_palette[c].g * 587u
				+ atari_palette[c].b * 114u;
		}
	}
};

} // namespace

const unsigned char* KnollThresholdMap()
{
	return kThresholdMap;
}

MixingPlan DeviseBestMixingPlan(rgb color, const DitherParams& params,
	const JitterFn& jitter)
{
	MixingPlan result = {{0}};
	const double error_multiplier = params.strength / 100.0;
	const rgb src = color;
	rgb_error accumulated;
	accumulated.zero();

	for (unsigned c = 0; c < 64; ++c) {
		const double jitter_r = jitter ? (1.0 + jitter(params.randomness)) : 1.0;
		const double jitter_g = jitter ? (1.0 + jitter(params.randomness)) : 1.0;
		const double jitter_b = jitter ? (1.0 + jitter(params.randomness)) : 1.0;

		rgb_error temp;
		temp.r = src.r + accumulated.r * error_multiplier * jitter_r;
		temp.g = src.g + accumulated.g * error_multiplier * jitter_g;
		temp.b = src.b + accumulated.b * error_multiplier * jitter_b;

		if (temp.r < 0) temp.r = 0; else if (temp.r > 255) temp.r = 255;
		if (temp.g < 0) temp.g = 0; else if (temp.g > 255) temp.g = 255;
		if (temp.b < 0) temp.b = 0; else if (temp.b > 255) temp.b = 255;

		double least_penalty = 1e99;
		unsigned chosen = c % 128;
		rgb candidate;
		candidate.r = static_cast<unsigned char>(temp.r);
		candidate.g = static_cast<unsigned char>(temp.g);
		candidate.b = static_cast<unsigned char>(temp.b);
		candidate.a = 0;
		for (unsigned index = 0; index < 128; ++index) {
			const double penalty = distance_function(atari_palette[index], candidate);
			if (penalty < least_penalty) {
				least_penalty = penalty;
				chosen = index;
			}
		}

		result.colors[c] = chosen;
		const rgb picked = atari_palette[chosen];
		accumulated.r += src.r - picked.r;
		accumulated.g += src.g - picked.g;
		accumulated.b += src.b - picked.b;
	}

	// Sort by luminance so the threshold map selects a smooth ramp.
	LumaTable luma;
	luma.Refresh();
	std::sort(result.colors, result.colors + 64,
		[&luma](unsigned a, unsigned b) { return luma.values[a] < luma.values[b]; });
	return result;
}

} // namespace rasta

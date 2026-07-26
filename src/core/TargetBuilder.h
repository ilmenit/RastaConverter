#ifndef TARGETBUILDER_H
#define TARGETBUILDER_H

// Quantization and error-diffusion dithering of the target picture.
//
// This was lifted out of RastaConverter::OtherDithering so the Setup screen's
// live preview and the real conversion run the *same* code rather than two
// implementations that drift apart (live_ui_design.md §7.6). It is a header
// template so both callers share one body while keeping their own row types
// (`screen_line` in the converter, a plain vector in the preview).
//
// Knoll dithering's core - the ordered threshold map and the per-colour mixing
// plan - lives here too, so the preview can compute the genuine article when
// the user asks for it rather than only ever approximating it. The converter
// keeps its own parallel driver and progress reporting around that core.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "rgb.h"
#include "TargetPicture.h"

namespace rasta {

struct DitherParams {
	e_dither_type type = E_DITHER_NONE;
	double strength = 1.0;
	double randomness = 0.0;
};

// Signature of the jitter source. Takes the configured randomness, returns a
// value in roughly [-randomness, +randomness]. Passed in so the converter can
// keep using the global RNG while the preview stays on its own stream and does
// not perturb a reproducible run.
using JitterFn = std::function<double(double)>;

namespace detail {

// Accumulated per-pixel diffusion error, sized width+1 to match the original
// implementation's addressing.
using ErrorMap = std::vector<std::vector<rgb_error>>;

inline void ClearErrorMap(ErrorMap& map, int width, int height)
{
	if (static_cast<int>(map.size()) != height) {
		map.assign(height, std::vector<rgb_error>(width + 1));
	}
	for (int y = 0; y < height; ++y) {
		if (static_cast<int>(map[y].size()) < width + 1)
			map[y].resize(width + 1);
		for (int x = 0; x < width; ++x)
			map[y][x].zero();
	}
}

inline void DiffuseError(ErrorMap& map, int width, int height, int x, int y,
	double quant_error, double e_r, double e_g, double e_b,
	const DitherParams& params, const JitterFn& jitter)
{
	if (!(x >= 0 && x < width && y >= 0 && y < height))
		return;

	const double jitter_r = jitter ? (1.0 + jitter(params.randomness)) : 1.0;
	const double jitter_g = jitter ? (1.0 + jitter(params.randomness)) : 1.0;
	const double jitter_b = jitter ? (1.0 + jitter(params.randomness)) : 1.0;

	rgb_error p = map[y][x];
	p.r += e_r * quant_error * params.strength * jitter_r;
	p.g += e_g * quant_error * params.strength * jitter_g;
	p.b += e_b * quant_error * params.strength * jitter_b;

	// Each channel clamps itself. The original clamped blue by testing and
	// writing green, which silently left blue unbounded and corrupted green
	// whenever blue saturated.
	if (p.r > 255) p.r = 255; else if (p.r < 0) p.r = 0;
	if (p.g > 255) p.g = 255; else if (p.g < 0) p.g = 0;
	if (p.b > 255) p.b = 255; else if (p.b < 0) p.b = 0;

	map[y][x] = p;
}

} // namespace detail

// Quantizes `source` to the loaded Atari palette, optionally dithering, and
// writes the result into `out` (which the caller has sized to h rows of w).
// Every palette index used is inserted into `used`.
//
// `on_row` is invoked after each completed row with its y coordinate; return
// false from it to cancel, which makes the whole call return true. Passing an
// empty function means "never cancel".
//
// Returns true if cancelled, matching the converter's existing convention.
template <typename SourceRows, typename OutRows>
bool BuildQuantizedTarget(const SourceRows& source, int width, int height,
	const DitherParams& params, OutRows& out, std::set<unsigned char>& used,
	const std::function<bool(int)>& on_row, const JitterFn& jitter)
{
	const bool dithering = params.type != E_DITHER_NONE
		&& params.type != E_DITHER_KNOLL;

	if (!dithering) {
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const unsigned char index = FindAtariColorIndex(source[y][x]);
				used.insert(index);
				out[y][x] = atari_palette[index];
			}
			if (on_row && !on_row(y))
				return true;
		}
		return false;
	}

	detail::ErrorMap error_map;
	detail::ClearErrorMap(error_map, width, height);
	// Jitter is only meaningful when randomness is configured; skipping it
	// keeps the deterministic case free of RNG traffic.
	const JitterFn& active_jitter = params.randomness > 0.0 ? jitter : JitterFn();
	const int last_x = width - 1;

	for (int y = 0; y < height; ++y) {
		// Serpentine traversal, alternating direction per row.
		const bool flip = (y & 1) != 0;
		for (int i = 0; i < width; ++i) {
			const int x = flip ? last_x - i : i;

			rgb out_pixel = source[y][x];

			rgb_error p = error_map[y][x];
			p.r += out_pixel.r;
			p.g += out_pixel.g;
			p.b += out_pixel.b;
			if (p.r > 255) p.r = 255; else if (p.r < 0) p.r = 0;
			if (p.g > 255) p.g = 255; else if (p.g < 0) p.g = 0;
			if (p.b > 255) p.b = 255; else if (p.b < 0) p.b = 0;

			out_pixel.r = static_cast<unsigned char>(p.r + 0.5);
			out_pixel.g = static_cast<unsigned char>(p.g + 0.5);
			out_pixel.b = static_cast<unsigned char>(p.b + 0.5);
			out_pixel = atari_palette[FindAtariColorIndex(out_pixel)];

			const rgb in_pixel = source[y][x];
			const double e_r = static_cast<int>(in_pixel.r) - static_cast<int>(out_pixel.r);
			const double e_g = static_cast<int>(in_pixel.g) - static_cast<int>(out_pixel.g);
			const double e_b = static_cast<int>(in_pixel.b) - static_cast<int>(out_pixel.b);

			auto diffuse = [&](int dx, int dy, double weight) {
				detail::DiffuseError(error_map, width, height, dx, dy, weight,
					e_r, e_g, e_b, params, active_jitter);
			};

			switch (params.type) {
			case E_DITHER_FLOYD:
				diffuse(x - 1, y,     7.0 / 16.0);
				diffuse(x + 1, y + 1, 3.0 / 16.0);
				diffuse(x,     y + 1, 5.0 / 16.0);
				diffuse(x - 1, y + 1, 1.0 / 16.0);
				break;
			case E_DITHER_LINE:
				// Halves the number of distinct colours per scanline.
				if (y % 2 == 0)
					diffuse(x, y + 1, 0.5);
				break;
			case E_DITHER_LINE2:
				diffuse(x, y + 1, 0.5);
				break;
			case E_DITHER_CHESS:
				if ((x + y) % 2 == 0) {
					diffuse(x + 1, y,     0.5);
					diffuse(x,     y + 1, 0.5);
				}
				break;
			case E_DITHER_SIMPLE:
				diffuse(x + 1, y,     1.0 / 3.0);
				diffuse(x,     y + 1, 1.0 / 3.0);
				diffuse(x + 1, y + 1, 1.0 / 3.0);
				break;
			case E_DITHER_2D:
				diffuse(x + 1, y,     2.0 / 4.0);
				diffuse(x,     y + 1, 1.0 / 4.0);
				diffuse(x + 1, y + 1, 1.0 / 4.0);
				break;
			case E_DITHER_JARVIS:
				diffuse(x + 1, y,     7.0 / 48.0);
				diffuse(x + 2, y,     5.0 / 48.0);
				diffuse(x - 1, y + 1, 3.0 / 48.0);
				diffuse(x - 2, y + 1, 5.0 / 48.0);
				diffuse(x,     y + 1, 7.0 / 48.0);
				diffuse(x + 1, y + 1, 5.0 / 48.0);
				diffuse(x + 2, y + 1, 3.0 / 48.0);
				diffuse(x - 1, y + 2, 1.0 / 48.0);
				diffuse(x - 2, y + 2, 3.0 / 48.0);
				diffuse(x,     y + 2, 5.0 / 48.0);
				diffuse(x + 1, y + 2, 3.0 / 48.0);
				diffuse(x + 2, y + 2, 1.0 / 48.0);
				break;
			case E_DITHER_RFLOYD:
			default:
				// rfloyd is documented as quantizing without diffusion
				// (help.txt:125-127); it deliberately spreads no error.
				break;
			}

			const unsigned char index = FindAtariColorIndex(out_pixel);
			used.insert(index);
			out[y][x] = atari_palette[index];
		}
		if (on_row && !on_row(y))
			return true;
	}
	return false;
}

//
// ---- Knoll ordered dithering ---------------------------------------------
//

// The 8x8 ordered threshold map Knoll dithering indexes with the pixel
// position. Shared so the preview and the converter cannot pick different maps.
const unsigned char* KnollThresholdMap();

// 64 palette candidates for one source colour, sorted by luminance. Which one a
// pixel receives depends on its position in the threshold map.
struct MixingPlan {
	unsigned colors[64];
};

// Builds the plan for `color`. Deterministic when params.randomness is 0 and
// `jitter` is empty.
MixingPlan DeviseBestMixingPlan(rgb color, const DitherParams& params,
	const JitterFn& jitter);

// Caches plans by colour. A mixing plan costs 64 x 128 distance evaluations, so
// reusing it across repeated colours is the difference between a preview that
// takes seconds and one that takes minutes.
class MixingPlanCache {
public:
	const MixingPlan& Get(const rgb& color, const DitherParams& params,
		const JitterFn& jitter)
	{
		const std::uint32_t key = (static_cast<std::uint32_t>(color.r) << 16)
			| (static_cast<std::uint32_t>(color.g) << 8)
			| static_cast<std::uint32_t>(color.b);
		auto it = plans_.find(key);
		if (it != plans_.end())
			return it->second;
		return plans_.emplace(key, DeviseBestMixingPlan(color, params, jitter)).first->second;
	}

private:
	std::unordered_map<std::uint32_t, MixingPlan> plans_;
};

// Knoll-dithers `source` into `out`. Single-threaded and cancellable through
// `on_row`; the converter uses its own threaded driver over the same core.
template <typename SourceRows, typename OutRows>
bool BuildKnollTarget(const SourceRows& source, int width, int height,
	const DitherParams& params, OutRows& out, std::set<unsigned char>& used,
	const std::function<bool(int)>& on_row, const JitterFn& jitter)
{
	const unsigned char* threshold = KnollThresholdMap();
	MixingPlanCache cache;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const MixingPlan& plan = cache.Get(source[y][x], params, jitter);
			const unsigned map_value = threshold[(x & 7) + ((y & 7) << 3)];
			const unsigned char index =
				static_cast<unsigned char>(plan.colors[map_value]);
			used.insert(index);
			out[y][x] = atari_palette[index];
		}
		if (on_row && !on_row(y))
			return true;
	}
	return false;
}

} // namespace rasta

#endif // TARGETBUILDER_H

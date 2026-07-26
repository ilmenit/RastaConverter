#include "DetailsMask.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <cmath>
#include <string>

#include "FreeImage.h"

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

int main(int argc, char** argv)
{
	Require(argc == 2 || argc == 3,
		"test requires a temporary output path and optional real mask");
	const std::string path = argv[1];
	FIBITMAP* bitmap = FreeImage_Allocate(2, 2, 32);
	Require(bitmap != nullptr, "synthetic RGBA mask must allocate");
	RGBQUAD topLeft{30, 60, 90, 0};
	RGBQUAD topRight{255, 255, 255, 1};
	RGBQUAD bottomLeft{0, 0, 0, 255};
	RGBQUAD bottomRight{10, 20, 30, 127};
	FreeImage_SetPixelColor(bitmap, 0, 1, &topLeft);
	FreeImage_SetPixelColor(bitmap, 1, 1, &topRight);
	FreeImage_SetPixelColor(bitmap, 0, 0, &bottomLeft);
	FreeImage_SetPixelColor(bitmap, 1, 0, &bottomRight);
	Require(FreeImage_Save(FIF_PNG, bitmap, path.c_str(), 0) != 0,
		"synthetic mask must save");
	FreeImage_Unload(bitmap);

	DetailsMask mask;
	std::string error;
	Require(mask.LoadLegacy(path, 2, 2, &error), "synthetic mask must load");
	Require(mask.Width() == 2 && mask.Height() == 2, "mask boundaries must match target");
	Require(mask.At(0, 0) == 60 && mask.At(1, 0) == 255,
		"brightness must use arithmetic sRGB and ignore transparent alpha");
	Require(mask.At(0, 1) == 0 && mask.At(1, 1) == 20,
		"mask loading must preserve top-to-bottom coordinates");
	Require(mask.LoadLegacy(path, 1, 1, &error), "box resize must load");
	Require(mask.At(0, 0) == 84, "box resize must deterministically average coverage");

	Require(ApplyLegacyDetailsWeight(100, 0, 0.5) == 100,
		"black mask must retain ordinary weight");
	Require(ApplyLegacyDetailsWeight(100, 255, 0.5) == 150,
		"white mask must apply 1 + strength");
	Require(ApplyLegacyDetailsWeight(100, 255, 0.0) == 100,
		"zero strength must preserve no-mask behavior");
	Require(ApplyLegacyDetailsWeight(100, 255, -1.0) == 100,
		"negative strength must preserve no-mask behavior");
	Require(ApplyLegacyDetailsWeight(std::numeric_limits<distance_t>::max(), 255, 1.0)
		== std::numeric_limits<distance_t>::max(), "weighted errors must saturate");
	Require(mask.LoadNormalized(path, 2, 2, 1.0, 0.25, 0, &error),
		"normalized mask must load");
	double mean = 0.0;
	for (unsigned y = 0; y < 2; ++y)
		for (unsigned x = 0; x < 2; ++x)
		{
			Require(mask.WeightAt(x, y) > 0.0,
				"normalized background floor must remain nonzero");
			mean += mask.WeightAt(x, y);
		}
	Require(std::abs(mean / 4.0 - 1.0) < 1e-12,
		"normalized effective weights must have fixed mean one");
	Require(mask.WeightAt(1, 0) > mask.WeightAt(0, 1),
		"linear-light white priority must exceed black priority");

	// Degenerate all-zero-weight input (floor=0, strength=0) must not divide
	// by a zero mean/max and produce NaN/UB when cast to unsigned char.
	DetailsMask degenerateMask;
	Require(degenerateMask.LoadNormalized(path, 2, 2, 0.0, 0.0, 0, &error),
		"degenerate zero-floor/zero-strength mask must still load");
	for (unsigned y = 0; y < 2; ++y)
		for (unsigned x = 0; x < 2; ++x)
		{
			Require(std::isfinite(degenerateMask.WeightAt(x, y)),
				"degenerate mask weights must stay finite, not NaN");
			Require(static_cast<int>(degenerateMask.At(x, y)) >= 0,
				"degenerate mask byte values must be well-defined, not UB from a NaN cast");
		}

	const std::string sourceHash = mask.SourceHash();
	const std::string effectiveHash = mask.EffectiveHash();
	Require(sourceHash.size() == 16 && effectiveHash.size() == 16,
		"normalized maps must expose deterministic identities");
	Require(mask.LoadNormalized(path, 2, 2, 1.0, 0.25, 0, &error)
		&& sourceHash == mask.SourceHash() && effectiveHash == mask.EffectiveHash(),
		"normalized map identities must reproduce exactly");
	const double normalizedBackgroundWeight = mask.WeightAt(0, 1);
	Require(mask.SaveEffectivePreview(path + "-effective.png", &error),
		"effective 160-space preview must save as PNG");
	Require(ApplyEffectiveDetailsWeight(100, 1.5) == 150,
		"effective weight must multiply direct error");
	std::vector<screen_line> refinementSource(2);
	for (screen_line& row : refinementSource) row.Resize(2);
	refinementSource[0][0] = Gray(64);
	refinementSource[0][1] = Gray(255);
	refinementSource[1][0] = Gray(64);
	refinementSource[1][1] = Gray(96);
	Require(mask.LoadRefined(path, 2, 2, refinementSource.data(), 1.0, 0.25, 1, 0.5, &error),
		"binary-mask refinement must load deterministically");
	mean = 0.0;
	for (unsigned y = 0; y < 2; ++y)
		for (unsigned x = 0; x < 2; ++x) mean += mask.WeightAt(x, y);
	Require(std::abs(mean / 4.0 - 1.0) < 1e-12,
		"refined weights must retain fixed mean one");
	Require(mask.WeightAt(0, 1) < mask.WeightAt(1, 0),
		"refinement must not expand priority into black-mask background");
	Require(std::abs(mask.WeightAt(0, 1) - normalizedBackgroundWeight) < 1e-12,
		"refinement must preserve literal ROI-total allocation");
	const std::vector<double> linePriorities = mask.LinePriorities(1.0);
	Require(linePriorities.size() == 2 && linePriorities[0] > linePriorities[1],
		"allocation priorities must favor scanlines with stronger effective mask weight");
	if (argc == 3)
	{
		Require(mask.LoadNormalized(argv[2], 160, 116, 0.5, 0.25, 1, &error),
			"supplied real mask must load at converter width");
		Require(mask.Width() == 160 && mask.Height() == 116,
			"supplied real mask must match requested target boundaries");
		Require(mask.SaveEffectivePreview(path + "-real-effective.png", &error),
			"supplied real mask preview must save");
		std::cout << "source_hash=" << mask.SourceHash()
			<< " effective_hash=" << mask.EffectiveHash() << '\n';
	}

	std::cout << "DetailsMaskTests passed\n";
	return 0;
}

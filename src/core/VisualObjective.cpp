#include "VisualObjective.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kOklabEnergyScale = 200000.0;
}

DisplayFilteredObjective::LinearRgb DisplayFilteredObjective::ToLinear(const rgb& color)
{
	auto component = [](unsigned char value) {
		const double srgb = value / 255.0;
		return srgb <= 0.04045
			? srgb / 12.92
			: std::pow((srgb + 0.055) / 1.055, 2.4);
	};
	return {component(color.r), component(color.g), component(color.b)};
}

DisplayFilteredObjective::Oklab DisplayFilteredObjective::ToOklab(const LinearRgb& color)
{
	const double l = 0.4122214708 * color.r + 0.5363325363 * color.g + 0.0514459929 * color.b;
	const double m = 0.2119034982 * color.r + 0.6806995451 * color.g + 0.1073969566 * color.b;
	const double s = 0.0883024619 * color.r + 0.2817188376 * color.g + 0.6299787005 * color.b;
	const double lc = std::cbrt(l);
	const double mc = std::cbrt(m);
	const double sc = std::cbrt(s);
	return {
		0.2104542553 * lc + 0.7936177850 * mc - 0.0040720468 * sc,
		1.9779984951 * lc - 2.4285922050 * mc + 0.4505937099 * sc,
		0.0259040371 * lc + 0.7827717662 * mc - 0.8086757660 * sc
	};
}

void DisplayFilteredObjective::Init(unsigned width, unsigned height,
	const screen_line* reference, const rgb* palette)
{
	m_width = width;
	m_height = height;
	for (unsigned i = 0; i < 128; ++i)
		m_palette_oklab[i] = ToOklab(ToLinear(palette[i]));
	m_reference_direct_oklab.resize(static_cast<size_t>(width) * height);
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
			m_reference_direct_oklab[static_cast<size_t>(y) * width + x] =
				ToOklab(ToLinear(reference[y][x]));
}

distance_accum_t DisplayFilteredObjective::DirectMeanScore(
	const unsigned char* const* rendered_rows) const
{
	double total = 0.0;
	for (unsigned y = 0; y < m_height; ++y)
		for (unsigned x = 0; x < m_width; ++x)
		{
			const Oklab& rendered = m_palette_oklab[rendered_rows[y][x]];
			const Oklab& reference =
				m_reference_direct_oklab[static_cast<size_t>(y) * m_width + x];
			const double dl = rendered.l - reference.l;
			const double da = rendered.a - reference.a;
			const double db = rendered.b - reference.b;
			total += std::sqrt(dl * dl + da * da + db * db) * kOklabEnergyScale;
		}
	return static_cast<distance_accum_t>(std::llround(total));
}

DualFrameObjective::Yuv DualFrameObjective::ToYuv(const rgb& color)
{
	const double r = color.r;
	const double g = color.g;
	const double b = color.b;
	return {
		0.299 * r + 0.587 * g + 0.114 * b,
		-0.168736 * r - 0.331264 * g + 0.5 * b,
		0.5 * r - 0.418688 * g - 0.081312 * b
	};
}

void DualFrameObjective::Init(unsigned width, unsigned height,
	const screen_line* reference, const rgb* palette,
	double lumaWeight, double chromaWeight)
{
	m_width = width;
	m_height = height;
	m_luma_weight = std::max(0.0, lumaWeight);
	m_chroma_weight = std::max(0.0, chromaWeight);
	for (unsigned index = 0; index < 128; ++index)
		m_palette[index] = ToYuv(palette[index]);
	m_target.resize(static_cast<size_t>(width) * height);
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
			m_target[static_cast<size_t>(y) * width + x] = ToYuv(reference[y][x]);
}

DualFrameScore DualFrameObjective::Score(
	const unsigned char* const* frameA,
	const unsigned char* const* frameB) const
{
	DualFrameScore score;
	if (!IsInitialized() || frameA == nullptr || frameB == nullptr)
		return score;
	for (unsigned y = 0; y < m_height; ++y)
		for (unsigned x = 0; x < m_width; ++x)
		{
			const Yuv& a = m_palette[frameA[y][x]];
			const Yuv& b = m_palette[frameB[y][x]];
			const Yuv& target = m_target[static_cast<size_t>(y) * m_width + x];
			const double dy = 0.5 * (a.y + b.y) - target.y;
			const double du = 0.5 * (a.u + b.u) - target.u;
			const double dv = 0.5 * (a.v + b.v) - target.v;
			score.visual += dy * dy + du * du + dv * dv;
			const double fy = a.y - b.y;
			const double fu = a.u - b.u;
			const double fv = a.v - b.v;
			score.flicker += m_luma_weight * fy * fy
				+ m_chroma_weight * (fu * fu + fv * fv);
		}
	score.total = score.visual + score.flicker;
	return score;
}

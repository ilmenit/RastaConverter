#include "ColorCorrection.h"

#include <algorithm>
#include <cmath>

namespace rasta {
namespace {

double HueToRgb(double p, double q, double t)
{
	if (t < 0.0) t += 1.0;
	if (t > 1.0) t -= 1.0;
	if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
	if (t < 1.0 / 2.0) return q;
	if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
	return p;
}

std::uint8_t ToByte(double value)
{
	return static_cast<std::uint8_t>(std::lround(
		std::max(0.0, std::min(1.0, value)) * 255.0));
}

} // namespace

RGB8 AdjustSaturationAndVibrance(RGB8 color, int saturation, int vibrance)
{
	const double r = color.r / 255.0;
	const double g = color.g / 255.0;
	const double b = color.b / 255.0;
	const double maximum = std::max(r, std::max(g, b));
	const double minimum = std::min(r, std::min(g, b));
	const double lightness = (maximum + minimum) * 0.5;
	const double delta = maximum - minimum;
	if (delta == 0.0)
		return color;

	double hue;
	if (maximum == r)
		hue = std::fmod((g - b) / delta + (g < b ? 6.0 : 0.0), 6.0);
	else if (maximum == g)
		hue = (b - r) / delta + 2.0;
	else
		hue = (r - g) / delta + 4.0;
	hue /= 6.0;

	double chroma = delta / (1.0 - std::abs(2.0 * lightness - 1.0));
	const double uniformScale = 1.0 + std::max(-100, std::min(100, saturation)) / 100.0;
	const double selectiveScale = 1.0 +
		(std::max(-100, std::min(100, vibrance)) / 100.0) * (1.0 - chroma);
	chroma = std::max(0.0, std::min(1.0, chroma * uniformScale * selectiveScale));

	const double q = lightness < 0.5
		? lightness * (1.0 + chroma)
		: lightness + chroma - lightness * chroma;
	const double p = 2.0 * lightness - q;
	return {ToByte(HueToRgb(p, q, hue + 1.0 / 3.0)),
		ToByte(HueToRgb(p, q, hue)),
		ToByte(HueToRgb(p, q, hue - 1.0 / 3.0))};
}

} // namespace rasta

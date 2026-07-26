#pragma once

#include <cstdint>

namespace rasta {

struct RGB8 {
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
};

// Adjust chroma in HSL space. Values use the command-line [-100, 100] scale.
// Vibrance weights the adjustment by the inverse of the original saturation.
RGB8 AdjustSaturationAndVibrance(RGB8 color, int saturation, int vibrance);

} // namespace rasta

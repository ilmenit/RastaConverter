#include "DetailsMask.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

#include "FreeImage.h"
#include "FreeImageIO.h"

namespace
{
std::string StableHash(const void* data, size_t size)
{
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	unsigned long long hash = 1469598103934665603ULL;
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ULL;
	}
	std::ostringstream stream;
	stream << std::hex << std::setfill('0') << std::setw(16) << hash;
	return stream.str();
}

std::string StableHashDoubles(const std::vector<double>& values)
{
	std::vector<unsigned char> canonical;
	canonical.reserve(values.size() * 8);
	for (double value : values)
	{
		const unsigned long long fixed = static_cast<unsigned long long>(
			std::llround(std::max(0.0, value) * 1000000000000.0));
		for (unsigned shift = 0; shift < 64; shift += 8)
			canonical.push_back(static_cast<unsigned char>(fixed >> shift));
	}
	return StableHash(canonical.data(), canonical.size());
}

double LinearComponent(unsigned char value)
{
	const double encoded = value / 255.0;
	return encoded <= 0.04045 ? encoded / 12.92
		: std::pow((encoded + 0.055) / 1.055, 2.4);
}

}

bool DetailsMask::LoadLegacy(const std::string& path, unsigned width,
	unsigned height, std::string* error)
{
	m_width = 0;
	m_height = 0;
	m_values.clear();
	m_layer_values.clear();
	m_weights.clear();
	m_coverage.clear();
	m_source_hash.clear();
	m_effective_hash.clear();
	if (path.empty() || width == 0 || height == 0)
	{
		if (error) *error = "details mask path and target dimensions must be nonempty";
		return false;
	}

	FIBITMAP* source = FreeImageLoadUtf8(path);
	if (!source)
	{
		if (error) *error = "unable to load details mask: " + path;
		return false;
	}
	FIBITMAP* resized = FreeImage_Rescale(source, static_cast<int>(width),
		static_cast<int>(height), FILTER_BOX);
	FreeImage_Unload(source);
	if (!resized)
	{
		if (error) *error = "unable to resize details mask: " + path;
		return false;
	}
	FIBITMAP* rgb = FreeImage_ConvertTo24Bits(resized);
	FreeImage_Unload(resized);
	if (!rgb)
	{
		if (error) *error = "unable to convert details mask: " + path;
		return false;
	}

	// FreeImage's scanline origin is the bottom of the image. Reading rows in
	// reverse preserves the top-to-bottom coordinates used by the optimizer.
	m_values.resize(static_cast<size_t>(width) * height);
	RGBQUAD pixel{};
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
		{
			FreeImage_GetPixelColor(rgb, x, height - 1 - y, &pixel);
			m_values[static_cast<size_t>(y) * width + x] =
				static_cast<unsigned char>((static_cast<unsigned>(pixel.rgbRed)
					+ pixel.rgbGreen + pixel.rgbBlue) / 3);
		}
	FreeImage_Unload(rgb);
	m_width = width;
	m_height = height;
	m_layer_values = m_values;
	m_source_hash = StableHash(m_values.data(), m_values.size());
	m_effective_hash = m_source_hash;
	return true;
}

bool DetailsMask::LoadEditableLayer(const std::string& path, unsigned width,
	unsigned height, std::string* error)
{
	m_width = 0;
	m_height = 0;
	m_values.clear();
	m_layer_values.clear();
	m_weights.clear();
	m_coverage.clear();
	m_source_hash.clear();
	m_effective_hash.clear();
	FIBITMAP* source = FreeImageLoadUtf8(path);
	if (!source || width == 0 || height == 0) {
		if (source) FreeImage_Unload(source);
		if (error) *error = "unable to load editable details layer: " + path;
		return false;
	}
	FIBITMAP* resized =
		FreeImage_GetWidth(source) == width && FreeImage_GetHeight(source) == height
		? FreeImage_Clone(source)
		: FreeImage_Rescale(source, static_cast<int>(width),
			static_cast<int>(height), FILTER_BOX);
	FreeImage_Unload(source);
	FIBITMAP* rgb = resized ? FreeImage_ConvertTo24Bits(resized) : nullptr;
	if (resized) FreeImage_Unload(resized);
	if (!rgb) {
		if (error) *error = "unable to convert editable details layer: " + path;
		return false;
	}
	m_layer_values.resize(static_cast<size_t>(width) * height);
	RGBQUAD pixel{};
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x) {
			FreeImage_GetPixelColor(rgb, x, height - 1 - y, &pixel);
			m_layer_values[static_cast<size_t>(y) * width + x] =
				static_cast<unsigned char>((static_cast<unsigned>(pixel.rgbRed)
					+ pixel.rgbGreen + pixel.rgbBlue) / 3);
		}
	FreeImage_Unload(rgb);
	m_width = width;
	m_height = height;
	return RebuildLegacy();
}

void DetailsMask::InitializeNeutral(unsigned width, unsigned height)
{
	m_width = width;
	m_height = height;
	m_values.assign(static_cast<size_t>(width) * height, 0);
	m_layer_values = m_values;
	m_weights.clear();
	m_coverage.clear();
	m_effective_hash = StableHash(m_values.data(), m_values.size());
	m_source_hash = m_effective_hash;
}

bool DetailsMask::SetEditableValue(unsigned x, unsigned y, unsigned char value)
{
	if (x >= m_width || y >= m_height || m_layer_values.empty())
		return false;
	unsigned char& target =
		m_layer_values[static_cast<size_t>(y) * m_width + x];
	if (target == value)
		return false;
	target = value;
	if (!IsNormalized())
		m_values[static_cast<size_t>(y) * m_width + x] = value;
	return true;
}

unsigned char DetailsMask::EditableAt(unsigned x, unsigned y) const
{
	return m_layer_values.at(static_cast<size_t>(y) * m_width + x);
}

bool DetailsMask::RebuildLegacy()
{
	if (m_layer_values.empty())
		return false;
	m_values = m_layer_values;
	m_weights.clear();
	m_coverage.clear();
	m_source_hash = StableHash(m_layer_values.data(), m_layer_values.size());
	m_effective_hash = m_source_hash;
	return true;
}

bool DetailsMask::LoadNormalized(const std::string& path, unsigned width,
	unsigned height, double strength, double floor, unsigned feather,
	std::string* error)
{
	m_width = m_height = 0;
	m_values.clear();
	m_layer_values.clear();
	m_weights.clear();
	m_coverage.clear();
	FIBITMAP* loaded = FreeImageLoadUtf8(path);
	if (!loaded)
	{
		if (error) *error = "unable to load details mask: " + path;
		return false;
	}
	FIBITMAP* rgb = FreeImage_ConvertTo24Bits(loaded);
	FreeImage_Unload(loaded);
	if (!rgb || width == 0 || height == 0)
	{
		if (rgb) FreeImage_Unload(rgb);
		if (error) *error = "unable to convert details mask or invalid target size: " + path;
		return false;
	}
	const unsigned sourceWidth = FreeImage_GetWidth(rgb);
	const unsigned sourceHeight = FreeImage_GetHeight(rgb);
	std::vector<double> source(static_cast<size_t>(sourceWidth) * sourceHeight);
	RGBQUAD pixel{};
	for (unsigned y = 0; y < sourceHeight; ++y)
		for (unsigned x = 0; x < sourceWidth; ++x)
		{
			FreeImage_GetPixelColor(rgb, x, sourceHeight - 1 - y, &pixel);
			source[static_cast<size_t>(y) * sourceWidth + x] =
				0.2126 * LinearComponent(pixel.rgbRed)
				+ 0.7152 * LinearComponent(pixel.rgbGreen)
				+ 0.0722 * LinearComponent(pixel.rgbBlue);
		}
	FreeImage_Unload(rgb);
	const std::string sourceHash = StableHashDoubles(source);

	std::vector<double> coverage(static_cast<size_t>(width) * height, 0.0);
	for (unsigned ty = 0; ty < height; ++ty)
		for (unsigned tx = 0; tx < width; ++tx)
		{
			const double x0 = tx * static_cast<double>(sourceWidth) / width;
			const double x1 = (tx + 1) * static_cast<double>(sourceWidth) / width;
			const double y0 = ty * static_cast<double>(sourceHeight) / height;
			const double y1 = (ty + 1) * static_cast<double>(sourceHeight) / height;
			double total = 0.0;
			for (unsigned sy = static_cast<unsigned>(std::floor(y0)); sy < std::ceil(y1); ++sy)
				for (unsigned sx = static_cast<unsigned>(std::floor(x0)); sx < std::ceil(x1); ++sx)
				{
					const double overlap = (std::min(x1, sx + 1.0) - std::max(x0, static_cast<double>(sx)))
						* (std::min(y1, sy + 1.0) - std::max(y0, static_cast<double>(sy)));
					total += overlap * source[static_cast<size_t>(sy) * sourceWidth + sx];
				}
			coverage[static_cast<size_t>(ty) * width + tx] = total / ((x1 - x0) * (y1 - y0));
		}
	m_layer_values.resize(coverage.size());
	for (size_t i = 0; i < coverage.size(); ++i)
		m_layer_values[i] = static_cast<unsigned char>(std::llround(
			255.0 * std::max(0.0, std::min(1.0, coverage[i]))));
	m_width = width;
	m_height = height;
	m_source_hash = sourceHash;
	return BuildNormalizedWeights(std::move(coverage), strength, floor, feather);
}

bool DetailsMask::RebuildNormalized(double strength, double floor,
	unsigned feather)
{
	if (m_width == 0 || m_height == 0 || m_layer_values.empty())
		return false;
	std::vector<double> coverage(m_layer_values.size());
	for (size_t i = 0; i < coverage.size(); ++i)
		coverage[i] = m_layer_values[i] / 255.0;
	m_source_hash = StableHash(m_layer_values.data(), m_layer_values.size());
	return BuildNormalizedWeights(std::move(coverage), strength, floor, feather);
}

bool DetailsMask::BuildNormalizedWeights(std::vector<double> coverage,
	double strength, double floor, unsigned feather)
{
	const unsigned width = m_width;
	const unsigned height = m_height;
	m_coverage = coverage;
	if (feather > 0)
	{
		std::vector<double> softened(coverage.size());
		for (unsigned y = 0; y < height; ++y)
			for (unsigned x = 0; x < width; ++x)
			{
				double total = 0.0;
				unsigned count = 0;
				for (int yy = std::max(0, static_cast<int>(y) - static_cast<int>(feather));
					yy <= std::min(static_cast<int>(height) - 1, static_cast<int>(y + feather)); ++yy)
					for (int xx = std::max(0, static_cast<int>(x) - static_cast<int>(feather));
						xx <= std::min(static_cast<int>(width) - 1, static_cast<int>(x + feather)); ++xx)
					{
						total += coverage[static_cast<size_t>(yy) * width + xx];
						++count;
					}
				softened[static_cast<size_t>(y) * width + x] = total / count;
			}
		coverage.swap(softened);
	}
	m_weights.resize(coverage.size());
	double mean = 0.0;
	for (size_t i = 0; i < coverage.size(); ++i)
	{
		m_weights[i] = floor + std::max(0.0, strength) * coverage[i];
		mean += m_weights[i];
	}
	mean /= m_weights.size();
	if (mean < 1e-12) mean = 1.0;
	for (double& weight : m_weights) weight /= mean;
	m_values.resize(m_weights.size());
	double maximum = *std::max_element(m_weights.begin(), m_weights.end());
	if (maximum < 1e-12) maximum = 1.0;
	for (size_t i = 0; i < m_weights.size(); ++i)
		m_values[i] = static_cast<unsigned char>(std::llround(255.0 * m_weights[i] / maximum));
	m_effective_hash = StableHashDoubles(m_weights);
	return true;
}

bool DetailsMask::LoadRefined(const std::string& path, unsigned width,
	unsigned height, const screen_line* source, double strength, double floor,
	unsigned feather, double refineMix, std::string* error)
{
	if (source == nullptr || !LoadNormalized(path, width, height, 1.0, floor, 0, error))
		return false;
	return BuildRefinedWeights(source, strength, floor, feather, refineMix);
}

bool DetailsMask::RebuildRefined(const screen_line* source, double strength,
	double floor, unsigned feather, double refineMix)
{
	if (source == nullptr || m_width == 0 || m_height == 0
		|| m_layer_values.empty())
		return false;
	// Start from the edited literal layer without normalizing it; refinement
	// redistributes priority only inside that layer's coverage.
	m_coverage.resize(m_layer_values.size());
	for (size_t i = 0; i < m_layer_values.size(); ++i)
		m_coverage[i] = m_layer_values[i] / 255.0;
	m_source_hash = StableHash(m_layer_values.data(), m_layer_values.size());
	return BuildRefinedWeights(source, strength, floor, feather, refineMix);
}

bool DetailsMask::BuildRefinedWeights(const screen_line* source,
	double strength, double floor, unsigned feather, double refineMix)
{
	if (source == nullptr || m_width == 0 || m_height == 0
		|| m_coverage.size() != m_layer_values.size())
		return false;
	const unsigned width = m_width;
	const unsigned height = m_height;
	std::vector<double> luminance(static_cast<size_t>(width) * height);
	std::vector<double> red(luminance.size()), green(luminance.size()), blue(luminance.size());
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
		{
			const rgb& color = source[y][x];
			const size_t offset = static_cast<size_t>(y) * width + x;
			red[offset] = LinearComponent(color.r);
			green[offset] = LinearComponent(color.g);
			blue[offset] = LinearComponent(color.b);
			luminance[offset] = 0.2126 * red[offset] + 0.7152 * green[offset]
				+ 0.0722 * blue[offset];
		}

	std::vector<double> importance(luminance.size(), 0.0);
	double signalMean = 0.0;
	double coverageTotal = 0.0;
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
		{
			const size_t offset = static_cast<size_t>(y) * width + x;
			if (m_coverage[offset] <= 0.0) continue;
			double signal = 0.0;
			for (unsigned radius : {1U, 3U})
			{
				double localL = 0.0, localR = 0.0, localG = 0.0, localB = 0.0;
				unsigned count = 0;
				for (int yy = std::max(0, static_cast<int>(y) - static_cast<int>(radius));
					yy <= std::min(static_cast<int>(height) - 1, static_cast<int>(y + radius)); ++yy)
					for (int xx = std::max(0, static_cast<int>(x) - static_cast<int>(radius));
						xx <= std::min(static_cast<int>(width) - 1, static_cast<int>(x + radius)); ++xx)
					{
						const size_t nearby = static_cast<size_t>(yy) * width + xx;
						localL += luminance[nearby]; localR += red[nearby];
						localG += green[nearby]; localB += blue[nearby]; ++count;
					}
				localL /= count; localR /= count; localG /= count; localB /= count;
				signal += std::abs(luminance[offset] - localL)
					+ 0.35 * std::sqrt((red[offset] - localR) * (red[offset] - localR)
						+ (green[offset] - localG) * (green[offset] - localG)
						+ (blue[offset] - localB) * (blue[offset] - localB));
			}
			const unsigned right = std::min(x + 1, width - 1);
			const unsigned down = std::min(y + 1, height - 1);
			signal += 0.75 * (std::abs(luminance[offset] - luminance[static_cast<size_t>(y) * width + right])
				+ std::abs(luminance[offset] - luminance[static_cast<size_t>(down) * width + x]));
			importance[offset] = signal;
			signalMean += signal * m_coverage[offset];
			coverageTotal += m_coverage[offset];
		}
	signalMean = coverageTotal > 0.0 ? signalMean / coverageTotal : 1.0;
	if (signalMean < 1e-12) signalMean = 1.0;

	std::vector<double> salience(importance.size(), 0.0);
	double salienceTotal = 0.0;
	for (unsigned y = 0; y < height; ++y)
		for (unsigned x = 0; x < width; ++x)
		{
			const size_t offset = static_cast<size_t>(y) * width + x;
			double boundary = 1.0;
			if (m_coverage[offset] > 0.0 && feather > 0)
			{
				unsigned distance = feather + 1;
				for (unsigned radius = 1; radius <= feather && distance > feather; ++radius)
					for (int yy = std::max(0, static_cast<int>(y) - static_cast<int>(radius));
						yy <= std::min(static_cast<int>(height) - 1, static_cast<int>(y + radius)); ++yy)
						for (int xx = std::max(0, static_cast<int>(x) - static_cast<int>(radius));
							xx <= std::min(static_cast<int>(width) - 1, static_cast<int>(x + radius)); ++xx)
							if (m_coverage[static_cast<size_t>(yy) * width + xx] <= 0.0)
								distance = std::min(distance, radius);
				boundary = std::min(1.0, static_cast<double>(distance) / feather);
			}
			const double feature = std::max(0.5, std::min(2.5,
				0.5 + importance[offset] / signalMean));
			const double mixedFeature = 1.0 + std::max(0.0, std::min(1.0, refineMix))
				* (feature - 1.0);
			salience[offset] = m_coverage[offset] * boundary * mixedFeature;
			salienceTotal += salience[offset];
		}
	// Refinement redistributes a literal mask's priority within its ROI; it must
	// not increase the ROI's total allocation merely because features exist.
	const double salienceScale = salienceTotal > 0.0
		? coverageTotal / salienceTotal : 1.0;
	m_weights.resize(importance.size());
	double weightMean = 0.0;
	for (size_t offset = 0; offset < m_weights.size(); ++offset)
	{
		m_weights[offset] = floor + std::max(0.0, strength)
			* salience[offset] * salienceScale;
			weightMean += m_weights[offset];
	}
	weightMean /= m_weights.size();
	if (weightMean < 1e-12) weightMean = 1.0;
	for (double& weight : m_weights) weight /= weightMean;
	double maximum = *std::max_element(m_weights.begin(), m_weights.end());
	if (maximum < 1e-12) maximum = 1.0;
	for (size_t i = 0; i < m_weights.size(); ++i)
		m_values[i] = static_cast<unsigned char>(std::llround(255.0 * m_weights[i] / maximum));
	m_effective_hash = StableHashDoubles(m_weights);
	return true;
}

double DetailsMask::WeightAt(unsigned x, unsigned y) const
{
	return m_weights.at(static_cast<size_t>(y) * m_width + x);
}

std::vector<double> DetailsMask::LinePriorities(double legacyStrength) const
{
	std::vector<double> priorities(m_height, 1.0);
	if (m_width == 0 || m_height == 0) return priorities;
	for (unsigned y = 0; y < m_height; ++y)
	{
		double total = 0.0;
		for (unsigned x = 0; x < m_width; ++x)
			total += IsNormalized() ? WeightAt(x, y)
				: 1.0 + std::max(0.0, legacyStrength) * At(x, y) / 255.0;
		priorities[y] = total / m_width;
	}
	return priorities;
}

bool DetailsMask::SaveEffectivePreview(const std::string& path, std::string* error) const
{
	if (m_values.empty()) return false;
	FIBITMAP* bitmap = FreeImage_Allocate(m_width, m_height, 24);
	if (!bitmap) return false;
	RGBQUAD pixel{};
	for (unsigned y = 0; y < m_height; ++y)
		for (unsigned x = 0; x < m_width; ++x)
		{
			pixel.rgbRed = pixel.rgbGreen = pixel.rgbBlue = At(x, y);
			FreeImage_SetPixelColor(bitmap, x, m_height - 1 - y, &pixel);
		}
	const bool saved = FreeImageSaveUtf8(FIF_PNG, bitmap, path);
	FreeImage_Unload(bitmap);
	if (!saved && error) *error = "unable to save effective details preview: " + path;
	return saved;
}

bool DetailsMask::SaveEditableLayer(const std::string& path,
	std::string* error) const
{
	if (m_layer_values.empty()) return false;
	FIBITMAP* bitmap = FreeImage_Allocate(m_width, m_height, 24);
	if (!bitmap) return false;
	RGBQUAD pixel{};
	for (unsigned y = 0; y < m_height; ++y)
		for (unsigned x = 0; x < m_width; ++x)
		{
			pixel.rgbRed = pixel.rgbGreen = pixel.rgbBlue = EditableAt(x, y);
			FreeImage_SetPixelColor(bitmap, x, m_height - 1 - y, &pixel);
		}
	const bool saved = FreeImageSaveUtf8(FIF_PNG, bitmap, path);
	FreeImage_Unload(bitmap);
	if (!saved && error) *error = "unable to save editable details layer: " + path;
	return saved;
}

unsigned char DetailsMask::At(unsigned x, unsigned y) const
{
	return m_values.at(static_cast<size_t>(y) * m_width + x);
}

distance_t ApplyLegacyDetailsWeight(distance_t error, unsigned char brightness,
	double strength)
{
	if (brightness == 0 || error == 0 || !(strength > 0.0))
		return error;
	const long double multiplier = 255.0L
		+ static_cast<long double>(brightness) * strength;
	const long double weighted = multiplier * error / 255.0L;
	const long double maximum = std::numeric_limits<distance_t>::max();
	return weighted >= maximum ? std::numeric_limits<distance_t>::max()
		: static_cast<distance_t>(weighted);
}

distance_t ApplyEffectiveDetailsWeight(distance_t error, double weight)
{
	if (error == 0 || !(weight > 0.0)) return 0;
	const long double weighted = static_cast<long double>(error) * weight;
	const long double maximum = std::numeric_limits<distance_t>::max();
	return weighted >= maximum ? std::numeric_limits<distance_t>::max()
		: static_cast<distance_t>(weighted);
}

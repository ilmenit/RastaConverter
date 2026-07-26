#ifndef DETAILSMASK_H
#define DETAILSMASK_H

#include <string>
#include <vector>

#include "Distance.h"
#include "Program.h"

// Compatibility contract for the historical /details path. The image is
// box-resized to target size, alpha is ignored, and brightness is the integer
// arithmetic mean of the three encoded sRGB bytes.
class DetailsMask
{
public:
	bool LoadLegacy(const std::string& path, unsigned width, unsigned height,
		std::string* error = nullptr);
	bool LoadEditableLayer(const std::string& path, unsigned width,
		unsigned height, std::string* error = nullptr);
	bool LoadNormalized(const std::string& path, unsigned width, unsigned height,
		double strength, double floor, unsigned feather,
		std::string* error = nullptr);
	bool LoadRefined(const std::string& path, unsigned width, unsigned height,
		const screen_line* source, double strength, double floor, unsigned feather,
		double refineMix,
		std::string* error = nullptr);
	bool SaveEffectivePreview(const std::string& path,
		std::string* error = nullptr) const;
	bool SaveEditableLayer(const std::string& path,
		std::string* error = nullptr) const;
	void InitializeNeutral(unsigned width, unsigned height);
	bool SetEditableValue(unsigned x, unsigned y, unsigned char value);
	unsigned char EditableAt(unsigned x, unsigned y) const;
	const std::vector<unsigned char>& EditableValues() const { return m_layer_values; }
	bool RebuildLegacy();
	bool RebuildNormalized(double strength, double floor, unsigned feather);
	bool RebuildRefined(const screen_line* source, double strength, double floor,
		unsigned feather, double refineMix);
	bool Empty() const { return m_values.empty(); }
	unsigned Width() const { return m_width; }
	unsigned Height() const { return m_height; }
	unsigned char At(unsigned x, unsigned y) const;
	const std::vector<unsigned char>& Values() const { return m_values; }
	double WeightAt(unsigned x, unsigned y) const;
	const std::string& SourceHash() const { return m_source_hash; }
	const std::string& EffectiveHash() const { return m_effective_hash; }
	bool IsNormalized() const { return !m_weights.empty(); }
	std::vector<double> LinePriorities(double legacyStrength) const;

private:
	bool BuildNormalizedWeights(std::vector<double> coverage, double strength,
		double floor, unsigned feather);
	bool BuildRefinedWeights(const screen_line* source, double strength,
		double floor, unsigned feather, double refineMix);
	unsigned m_width = 0;
	unsigned m_height = 0;
	std::vector<unsigned char> m_values;
	// Target-sized mask source edited by the live UI. `m_values` is the
	// effective visualization and may differ after normalization/refinement.
	std::vector<unsigned char> m_layer_values;
	std::vector<double> m_weights;
	std::vector<double> m_coverage;
	std::string m_source_hash;
	std::string m_effective_hash;
};

// Black retains ordinary error. White applies up to 1 + strength. Results
// saturate instead of wrapping when an unusually large strength is supplied.
distance_t ApplyLegacyDetailsWeight(distance_t error, unsigned char brightness,
	double strength);
distance_t ApplyEffectiveDetailsWeight(distance_t error, double weight);

#endif

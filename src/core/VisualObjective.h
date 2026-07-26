#ifndef VISUALOBJECTIVE_H
#define VISUALOBJECTIVE_H

#include <vector>

#include "Distance.h"
#include "Program.h"

// Experimental Phase 5A full-frame terms. Spatial scoring compares equally
// filtered source/output OKLab; edge scoring compares adjacent linear-light
// luminance gradients so one-pixel oscillation cannot alias away.
class DisplayFilteredObjective
{
public:
	void Init(unsigned width, unsigned height, const screen_line* reference,
		const rgb* palette);
	// Direct unfiltered sum of per-pixel OKLab delta-E. This has the same
	// ordering as benchmark metric_oklab_mean for a fixed image size.
	distance_accum_t DirectMeanScore(
		const unsigned char* const* rendered_rows) const;
	distance_accum_t Score(const unsigned char* const* rendered_rows) const;
	distance_accum_t CompositeScore(distance_accum_t direct_score,
		const unsigned char* const* rendered_rows, double spatial_weight) const;
	distance_accum_t EdgeScore(const unsigned char* const* rendered_rows) const;
	distance_accum_t EdgeCompositeScore(distance_accum_t direct_score,
		const unsigned char* const* rendered_rows, double edge_weight) const;
	distance_accum_t RegionScore(const unsigned char* const* rendered_rows) const;
	distance_accum_t RegionCompositeScore(distance_accum_t direct_score,
		const unsigned char* const* rendered_rows, double region_weight) const;
	bool IsInitialized() const { return !m_reference_oklab.empty(); }

private:
	struct LinearRgb { double r, g, b; };
	struct Oklab { double l, a, b; };

	static LinearRgb ToLinear(const rgb& color);
	static Oklab ToOklab(const LinearRgb& color);
	LinearRgb FilterReference(int x, int y, const screen_line* reference) const;
	LinearRgb FilterRendered(int x, int y, const unsigned char* const* rows) const;

	unsigned m_width = 0;
	unsigned m_height = 0;
	LinearRgb m_palette_linear[128]{};
	Oklab m_palette_oklab[128]{};
	double m_palette_luminance[128]{};
	std::vector<Oklab> m_reference_oklab;
	std::vector<Oklab> m_reference_direct_oklab;
	std::vector<double> m_reference_luminance;
};

// Phase 7 paired-frame objective. Both inputs are complete color-index renders
// produced by the existing evaluator oracle. The perceived image is the
// arithmetic mean of the two frames in YUV; temporal energy is reported
// separately so search policies can distinguish reproduction error from
// objectionable A/B flicker.
struct DualFrameScore
{
	double visual = 0.0;
	double flicker = 0.0;
	double total = 0.0;
};

class DualFrameObjective
{
public:
	void Init(unsigned width, unsigned height, const screen_line* reference,
		const rgb* palette, double lumaWeight, double chromaWeight);
	DualFrameScore Score(const unsigned char* const* frameA,
		const unsigned char* const* frameB) const;
	bool IsInitialized() const { return !m_target.empty(); }

private:
	struct Yuv { double y, u, v; };
	static Yuv ToYuv(const rgb& color);

	unsigned m_width = 0;
	unsigned m_height = 0;
	double m_luma_weight = 0.0;
	double m_chroma_weight = 0.0;
	Yuv m_palette[128]{};
	std::vector<Yuv> m_target;
};

#endif

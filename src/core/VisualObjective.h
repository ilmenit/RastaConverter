#ifndef VISUALOBJECTIVE_H
#define VISUALOBJECTIVE_H

#include <vector>

#include "Distance.h"
#include "Program.h"

// Source-referenced OKLab readout. This is not part of scoring - candidates are
// scored from the separable per-pixel error tables - it exists so the converter
// can report, and the structured-solver research path can compare, how far a
// rendered picture is from the full-colour source in a perceptual space.
//
// It used to carry four alternative objectives (filtered/spatial, composite,
// edge, worst-region) that were added to the score on every evaluation. They
// cost about sixty times the throughput and never beat scoring the source
// directly, on photographs or on artwork, at equal time or at equal evaluation
// counts, so they were removed; their command-line names now select /objective=
// source.
class DisplayFilteredObjective
{
public:
	void Init(unsigned width, unsigned height, const screen_line* reference,
		const rgb* palette);
	// Direct unfiltered sum of per-pixel OKLab delta-E. This has the same
	// ordering as benchmark metric_oklab_mean for a fixed image size.
	distance_accum_t DirectMeanScore(
		const unsigned char* const* rendered_rows) const;
	bool IsInitialized() const { return !m_reference_direct_oklab.empty(); }

private:
	struct LinearRgb { double r, g, b; };
	struct Oklab { double l, a, b; };

	static LinearRgb ToLinear(const rgb& color);
	static Oklab ToOklab(const LinearRgb& color);

	unsigned m_width = 0;
	unsigned m_height = 0;
	Oklab m_palette_oklab[128]{};
	std::vector<Oklab> m_reference_direct_oklab;
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

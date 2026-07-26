#pragma once

// The during-run dashboard (design §9).
//
// Organised around the five questions a user actually has while a run is going:
// how does it look, is it still improving, where is it still wrong, can I nudge
// it, and give me the file. The convergence readout gets the most prominent
// non-image space because "when do I stop?" is the decision the tool otherwise
// gives no help with.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "LiveStats.h"
#include "ImageViewer.h"

struct SDL_Renderer;

namespace rc_live_ui {

// UI-side ring buffer of (evaluations, normalized distance) samples. Nothing in
// the optimizer changes to feed this; the dashboard samples what it is shown.
class ProgressTrace {
public:
	void Sample(unsigned long long evaluations, double normalized_distance);
	void Clear();

	bool empty() const { return samples_.empty(); }
	size_t size() const { return samples_.size(); }
	// Values only, for ImGui::PlotLines.
	const std::vector<float>& values() const { return values_; }
	double best() const { return best_; }
	double worst() const { return worst_; }

private:
	// Named Point rather than Sample so it does not collide with the
	// Sample() member function above.
	struct Point {
		unsigned long long evaluations;
		double distance;
	};
	std::deque<Point> samples_;
	std::vector<float> values_;
	double best_ = 0.0;
	double worst_ = 0.0;
	unsigned long long last_sample_evaluations_ = 0;
};

class Dashboard {
public:
	explicit Dashboard(SDL_Renderer* renderer);

	void SetStats(const LiveStats& stats);
	// Uploads one of the pipeline images the converter produces.
	void SetImage(PreviewStage stage, int width, int height,
		const std::uint32_t* pixels);
	// The effective details-mask weights, for the viewer's overlay.
	void SetMask(const PreviewImage& mask);

	// Draws the whole window. Returns any command the user issued.
	LiveCommand Draw();

	bool wants_keyboard() const;

private:
	void DrawProgressPanel();
	void DrawMutationPanel();
	void DrawDualPanel();
	void DrawDiagnosticsPanel();
	void DrawConfigPanel();
	LiveCommand DrawBottomBar();
	void DrawRail();

	ImageViewer viewer_;
	LiveStats stats_;
	ProgressTrace trace_;
	PreviewResult content_;
	bool content_dirty_ = false;

	int active_panel_ = 0;
	bool show_mutations_ = false;
	bool show_diagnostics_ = false;
	bool show_config_ = true;
	std::string copied_notice_;
	double copied_at_ = 0.0;
	double last_sample_ms_ = 0.0;
	// Used to follow the pipeline automatically until the search starts, so
	// the viewer never sits on an empty picture.
	bool was_preprocessing_ = false;
	bool search_started_ = false;
};

} // namespace rc_live_ui

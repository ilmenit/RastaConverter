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
#include "PaintEditor.h"

struct SDL_Renderer;

namespace rc_live_ui {

// UI-side history of (evaluations, rate) samples. Nothing in the optimizer
// changes to feed this; the dashboard samples what it is shown.
//
// It plots throughput rather than score. The score starts at whatever distance
// a random program happens to have - a number with no fixed ceiling, often ten
// times where it ends up - so a chart of it spends the whole run showing the
// first two seconds and a flat line for everything after. Throughput has a
// natural scale, answers a question the numbers alone cannot ("is it still
// running as fast as it was?"), and makes a stall or a retarget visible as a
// dip rather than as nothing at all.
//
// Two buffers, because the interesting part moves: the whole run is kept by
// thinning - when the buffer fills, every other sample goes and the sampling
// stride doubles, which costs nothing and never loses the shape - and the last
// few minutes are kept at full resolution alongside it.
class ProgressTrace {
public:
	struct Point {
		unsigned long long evaluations;
		double rate;               // evaluations per second
	};

	enum class View { WholeRun, Recent };

	void Sample(unsigned long long evaluations, double rate);
	void MarkEvent(unsigned long long evaluations);
	void Clear();

	bool empty() const { return recent_.empty(); }
	size_t size() const { return recent_.size(); }
	const std::deque<Point>& points(View view) const {
		return view == View::WholeRun ? whole_ : recent_;
	}
	const std::vector<unsigned long long>& events() const { return events_; }

private:
	std::deque<Point> whole_;    // thinned, covers the run from its first sample
	std::deque<Point> recent_;   // every sample, last few minutes
	std::vector<unsigned long long> events_;
	unsigned long long last_sample_evaluations_ = 0;
	size_t stride_ = 1;          // one in `stride_` samples reaches `whole_`
	size_t pushes_ = 0;
};

class Dashboard {
public:
	explicit Dashboard(SDL_Renderer* renderer);

	void SetStats(const LiveStats& stats);
	// Uploads one of the pipeline images the converter produces.
	void SetImage(PreviewStage stage, int width, int height,
		const std::uint32_t* pixels);
	// The effective details-mask weights, for the viewer's overlay, plus the
	// editable source layer the editor paints on.
	void SetMask(const PreviewImage& mask,
		const std::vector<unsigned char>& editable_values);
	void SetDestinationLayer(const std::vector<unsigned char>& palette_indices,
		int width, int height);
	bool TakeEditorApply(GuiEditorApply& request);
	bool EditorWantsDestination() const;

	// Draws the whole window. Returns any command the user issued.
	LiveCommand Draw();

	bool wants_keyboard() const;

private:
	void DrawProgressPanel();
	void DrawRateChart();
	void DrawMutationPanel();
	void DrawDualPanel();
	void DrawDiagnosticsPanel();
	void DrawConfigPanel();
	LiveCommand DrawBottomBar();
	void DrawRail();

	ImageViewer viewer_;
	PaintEditor editor_;
	LiveStats stats_;
	ProgressTrace trace_;
	bool chart_whole_run_ = true;
	std::vector<ImVec2> chart_scratch_;   // polyline, reused every frame
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
	unsigned long long last_objective_revision_ = 0;
	// The editor is opened by the dashboard but only becomes real once the run
	// loop has acknowledged the pause, so the request is held for one frame.
	bool editor_pending_open_ = false;
	bool editor_pending_apply_ = false;
	bool test_stop_requested_ = false;
};

} // namespace rc_live_ui

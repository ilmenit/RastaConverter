#pragma once

// The paused editor (design §10).
//
// One screen for both edit targets. Entering it stops the search - that is the
// whole mental model, "pause, change something, resume" - and the window is
// given over to the canvas, because every dashboard panel is frozen while the
// optimizer is not running and would only cost space.
//
// The two targets share tools, zoom, scroll, history and the apply bar; only
// the picker changes, because only the payload differs: a details-mask pixel is
// a 0-255 priority, a destination pixel is one of the 128 hardware colours.
// Mask parameters live here too - a painted value means nothing without the
// strength that scales it, and the retarget an apply performs already covers
// both, so they cost nothing extra to commit together.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "LiveStats.h"
#include "TargetPreview.h"
#include "gui.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace rc_live_ui {

// Atari pixels are twice as wide as they are tall. Both halves of the editor
// need this: one to lay the canvas out, the other to keep a brush round.
constexpr float kEditorPixelAspect = 2.0f;

class PaintEditor {
public:
	enum class Target { Mask, Destination };

	enum class Tool {
		Pan,
		Brush,
		Line,
		Rectangle,
		Ellipse,
		Bucket,
		Eyedropper,
		Revert,
		Count,
	};

	// What the user asked the run loop to do, once per frame.
	struct Action {
		bool apply = false;
		bool discard = false;
	};

	explicit PaintEditor(SDL_Renderer* renderer);
	~PaintEditor();

	PaintEditor(const PaintEditor&) = delete;
	PaintEditor& operator=(const PaintEditor&) = delete;

	// Opens a session on `target`, taking the current layers as its baseline.
	void Open(Target target);
	void Close();
	bool active() const { return active_; }
	bool wants_destination() const { return target_ == Target::Destination; }

	// Feeds, safe to call every publish. While a session is open the layers are
	// the user's to edit, so incoming values only re-seed the baseline.
	void SetContent(const PreviewResult& content);
	void SetMaskLayer(const std::vector<unsigned char>& values, int width, int height);
	void SetDestinationLayer(const std::vector<unsigned char>& values,
		int width, int height);
	void SetStats(const LiveStats& stats);

	// Draws the whole window; returns what the user pressed.
	Action Draw();

	// Drains the payload of an accepted apply.
	bool TakeApply(GuiEditorApply& request);

	// Development hooks used by the RASTA_TEST_* env vars in Dashboard, so the
	// pause -> paint -> apply -> resume path can be exercised headlessly.
	void TestStroke(unsigned char value);
	void TestRequestApply(bool branch);
	bool has_pending() const { return has_pending_; }

private:
	struct MaskParameters {
		std::string mode = "legacy";
		double strength = 0.5;
		double floor = 0.25;
		int feather = 1;
		bool score = true;

		bool operator==(const MaskParameters& other) const {
			return mode == other.mode && strength == other.strength
				&& floor == other.floor && feather == other.feather
				&& score == other.score;
		}
		bool operator!=(const MaskParameters& other) const { return !(*this == other); }
	};

	struct HistoryEntry {
		std::string label;
		std::vector<GuiMaskPixelChange> pixels;   // empty for a parameter change
		MaskParameters before;
		MaskParameters after;
		bool parameters = false;
	};

	std::vector<unsigned char>& layer();
	const std::vector<unsigned char>& layer() const;
	const std::vector<unsigned char>& baseline() const;

	void DrawTopStrip();
	void DrawTargetRow();
	void DrawToolOptions();
	void DrawToolRail();
	void DrawCanvas(const ImVec2& size);
	void DrawInspector();
	void DrawValuePicker();
	void DrawPalettePicker();
	void DrawMaskPanel();
	void DrawHistoryPanel();
	Action DrawBottomBar();

	// True when a brush centred on a pixel covers the one `dx` columns and `dy`
	// rows away. Measured on screen rather than in the buffer, so "round" looks
	// round on a 2:1 pixel grid instead of coming out as a wide oval.
	bool BrushCovers(int dx, int dy) const;

	// The pixels an operation would touch, as buffer offsets, appended to `out`.
	// Painting and the on-canvas preview both go through these, so what the
	// preview shows is by construction what a release will commit.
	void CollectStamp(int cx, int cy, std::vector<int>& out) const;
	void CollectLine(int x0, int y0, int x1, int y1, std::vector<int>& out) const;
	void CollectShape(int x0, int y0, int x1, int y1, std::vector<int>& out) const;

	void PaintPixel(int x, int y, unsigned char value);
	void PaintOffset(int offset, unsigned char value);
	void Stamp(int x, int y, unsigned char value);
	void StrokeLine(int x0, int y0, int x1, int y1, unsigned char value);
	void StrokeShape(int x0, int y0, int x1, int y1, unsigned char value);
	void BucketFill(int x, int y, unsigned char value);
	void CommitStroke(const char* label);
	void PushParameterChange(const MaskParameters& before, const char* label);
	void Undo();
	void Redo();
	void RebuildLayerTexture();
	void RebuildReferenceTexture();
	std::vector<GuiMaskPixelChange> PendingPixels() const;
	void BuildApply(bool branch);

	SDL_Renderer* renderer_ = nullptr;
	bool active_ = false;
	Target target_ = Target::Mask;

	int width_ = 0;
	int height_ = 0;
	std::vector<unsigned char> mask_values_;
	std::vector<unsigned char> mask_baseline_;
	std::vector<unsigned char> destination_values_;
	std::vector<unsigned char> destination_baseline_;

	MaskParameters parameters_;
	MaskParameters parameter_baseline_;
	MaskParameters parameter_drag_start_;
	bool parameter_drag_active_ = false;

	PreviewResult content_;
	std::uint64_t content_revision_ = 0;
	std::uint64_t drawn_revision_ = 0;
	LiveStats stats_;

	// Tools and painting.
	Tool tool_ = Tool::Brush;
	int brush_size_ = 5;
	bool round_brush_ = true;
	bool fill_shapes_ = false;
	int primary_ = 192;
	int secondary_ = 0;
	bool stroke_active_ = false;
	bool stroke_secondary_ = false;
	int stroke_start_x_ = -1;
	int stroke_start_y_ = -1;
	int last_x_ = -1;
	int last_y_ = -1;
	int hover_x_ = -1;
	int hover_y_ = -1;
	std::unordered_map<size_t, GuiMaskPixelChange> active_changes_;
	// Reused by the collectors so a drag does not allocate per motion event.
	mutable std::vector<int> stroke_cells_;
	mutable std::vector<int> preview_cells_;

	std::vector<HistoryEntry> history_;
	std::vector<HistoryEntry> redo_;

	// View.
	float zoom_ = 2.0f;
	int reference_ = 1;          // 0 none, 1 target, 2 source, 3 best output
	bool heatmap_ = false;
	float reference_opacity_ = 1.0f;
	float mask_opacity_ = 0.55f;
	bool palette_used_only_ = false;

	// Textures.
	SDL_Texture* reference_texture_ = nullptr;
	SDL_Texture* layer_texture_ = nullptr;
	int texture_width_ = 0;
	int texture_height_ = 0;
	bool layer_dirty_ = true;
	bool reference_dirty_ = true;
	int drawn_reference_ = -1;
	bool drawn_heatmap_ = false;

	// Apply payload waiting for the run loop.
	GuiEditorApply pending_;
	bool has_pending_ = false;
	std::string branch_label_;
};

} // namespace rc_live_ui

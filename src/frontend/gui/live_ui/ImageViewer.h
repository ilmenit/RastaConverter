#pragma once

// The persistent image column (design §7.1a, §9.2).
//
// One widget serves Setup and the run dashboard, which is what lets zoom and
// pan survive pressing Convert. It owns the SDL textures for each pipeline
// stage, a shared zoom/pan state across stages, and the three comparison modes.

#include <cstdint>
#include <string>

#include <imgui.h>

#include "TargetPreview.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace rc_live_ui {

class ImageViewer {
public:
	enum class Compare {
		Off,
		Split, // vertical wipe, dragged with the mouse
		Blink, // alternates between the two stages on a timer
	};

	explicit ImageViewer(SDL_Renderer* renderer);
	~ImageViewer();

	ImageViewer(const ImageViewer&) = delete;
	ImageViewer& operator=(const ImageViewer&) = delete;

	// Replaces the textures with a new preview result. Cheap when the sizes
	// are unchanged - the textures are reused and only their pixels rewritten.
	void SetContent(const PreviewResult& result);

	// Draws toolbar and canvas filling the current content region.
	// `busy` shows the updating indicator; `placeholder` is drawn instead of a
	// canvas when there is nothing to show yet. Returns true when the user
	// asked for the exact (slow) recomputation.
	bool Draw(bool busy, const char* placeholder);

	PreviewStage stage() const { return stage_; }
	void set_stage(PreviewStage stage) { stage_ = stage; }

	// Renames the stage buttons and hides the ones not in `visible_mask`
	// (bit per PreviewStage). The Setup screen shows the four pipeline stages;
	// the run dashboard shows Source / Target / Output instead.
	void SetStageLabels(const char* const labels[4], const char* const hints[4],
		unsigned visible_mask);

	// Hides the palette utilization strip, which belongs to Setup.
	void set_show_palette(bool show) { show_palette_ = show; }

	// Supplies the details-mask overlay. An invalid image removes it, and the
	// control disappears with it.
	void SetMask(const PreviewImage& mask);

	bool has_content() const { return content_valid_; }

private:
	void DrawToolbar();
	void DrawCanvas();
	void DrawPaletteReadout();
	// Ensures a texture of the right size exists in `slot` and uploads pixels.
	void UploadStage(int slot, const PreviewImage& image);
	SDL_Texture* TextureFor(PreviewStage stage) const;

	// Zoom that makes the image fit the given viewport, in screen pixels per
	// source pixel.
	float FitZoom(const ImVec2& viewport) const;

	SDL_Renderer* renderer_ = nullptr;
	SDL_Texture* textures_[4] = {nullptr, nullptr, nullptr, nullptr};
	// The details-mask overlay, kept apart from the pipeline stages: it is
	// drawn on top of whichever of them is showing, not instead of one.
	SDL_Texture* mask_texture_ = nullptr;
	int mask_width_ = 0;
	int mask_height_ = 0;

	// How the mask is shown over the picture.
	enum class MaskView { Off, Overlay, Only };
	MaskView mask_view_ = MaskView::Overlay;
	// Percent, because that is what the slider shows; ImGui does not scale a
	// 0..1 value for a "%%" format and rendered 0.55 as "1%".
	float mask_opacity_percent_ = 40.0f;
	int content_width_ = 0;
	int content_height_ = 0;
	bool content_valid_ = false;

	PreviewStage stage_ = PreviewStage::Dithered;
	PreviewStage compare_stage_ = PreviewStage::Quantized;
	Compare compare_ = Compare::Off;

	float zoom_ = 0.0f;   // 0 means "fit"
	ImVec2 pan_ = ImVec2(0.0f, 0.0f); // in source pixels, centre offset
	float split_ = 0.5f;  // wipe position, 0..1
	double blink_started_ = 0.0;

	std::string stage_labels_[4];
	std::string stage_hints_[4];
	unsigned visible_mask_ = 0xF;

	bool show_palette_ = true;
	std::uint32_t histogram_[128] = {};
	int colors_used_ = 0;
	bool approximate_ = false;
	bool exact_available_ = false;
	std::string approximate_reason_;
	std::string error_;
	int compute_ms_ = 0;
};

} // namespace rc_live_ui

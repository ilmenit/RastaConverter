#include "ImageViewer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "LiveTheme.h"
#include "TargetPicture.h"
#include "rgb.h"

namespace rc_live_ui {

namespace {

// Atari pixels are twice as wide as they are tall in this mode, so the canvas
// stretches horizontally to show the picture with its real proportions.
constexpr float kPixelAspect = 2.0f;

constexpr double kBlinkPeriodMs = 700.0;

int SlotOf(PreviewStage stage)
{
	switch (stage) {
	case PreviewStage::Source:    return 0;
	case PreviewStage::Corrected: return 1;
	case PreviewStage::Quantized: return 2;
	case PreviewStage::Dithered:
	default:                      return 3;
	}
}

const PreviewStage kStages[4] = {
	PreviewStage::Source, PreviewStage::Corrected,
	PreviewStage::Quantized, PreviewStage::Dithered};

const char* const kStageHints[4] = {
	"The source, resized to the Atari's geometry. No adjustments applied.",
	"After brightness, contrast, gamma, saturation and vibrance - still full colour.",
	"Mapped to the palette with no dithering: exactly the colours that survive.",
	"The real target the optimizer will aim at, dithering included.",
};

double NowMs()
{
	return static_cast<double>(SDL_GetTicksNS()) / 1.0e6;
}

// A compact segmented control. Returns true when the selection changed.
bool SegmentedButton(const char* id, int* value, const char* const* labels,
	const char* const* tooltips, int count)
{
	bool changed = false;
	ImGui::PushID(id);
	// Only the horizontal gap is tightened; the vertical one has to stay at the
	// style default. ItemSize advances the cursor past the row by ItemSpacing.y,
	// so zeroing it here would make the row's trailing gap depend on whether a
	// segmented button happened to be the last widget on the line - the mask
	// opacity slider appearing then shifted everything below it by 8px.
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
		ImVec2(2.0f, ImGui::GetStyle().ItemSpacing.y));
	for (int i = 0; i < count; ++i) {
		if (i > 0)
			ImGui::SameLine();
		const bool selected = (*value == i);
		if (selected) {
			ImGui::PushStyleColor(ImGuiCol_Button, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
		}
		if (ImGui::Button(labels[i])) {
			*value = i;
			changed = true;
		}
		if (selected)
			ImGui::PopStyleColor(4);
		if (tooltips != nullptr && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltips[i]);
	}
	ImGui::PopStyleVar();
	ImGui::PopID();
	return changed;
}

} // namespace

ImageViewer::ImageViewer(SDL_Renderer* renderer) : renderer_(renderer)
{
	blink_started_ = NowMs();
	static const char* const kDefaultLabels[4] = {
		"Source", "Corrected", "Quantized", "Target"};
	SetStageLabels(kDefaultLabels, kStageHints, 0xF);
}

void ImageViewer::SetStageLabels(const char* const labels[4],
	const char* const hints[4], unsigned visible_mask)
{
	for (int i = 0; i < 4; ++i) {
		stage_labels_[i] = labels[i] != nullptr ? labels[i] : "";
		stage_hints_[i] = hints != nullptr && hints[i] != nullptr ? hints[i] : "";
	}
	visible_mask_ = visible_mask;
	// Keep the selection on something the user can actually see.
	if ((visible_mask_ & (1u << SlotOf(stage_))) == 0) {
		for (int i = 3; i >= 0; --i) {
			if (visible_mask_ & (1u << i)) {
				stage_ = kStages[i];
				break;
			}
		}
	}
}

ImageViewer::~ImageViewer()
{
	for (SDL_Texture*& texture : textures_) {
		if (texture != nullptr) {
			SDL_DestroyTexture(texture);
			texture = nullptr;
		}
	}
	if (mask_texture_ != nullptr) {
		SDL_DestroyTexture(mask_texture_);
		mask_texture_ = nullptr;
	}
}

void ImageViewer::SetMask(const PreviewImage& mask)
{
	if (!mask.valid()) {
		if (mask_texture_ != nullptr) {
			SDL_DestroyTexture(mask_texture_);
			mask_texture_ = nullptr;
		}
		mask_width_ = 0;
		mask_height_ = 0;
		return;
	}
	if (mask_texture_ != nullptr
		&& (mask_width_ != mask.width || mask_height_ != mask.height)) {
		SDL_DestroyTexture(mask_texture_);
		mask_texture_ = nullptr;
	}
	if (mask_texture_ == nullptr) {
		mask_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STREAMING, mask.width, mask.height);
		if (mask_texture_ == nullptr)
			return;
		SDL_SetTextureScaleMode(mask_texture_, SDL_SCALEMODE_NEAREST);
		SDL_SetTextureBlendMode(mask_texture_, SDL_BLENDMODE_BLEND);
	}
	SDL_UpdateTexture(mask_texture_, nullptr, mask.pixels.data(),
		mask.width * static_cast<int>(sizeof(std::uint32_t)));
	mask_width_ = mask.width;
	mask_height_ = mask.height;
}

void ImageViewer::UploadStage(int slot, const PreviewImage& image)
{
	SDL_Texture*& texture = textures_[slot];
	if (!image.valid()) {
		// The stage was not produced this time; drop it so the viewer cannot
		// show a stale image alongside fresh ones.
		if (texture != nullptr) {
			SDL_DestroyTexture(texture);
			texture = nullptr;
		}
		return;
	}
	if (texture != nullptr) {
		float w = 0.0f;
		float h = 0.0f;
		SDL_GetTextureSize(texture, &w, &h);
		if (static_cast<int>(w) != image.width || static_cast<int>(h) != image.height) {
			SDL_DestroyTexture(texture);
			texture = nullptr;
		}
	}
	if (texture == nullptr) {
		texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STREAMING, image.width, image.height);
		if (texture == nullptr)
			return;
		// Nearest keeps hardware pixels crisp when zoomed in, which is the
		// whole point of inspecting a 160-pixel-wide picture.
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
	}
	SDL_UpdateTexture(texture, nullptr, image.pixels.data(),
		image.width * static_cast<int>(sizeof(std::uint32_t)));
}

void ImageViewer::SetContent(const PreviewResult& result)
{
	error_ = result.error;
	// A mask that will not load must say so; silently showing no overlay looks
	// identical to having no mask at all.
	if (!result.mask_error.empty()) {
		error_ = error_.empty() ? result.mask_error
			: error_ + "  " + result.mask_error;
	}
	approximate_ = result.approximate;
	approximate_reason_ = result.approximate_reason;
	exact_available_ = result.exact_available;
	compute_ms_ = result.compute_ms;

	if (!result.error.empty() && !result.source.valid()) {
		content_valid_ = false;
		return;
	}

	UploadStage(0, result.source);
	UploadStage(1, result.corrected);
	UploadStage(2, result.quantized);
	UploadStage(3, result.dithered);
	content_width_ = result.source.width;
	content_height_ = result.source.height;
	content_valid_ = result.source.valid();

	std::copy(std::begin(result.palette_histogram), std::end(result.palette_histogram),
		std::begin(histogram_));
	colors_used_ = result.colors_used;
	SetMask(result.mask);
}

SDL_Texture* ImageViewer::TextureFor(PreviewStage stage) const
{
	return textures_[SlotOf(stage)];
}

float ImageViewer::FitZoom(const ImVec2& viewport) const
{
	if (content_width_ <= 0 || content_height_ <= 0)
		return 1.0f;
	const float by_width = viewport.x / (content_width_ * kPixelAspect);
	const float by_height = viewport.y / content_height_;
	return std::max(0.05f, std::min(by_width, by_height));
}

void ImageViewer::DrawToolbar()
{
	// Only the stages this viewer was configured to expose get a button.
	const char* labels[4];
	const char* hints[4];
	int mapping[4];
	int visible_count = 0;
	int stage_index = 0;
	for (int i = 0; i < 4; ++i) {
		if ((visible_mask_ & (1u << i)) == 0)
			continue;
		labels[visible_count] = stage_labels_[i].c_str();
		hints[visible_count] = stage_hints_[i].c_str();
		mapping[visible_count] = i;
		if (i == SlotOf(stage_))
			stage_index = visible_count;
		++visible_count;
	}
	if (visible_count > 0
		&& SegmentedButton("stage", &stage_index, labels, hints, visible_count)) {
		stage_ = kStages[mapping[stage_index]];
	}

	// Zoom controls sit at the right end of the stage row.
	char zoom_label[32];
	if (zoom_ <= 0.0f)
		std::snprintf(zoom_label, sizeof(zoom_label), "Fit");
	else
		std::snprintf(zoom_label, sizeof(zoom_label), "%.0f%%", zoom_ * 100.0f);
	{
		const float zoom_width = ImGui::CalcTextSize(zoom_label).x
			+ ImGui::CalcTextSize("-+Fit").x
			+ ImGui::GetStyle().FramePadding.x * 6.0f
			+ ImGui::GetStyle().ItemSpacing.x * 3.0f;
		const float right = ImGui::GetContentRegionMax().x - zoom_width;
		if (right > ImGui::GetCursorPosX() + 8.0f)
			ImGui::SameLine(right);
		else
			ImGui::SameLine(0.0f, 12.0f);

		if (ImGui::SmallButton("-"))
			zoom_ = std::max(0.25f, (zoom_ <= 0.0f ? 1.0f : zoom_) * 0.5f);
		ImGui::SameLine(0.0f, 4.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
		ImGui::TextUnformatted(zoom_label);
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 4.0f);
		if (ImGui::SmallButton("+"))
			zoom_ = std::min(32.0f, (zoom_ <= 0.0f ? 1.0f : zoom_) * 2.0f);
		ImGui::SameLine(0.0f, 6.0f);
		if (ImGui::SmallButton("Fit")) {
			zoom_ = 0.0f;
			pan_ = ImVec2(0.0f, 0.0f);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Scroll to zoom, drag to pan.");
	}

	// Compare controls get their own row so nothing collides on a narrow pane.
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted("Compare");
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 8.0f);
	static const char* const kCompareLabels[3] = {"Off", "Split", "Blink"};
	static const char* const kCompareHints[3] = {
		"Show one stage at a time.",
		"Wipe between two stages; drag the divider.",
		"Alternate between two stages - the quickest way to see what dithering buys you.",
	};
	int compare_index = static_cast<int>(compare_);
	if (SegmentedButton("compare", &compare_index, kCompareLabels, kCompareHints, 3))
		compare_ = static_cast<Compare>(compare_index);

	if (mask_texture_ != nullptr && mask_active_) {
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
		ImGui::TextUnformatted("Mask");
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 8.0f);
		static const char* const kMaskLabels[3] = {"Off", "Overlay", "Only"};
		static const char* const kMaskHints[3] = {
			"Hide the details mask.",
			"Blend the mask over the picture; brighter means more error weight. "
			"A view setting - it does not change the mask's effect.",
			"Show the effective mask on its own. A view setting - it does not "
			"change the mask's effect.",
		};
		int mask_index = static_cast<int>(mask_view_);
		if (SegmentedButton("mask", &mask_index, kMaskLabels, kMaskHints, 3))
			mask_view_ = static_cast<MaskView>(mask_index);
		if (mask_view_ == MaskView::Overlay) {
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
			// The value goes beside the slider, not under the grab handle,
			// which hides it at exactly the settings people use most.
			ImGui::SliderFloat("##mask_opacity", &mask_opacity_percent_, 5.0f, 100.0f,
				"", ImGuiSliderFlags_NoInput);
			if (ImGui::IsItemHovered()) {
				// Easy to read as "mask strength" and change expecting the
				// conversion to follow, so it says outright that it does not.
				ImGui::SetTooltip(
					"Display only - how opaque the overlay is drawn.\n"
					"It changes nothing about the conversion.\n\n"
					"How much the mask actually counts is Mask strength\n"
					"(/details_val) in the setup form's Details mask section.");
			}
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
			ImGui::Text("%.0f%%", mask_opacity_percent_);
			ImGui::PopStyleColor();
		}
	}

	if (compare_ != Compare::Off) {
		ImGui::SameLine(0.0f, 10.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
		ImGui::TextUnformatted("vs");
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 6.0f);
		ImGui::SetNextItemWidth(140.0f);
		std::string items;
		int against = 0;
		int against_map[4];
		int count = 0;
		for (int i = 0; i < 4; ++i) {
			if ((visible_mask_ & (1u << i)) == 0)
				continue;
			items += stage_labels_[i];
			items.push_back('\0');
			against_map[count] = i;
			if (i == SlotOf(compare_stage_))
				against = count;
			++count;
		}
		items.push_back('\0');
		if (count > 0 && ImGui::Combo("##against", &against, items.c_str()))
			compare_stage_ = kStages[against_map[against]];
	}
}

void ImageViewer::DrawCanvas()
{
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const float palette_height = show_palette_ ? 108.0f : 0.0f;
	const ImVec2 canvas_size(std::max(40.0f, available.x),
		std::max(80.0f, available.y - palette_height));

	ImGui::BeginChild("canvas", canvas_size, ImGuiChildFlags_Border,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	// Never hand a non-positive size to InvisibleButton below.
	const ImVec2 viewport(std::max(1.0f, ImGui::GetContentRegionAvail().x),
		std::max(1.0f, ImGui::GetContentRegionAvail().y));
	ImDrawList* draw = ImGui::GetWindowDrawList();

	// Checkerboard so a dark image is still distinguishable from empty space.
	const ImU32 dark = IM_COL32(0x10, 0x12, 0x17, 0xFF);
	draw->AddRectFilled(origin, ImVec2(origin.x + viewport.x, origin.y + viewport.y), dark);

	const float effective_zoom = zoom_ > 0.0f ? zoom_ : FitZoom(viewport);
	const float draw_width = content_width_ * kPixelAspect * effective_zoom;
	const float draw_height = content_height_ * effective_zoom;
	const ImVec2 image_min(
		origin.x + (viewport.x - draw_width) * 0.5f + pan_.x,
		origin.y + (viewport.y - draw_height) * 0.5f + pan_.y);
	const ImVec2 image_max(image_min.x + draw_width, image_min.y + draw_height);

	// Interaction surface covering the whole canvas.
	ImGui::InvisibleButton("surface", viewport,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle
		| ImGuiButtonFlags_MouseButtonRight);
	const bool hovered = ImGui::IsItemHovered();
	if (hovered) {
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			const float previous = effective_zoom;
			float next = previous * std::pow(1.25f, wheel);
			next = std::max(0.25f, std::min(32.0f, next));
			// Keep the point under the cursor stationary.
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const float rel_x = mouse.x - (image_min.x + draw_width * 0.5f);
			const float rel_y = mouse.y - (image_min.y + draw_height * 0.5f);
			const float ratio = next / previous;
			pan_.x -= rel_x * (ratio - 1.0f);
			pan_.y -= rel_y * (ratio - 1.0f);
			zoom_ = next;
		}
	}
	if (ImGui::IsItemActive()
		&& ((ImGui::IsMouseDragging(ImGuiMouseButton_Left)
				&& compare_ != Compare::Split)
			|| ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		pan_.x += delta.x;
		pan_.y += delta.y;
	}

	// If the requested stage was never produced - a palette error stops the
	// pipeline after the source, say - fall back to the latest stage that was,
	// rather than presenting a black rectangle.
	SDL_Texture* primary = TextureFor(stage_);
	if (primary == nullptr) {
		for (int slot = SlotOf(stage_); slot >= 0 && primary == nullptr; --slot)
			primary = textures_[slot];
	}
	SDL_Texture* secondary = TextureFor(compare_stage_);

	// Blink swaps which texture is treated as primary on a timer.
	PreviewStage shown_stage = stage_;
	if (compare_ == Compare::Blink && secondary != nullptr) {
		const double phase = std::fmod(NowMs() - blink_started_, kBlinkPeriodMs * 2.0);
		if (phase > kBlinkPeriodMs) {
			std::swap(primary, secondary);
			shown_stage = compare_stage_;
		}
	}

	if (primary != nullptr) {
		draw->AddImage(reinterpret_cast<ImTextureID>(primary), image_min, image_max);
	}

	if (compare_ == Compare::Split && secondary != nullptr) {
		const float divider = image_min.x + draw_width * split_;
		draw->PushClipRect(ImVec2(divider, image_min.y), image_max, true);
		draw->AddImage(reinterpret_cast<ImTextureID>(secondary), image_min, image_max);
		draw->PopClipRect();
		draw->AddLine(ImVec2(divider, image_min.y), ImVec2(divider, image_max.y),
			theme::kAccent, 1.5f);

		// Dragging anywhere on the canvas moves the wipe while split is on.
		if (ImGui::IsItemActive() && draw_width > 1.0f) {
			const float local = (ImGui::GetIO().MousePos.x - image_min.x) / draw_width;
			split_ = std::max(0.0f, std::min(1.0f, local));
		}
		// Label each side so the comparison is unambiguous.
		draw->AddText(ImVec2(image_min.x + 6.0f, image_min.y + 4.0f),
			theme::kTextStrong, PreviewStageName(stage_));
		const char* right_label = PreviewStageName(compare_stage_);
		draw->AddText(ImVec2(image_max.x - ImGui::CalcTextSize(right_label).x - 6.0f,
			image_min.y + 4.0f), theme::kTextStrong, right_label);
	} else if (compare_ == Compare::Blink && secondary != nullptr) {
		draw->AddText(ImVec2(image_min.x + 6.0f, image_min.y + 4.0f),
			theme::kAccent, PreviewStageName(shown_stage));
	}

	// The mask sits on top of the picture rather than replacing a stage, so it
	// can be judged against the image it is steering.
	if (mask_texture_ != nullptr && mask_active_ && mask_view_ != MaskView::Off) {
		const float alpha = mask_view_ == MaskView::Only
			? 1.0f : mask_opacity_percent_ / 100.0f;
		const ImU32 tint = IM_COL32(0xFF, 0xC8, 0x78,
			static_cast<int>(alpha * 255.0f));
		if (mask_view_ == MaskView::Only) {
			// Cover the picture so only the weights are visible.
			draw->AddRectFilled(image_min, image_max, IM_COL32(0x0C, 0x0E, 0x12, 0xFF));
		}
		draw->AddImage(reinterpret_cast<ImTextureID>(mask_texture_), image_min,
			image_max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
	}

	ImGui::EndChild();

	// Outside the canvas child: the readout is laid out below it, which is what
	// canvas_size reserved room for above.
	if (show_palette_)
		DrawPaletteReadout();
}

void ImageViewer::DrawPaletteReadout()
{
	ImGui::Spacing();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;

	// The Atari palette is naturally 16 hues x 8 luminances; drawing it that
	// way makes "colour is collapsing into the grey column" visible at a
	// glance, which is what the saturation and vibrance sliders need.
	constexpr int kHues = 16;
	constexpr int kLumas = 8;
	const float cell_w = std::min(20.0f, (width - 8.0f) / kHues);
	const float cell_h = std::min(10.0f, cell_w * 0.55f);
	std::uint32_t peak = 1;
	for (std::uint32_t count : histogram_)
		peak = std::max(peak, count);

	for (int hue = 0; hue < kHues; ++hue) {
		for (int luma = 0; luma < kLumas; ++luma) {
			const int index = hue * kLumas + luma;
			const rgb& color = atari_palette[index];
			const ImVec2 min(origin.x + hue * cell_w, origin.y + luma * cell_h);
			const ImVec2 max(min.x + cell_w - 1.0f, min.y + cell_h - 1.0f);
			const bool used = histogram_[index] > 0;
			ImU32 fill = IM_COL32(color.r, color.g, color.b, used ? 0xFF : 0x38);
			draw->AddRectFilled(min, max, fill, 1.5f);
			if (used) {
				// A brighter inset marks heavily used entries.
				const float share = static_cast<float>(histogram_[index]) / peak;
				if (share > 0.12f) {
					draw->AddRect(min, max,
						IM_COL32(0xFF, 0xFF, 0xFF, static_cast<int>(60 + share * 120)),
						1.5f, 0, 1.0f);
				}
			}
		}
	}
	ImGui::Dummy(ImVec2(width, kLumas * cell_h + 4.0f));

	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	if (content_valid_) {
		ImGui::Text("%d of 128 palette colours used", colors_used_);
		if (colors_used_ > 0 && colors_used_ < 5) {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kWarning));
			ImGui::TextUnformatted("- very low colour count");
			ImGui::PopStyleColor();
		}
	} else {
		ImGui::TextUnformatted("No image loaded");
	}
	ImGui::PopStyleColor();
}

bool ImageViewer::Draw(bool busy, const char* placeholder)
{
	bool exact_requested = false;
	if (!content_valid_) {
		if (!error_.empty()) {
			ImGui::Spacing();
			InlineNote(error_.c_str(), theme::kDanger);
		}
		const ImVec2 available = ImGui::GetContentRegionAvail();
		ImGui::BeginChild("empty", available, ImGuiChildFlags_Border);
		const ImVec2 size = ImGui::CalcTextSize(placeholder);
		ImGui::SetCursorPos(ImVec2((available.x - size.x) * 0.5f,
			(available.y - size.y) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::TextUnformatted(placeholder);
		ImGui::PopStyleColor();
		ImGui::EndChild();
		return false;
	}

	DrawToolbar();

	// An error has to be visible even when an earlier stage still has pixels
	// to show, otherwise the viewer silently displays something stale.
	if (!error_.empty()) {
		InlineNote(error_.c_str(), theme::kDanger);
	}

	// Status strip: what the preview is and whether it is still catching up.
	if (busy) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kInfo));
		ImGui::TextUnformatted("Updating preview...");
		ImGui::PopStyleColor();
	} else if (approximate_) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kWarning));
		ImGui::TextUnformatted("Approximate preview");
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", approximate_reason_.c_str());
		if (exact_available_) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Update for exact"))
				exact_requested = true;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", approximate_reason_.c_str());
		}
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::Text("%d x %d  -  built in %d ms", content_width_, content_height_, compute_ms_);
		ImGui::PopStyleColor();
	}

	DrawCanvas();
	return exact_requested;
}

} // namespace rc_live_ui

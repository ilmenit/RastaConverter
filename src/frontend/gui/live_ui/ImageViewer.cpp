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
		mask_values_.clear();
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
	if (mask_values_.size() != mask.pixels.size()) {
		mask_values_.resize(mask.pixels.size());
		for (size_t i = 0; i < mask.pixels.size(); ++i)
			mask_values_[i] = static_cast<unsigned char>(mask.pixels[i] & 0xFFu);
	}
}

void ImageViewer::SetEditableMask(const std::vector<unsigned char>& values)
{
	if (values.size() == static_cast<size_t>(mask_width_) * mask_height_) {
		if (destination_painting_) {
			if (stored_mask_original_values_.size() != values.size())
				stored_mask_original_values_ = values;
			stored_mask_values_ = values;
		} else {
			if (mask_original_values_.size() != values.size())
				mask_original_values_ = values;
			mask_values_ = values;
		}
	}
}

void ImageViewer::SetDestinationLayer(const std::vector<unsigned char>& values,
	int width, int height)
{
	if (width != content_width_ || height != content_height_
		|| values.size() != static_cast<size_t>(width) * height)
		return;
	// Core republishes the committed target on redraws. While an edit is
	// staged, that must not overwrite the local canvas buffer.
	if (destination_painting_)
		return;
	destination_values_ = values;
	destination_original_values_ = values;
}

void ImageViewer::SetDestinationPainting(bool enabled)
{
	if (enabled == destination_painting_)
		return;
	if (enabled) {
		stored_mask_values_ = mask_values_;
		stored_mask_original_values_ = mask_original_values_;
		mask_values_ = destination_values_;
		mask_original_values_ = destination_original_values_;
		stage_ = PreviewStage::Quantized;
		stored_mask_view_ = mask_view_;
		mask_view_ = MaskView::Off;
	} else {
		destination_values_ = mask_values_;
		mask_values_ = stored_mask_values_;
		mask_original_values_ = stored_mask_original_values_;
		mask_view_ = stored_mask_view_;
		UploadMaskValues();
	}
	destination_painting_ = enabled;
}

void ImageViewer::ResetDestinationEdits()
{
	destination_values_ = destination_original_values_;
	if (destination_painting_) {
		mask_values_ = destination_original_values_;
		UploadMaskValues();
	}
}

GuiMaskStroke ImageViewer::DestinationChanges() const
{
	GuiMaskStroke stroke;
	const std::vector<unsigned char>& edited =
		destination_painting_ ? mask_values_ : destination_values_;
	if (edited.size() != destination_original_values_.size())
		return stroke;
	for (size_t offset = 0; offset < edited.size(); ++offset) {
		if (edited[offset] == destination_original_values_[offset])
			continue;
		GuiMaskPixelChange change;
		change.x = static_cast<unsigned>(offset % content_width_);
		change.y = static_cast<unsigned>(offset / content_width_);
		change.before = destination_original_values_[offset];
		change.after = edited[offset];
		stroke.pixels.push_back(change);
	}
	return stroke;
}

void ImageViewer::SetMaskPainting(bool enabled, PaintTool tool, int radius,
	unsigned char value, unsigned char secondary_value)
{
	mask_painting_enabled_ = enabled;
	mask_paint_tool_ = tool;
	mask_brush_radius_ = std::max(1, radius);
	mask_brush_value_ = value;
	mask_secondary_value_ = secondary_value;
	if (enabled && !destination_painting_ && mask_view_ == MaskView::Off)
		mask_view_ = MaskView::Overlay;
	if (enabled)
		compare_ = Compare::Off;
}

void ImageViewer::ApplyMaskStroke(const GuiMaskStroke& stroke)
{
	for (const GuiMaskPixelChange& change : stroke.pixels) {
		if (change.x < static_cast<unsigned>(mask_width_)
			&& change.y < static_cast<unsigned>(mask_height_))
			mask_values_[static_cast<size_t>(change.y) * mask_width_ + change.x] =
				change.after;
	}
	UploadMaskValues();
}

bool ImageViewer::TakeMaskStroke(GuiMaskStroke& stroke)
{
	if (completed_mask_stroke_.pixels.empty())
		return false;
	stroke = std::move(completed_mask_stroke_);
	completed_mask_stroke_ = GuiMaskStroke{};
	return true;
}

bool ImageViewer::TakeSampledValue(unsigned char& value)
{
	if (sampled_value_ < 0) return false;
	value = static_cast<unsigned char>(sampled_value_);
	sampled_value_ = -1;
	return true;
}

void ImageViewer::UploadMaskValues()
{
	if (mask_values_.empty())
		return;
	if (destination_painting_) {
		std::vector<std::uint32_t> pixels(mask_values_.size());
		for (size_t i = 0; i < pixels.size(); ++i) {
			const rgb& color = atari_palette[mask_values_[i] % 128];
			pixels[i] = static_cast<std::uint32_t>(color.r)
				| (static_cast<std::uint32_t>(color.g) << 8)
				| (static_cast<std::uint32_t>(color.b) << 16) | 0xFF000000u;
		}
		if (textures_[2] != nullptr)
			SDL_UpdateTexture(textures_[2], nullptr, pixels.data(),
				content_width_ * static_cast<int>(sizeof(std::uint32_t)));
		return;
	}
	if (mask_texture_ == nullptr)
		return;
	std::vector<std::uint32_t> pixels(mask_values_.size());
	for (size_t i = 0; i < pixels.size(); ++i) {
		const std::uint32_t level = mask_values_[i];
		pixels[i] = level | (level << 8) | (level << 16) | (level << 24);
	}
	SDL_UpdateTexture(mask_texture_, nullptr, pixels.data(),
		mask_width_ * static_cast<int>(sizeof(std::uint32_t)));
}

void ImageViewer::PaintPixel(int x, int y, unsigned char value, bool revert)
{
	if (x < 0 || y < 0 || x >= mask_width_ || y >= mask_height_)
		return;
	const size_t offset = static_cast<size_t>(y) * mask_width_ + x;
	// The buffer and the published dimensions can disagree - entering
	// destination painting swaps in a layer that may not have arrived yet -
	// and the shape tools reach this without PaintAt's emptiness check.
	if (offset >= mask_values_.size())
		return;
	if (revert && offset < mask_original_values_.size())
		value = mask_original_values_[offset];
	auto found = active_mask_changes_.find(offset);
	if (found == active_mask_changes_.end()) {
		GuiMaskPixelChange change;
		change.x = static_cast<unsigned>(x);
		change.y = static_cast<unsigned>(y);
		change.before = mask_values_[offset];
		change.after = value;
		active_mask_changes_.emplace(offset, change);
	} else {
		found->second.after = value;
	}
	mask_values_[offset] = value;
}

void ImageViewer::PaintAt(int cx, int cy, unsigned char value, bool revert)
{
	if (mask_values_.empty())
		return;
	const int r = mask_brush_radius_;
	for (int y = std::max(0, cy - r); y <= std::min(mask_height_ - 1, cy + r); ++y)
		for (int x = std::max(0, cx - r); x <= std::min(mask_width_ - 1, cx + r); ++x) {
			const int dx = x - cx;
			const int dy = y - cy;
			if (dx * dx + dy * dy > r * r)
				continue;
			PaintPixel(x, y, value, revert);
		}
	UploadMaskValues();
}

void ImageViewer::PaintLine(int x0, int y0, int x1, int y1,
	unsigned char value, bool revert)
{
	const int dx = x1 - x0;
	const int dy = y1 - y0;
	const int steps = std::max(1, std::max(std::abs(dx), std::abs(dy)));
	for (int step = 0; step <= steps; ++step)
		PaintAt(x0 + dx * step / steps, y0 + dy * step / steps, value, revert);
}

void ImageViewer::PaintShape(int x0, int y0, int x1, int y1,
	unsigned char value)
{
	const int left = std::min(x0, x1);
	const int right = std::max(x0, x1);
	const int top = std::min(y0, y1);
	const int bottom = std::max(y0, y1);
	switch (mask_paint_tool_) {
	case PaintTool::Line:
		PaintLine(x0, y0, x1, y1, value);
		break;
	case PaintTool::Rectangle:
		PaintLine(left, top, right, top, value);
		PaintLine(right, top, right, bottom, value);
		PaintLine(right, bottom, left, bottom, value);
		PaintLine(left, bottom, left, top, value);
		break;
	case PaintTool::FilledRectangle:
		for (int y = top; y <= bottom; ++y)
			for (int x = left; x <= right; ++x)
				PaintPixel(x, y, value, false);
		UploadMaskValues();
		break;
	case PaintTool::Ellipse:
	case PaintTool::FilledEllipse: {
		const double cx = (left + right) * 0.5;
		const double cy = (top + bottom) * 0.5;
		const double rx = std::max(0.5, (right - left) * 0.5);
		const double ry = std::max(0.5, (bottom - top) * 0.5);
		const bool filled = mask_paint_tool_ == PaintTool::FilledEllipse;
		for (int y = top; y <= bottom; ++y)
			for (int x = left; x <= right; ++x) {
				const double nx = (x - cx) / rx;
				const double ny = (y - cy) / ry;
				const double distance = nx * nx + ny * ny;
				const double edge = std::max(1.0 / rx, 1.0 / ry) * 1.5;
				if ((filled && distance <= 1.0)
					|| (!filled && std::abs(distance - 1.0) <= edge))
					PaintPixel(x, y, value, false);
			}
		UploadMaskValues();
		break;
	}
	default:
		break;
	}
}

void ImageViewer::FloodFill(int x, int y, unsigned char value)
{
	if (x < 0 || y < 0 || x >= mask_width_ || y >= mask_height_
		|| mask_values_.empty())
		return;
	const unsigned char target =
		mask_values_[static_cast<size_t>(y) * mask_width_ + x];
	if (target == value)
		return;
	std::vector<size_t> stack{
		static_cast<size_t>(y) * mask_width_ + static_cast<size_t>(x)};
	while (!stack.empty()) {
		const size_t offset = stack.back();
		stack.pop_back();
		if (mask_values_[offset] != target)
			continue;
		const int px = static_cast<int>(offset % mask_width_);
		const int py = static_cast<int>(offset / mask_width_);
		PaintPixel(px, py, value, false);
		if (px > 0) stack.push_back(offset - 1);
		if (px + 1 < mask_width_) stack.push_back(offset + 1);
		if (py > 0) stack.push_back(offset - mask_width_);
		if (py + 1 < mask_height_) stack.push_back(offset + mask_width_);
	}
	UploadMaskValues();
}

void ImageViewer::FinishMaskStroke()
{
	completed_mask_stroke_.pixels.clear();
	completed_mask_stroke_.pixels.reserve(active_mask_changes_.size());
	for (const auto& entry : active_mask_changes_)
		if (entry.second.before != entry.second.after)
			completed_mask_stroke_.pixels.push_back(entry.second);
	active_mask_changes_.clear();
	mask_stroke_active_ = false;
	last_paint_x_ = last_paint_y_ = -1;
	stroke_start_x_ = stroke_start_y_ = -1;
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

	if (mask_texture_ != nullptr) {
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
				&& compare_ != Compare::Split && !mask_painting_enabled_)
			|| ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		pan_.x += delta.x;
		pan_.y += delta.y;
	}

	if (mask_painting_enabled_ && hovered && content_valid_
		&& draw_width > 0.0f && draw_height > 0.0f) {
		const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left)
			|| ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const int px = static_cast<int>((mouse.x - image_min.x)
			/ (kPixelAspect * effective_zoom));
		const int py = static_cast<int>((mouse.y - image_min.y) / effective_zoom);
		if (down && px >= 0 && px < mask_width_ && py >= 0 && py < mask_height_) {
			if (!mask_stroke_active_) {
				mask_stroke_active_ = true;
				active_mask_changes_.clear();
				stroke_start_x_ = last_paint_x_ = px;
				stroke_start_y_ = last_paint_y_ = py;
				mask_stroke_value_ = ImGui::IsMouseDown(ImGuiMouseButton_Right)
					? mask_secondary_value_ : mask_brush_value_;
				if (mask_paint_tool_ == PaintTool::Fill)
					FloodFill(px, py, mask_stroke_value_);
				else if (mask_paint_tool_ == PaintTool::Eyedropper)
					sampled_value_ = mask_values_[
						static_cast<size_t>(py) * mask_width_ + px];
			}
			const unsigned char value = ImGui::IsMouseDown(ImGuiMouseButton_Right)
				? mask_secondary_value_ : mask_brush_value_;
			if (mask_paint_tool_ == PaintTool::Brush
				|| mask_paint_tool_ == PaintTool::RevertBrush)
				PaintLine(last_paint_x_, last_paint_y_, px, py, value,
					mask_paint_tool_ == PaintTool::RevertBrush);
			last_paint_x_ = px;
			last_paint_y_ = py;
		}
	}
	if (mask_stroke_active_
		&& !ImGui::IsMouseDown(ImGuiMouseButton_Left)
		&& !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		if (mask_paint_tool_ != PaintTool::Brush
			&& mask_paint_tool_ != PaintTool::RevertBrush
			&& mask_paint_tool_ != PaintTool::Fill
			&& mask_paint_tool_ != PaintTool::Eyedropper)
			PaintShape(stroke_start_x_, stroke_start_y_,
				last_paint_x_, last_paint_y_, mask_stroke_value_);
		FinishMaskStroke();
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
	if (!destination_painting_ && mask_texture_ != nullptr
		&& mask_view_ != MaskView::Off) {
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

	if (mask_stroke_active_ && mask_paint_tool_ != PaintTool::Brush
		&& mask_paint_tool_ != PaintTool::RevertBrush
		&& mask_paint_tool_ != PaintTool::Fill
		&& mask_paint_tool_ != PaintTool::Eyedropper) {
		auto screenPoint = [&](int x, int y) {
			return ImVec2(image_min.x + (x + 0.5f) * kPixelAspect * effective_zoom,
				image_min.y + (y + 0.5f) * effective_zoom);
		};
		const ImVec2 start = screenPoint(stroke_start_x_, stroke_start_y_);
		const ImVec2 end = screenPoint(last_paint_x_, last_paint_y_);
		if (mask_paint_tool_ == PaintTool::Line)
			draw->AddLine(start, end, theme::kAccent, 2.0f);
		else {
			const ImVec2 minimum(std::min(start.x, end.x), std::min(start.y, end.y));
			const ImVec2 maximum(std::max(start.x, end.x), std::max(start.y, end.y));
			if (mask_paint_tool_ == PaintTool::Rectangle
				|| mask_paint_tool_ == PaintTool::FilledRectangle)
				draw->AddRect(minimum, maximum, theme::kAccent, 0.0f, 0, 2.0f);
			else
				draw->AddEllipse(ImVec2((minimum.x + maximum.x) * 0.5f,
					(minimum.y + maximum.y) * 0.5f),
					ImVec2((maximum.x - minimum.x) * 0.5f,
						(maximum.y - minimum.y) * 0.5f),
					theme::kAccent, 0.0f, 0, 2.0f);
		}
	}

	if (mask_painting_enabled_ && hovered && content_valid_
		&& draw_width > 0.0f && draw_height > 0.0f) {
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const int px = static_cast<int>((mouse.x - image_min.x)
			/ (kPixelAspect * effective_zoom));
		const int py = static_cast<int>((mouse.y - image_min.y) / effective_zoom);
		if (px >= 0 && px < mask_width_ && py >= 0 && py < mask_height_) {
			const ImVec2 center(
				image_min.x + (px + 0.5f) * kPixelAspect * effective_zoom,
				image_min.y + (py + 0.5f) * effective_zoom);
			if (mask_paint_tool_ == PaintTool::Brush
				|| mask_paint_tool_ == PaintTool::RevertBrush) {
				draw->AddEllipse(center,
					ImVec2((mask_brush_radius_ + 0.5f) * kPixelAspect * effective_zoom,
						(mask_brush_radius_ + 0.5f) * effective_zoom),
					theme::kAccent, 0.0f, 0, 1.5f);
			} else {
				const float arm = std::max(4.0f, effective_zoom * 1.5f);
				draw->AddLine(ImVec2(center.x - arm, center.y),
					ImVec2(center.x + arm, center.y), theme::kAccent, 1.5f);
				draw->AddLine(ImVec2(center.x, center.y - arm),
					ImVec2(center.x, center.y + arm), theme::kAccent, 1.5f);
			}
		}
	}

	// Pixel readout, the answer to "what colour is that exactly".
	if (hovered && content_valid_ && draw_width > 0.0f) {
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		const int px = static_cast<int>((mouse.x - image_min.x) / (kPixelAspect * effective_zoom));
		const int py = static_cast<int>((mouse.y - image_min.y) / effective_zoom);
		if (px >= 0 && px < content_width_ && py >= 0 && py < content_height_) {
			char readout[96];
			const size_t offset = static_cast<size_t>(py) * content_width_ + px;
			if (destination_painting_ && offset < mask_values_.size())
				std::snprintf(readout, sizeof(readout),
					"x %d  y %d  palette %u", px, py, mask_values_[offset]);
			else if (mask_painting_enabled_ && offset < mask_values_.size())
				std::snprintf(readout, sizeof(readout),
					"x %d  y %d  priority %.2f", px, py,
					mask_values_[offset] / 255.0);
			else
				std::snprintf(readout, sizeof(readout), "x %d  y %d", px, py);
			const ImVec2 size = ImGui::CalcTextSize(readout);
			const ImVec2 corner(origin.x + 8.0f, origin.y + viewport.y - size.y - 8.0f);
			draw->AddRectFilled(ImVec2(corner.x - 5.0f, corner.y - 3.0f),
				ImVec2(corner.x + size.x + 5.0f, corner.y + size.y + 3.0f),
				IM_COL32(0x0C, 0x0E, 0x12, 0xE0), 4.0f);
			draw->AddText(corner, theme::kTextMuted, readout);
		}
	}

	ImGui::EndChild();

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

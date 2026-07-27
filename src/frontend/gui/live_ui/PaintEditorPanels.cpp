// The editor's presentation half: the window layout, the tool rail, the canvas
// and every inspector panel.
//
// PaintEditor.cpp holds the other half - the session, the pixels, the history
// and the apply payload - which is the part with invariants worth reading on
// its own. Splitting them keeps either file browsable; they share nothing but
// the class, so the boundary costs nothing.

#include "PaintEditor.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "LiveTheme.h"
#include "RecentRuns.h"
#include "TargetPicture.h"
#include "rgb.h"

namespace rc_live_ui {

namespace {
constexpr float kRailWidth = 46.0f;
constexpr float kInspectorWidth = 288.0f;

const char* const kToolNames[] = {
	"Pan", "Brush", "Line", "Rectangle", "Ellipse", "Bucket fill",
	"Eyedropper", "Revert brush",
};

// Why a "5 px" brush is not five buffer pixels across.
const char* const kBrushSizeHint =
	"Diameter as it appears on screen. An Atari pixel is twice as wide as it is\n"
	"tall, so a brush covers half as many columns as rows - otherwise round\n"
	"would paint a wide oval.";

const char* const kToolKeys[] = { "space", "B", "L", "R", "E", "G", "I", "V" };

const char* const kToolHints[] = {
	"Drag to move the picture. Middle-drag does this with any tool.",
	"Freehand. Left drag paints the primary value, right drag the secondary.",
	"Straight line from press to release.",
	"Rectangle; tick Fill shapes for a solid one.",
	"Ellipse; tick Fill shapes for a solid one.",
	"Flood-fill the contiguous area of equal value under the cursor.",
	"Pick the value under the cursor into the primary slot.",
	"Paint back the value this pixel had when the editor opened.",
};

std::string FileName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string FolderName(const std::string& output_path)
{
	const size_t slash = output_path.find_last_of("/\\");
	if (slash == std::string::npos)
		return std::string(".");
	return FileName(output_path.substr(0, slash));
}

// Tool icons are drawn rather than typed: the UI font is loaded with the
// default Latin glyph range, so any pictographic character would come out as a
// box. Primitives cost nothing and scale with the button.
void DrawToolIcon(ImDrawList* draw, ImVec2 c, float s, PaintEditor::Tool tool,
	ImU32 color)
{
	const float h = s * 0.5f;
	switch (tool) {
	case PaintEditor::Tool::Pan:
		draw->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), color, 1.5f);
		draw->AddLine(ImVec2(c.x, c.y - h), ImVec2(c.x, c.y + h), color, 1.5f);
		draw->AddTriangleFilled(ImVec2(c.x - h, c.y), ImVec2(c.x - h + 4, c.y - 3),
			ImVec2(c.x - h + 4, c.y + 3), color);
		draw->AddTriangleFilled(ImVec2(c.x + h, c.y), ImVec2(c.x + h - 4, c.y - 3),
			ImVec2(c.x + h - 4, c.y + 3), color);
		break;
	case PaintEditor::Tool::Brush:
		draw->AddLine(ImVec2(c.x - h + 1, c.y + h - 1), ImVec2(c.x + h - 3, c.y - h + 3),
			color, 2.0f);
		draw->AddCircleFilled(ImVec2(c.x - h + 2, c.y + h - 2), 2.5f, color);
		break;
	case PaintEditor::Tool::Line:
		draw->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), color, 1.6f);
		draw->AddCircleFilled(ImVec2(c.x - h, c.y + h), 2.0f, color);
		draw->AddCircleFilled(ImVec2(c.x + h, c.y - h), 2.0f, color);
		break;
	case PaintEditor::Tool::Rectangle:
		draw->AddRect(ImVec2(c.x - h, c.y - h * 0.75f), ImVec2(c.x + h, c.y + h * 0.75f),
			color, 0.0f, 0, 1.6f);
		break;
	case PaintEditor::Tool::Ellipse:
		draw->AddEllipse(c, ImVec2(h, h * 0.78f), color, 0.0f, 0, 1.6f);
		break;
	case PaintEditor::Tool::Bucket:
		draw->AddQuadFilled(ImVec2(c.x - h, c.y), ImVec2(c.x, c.y - h),
			ImVec2(c.x + h * 0.6f, c.y), ImVec2(c.x, c.y + h * 0.6f), color);
		draw->AddCircleFilled(ImVec2(c.x + h * 0.75f, c.y + h * 0.6f), 2.5f, color);
		break;
	case PaintEditor::Tool::Eyedropper:
		draw->AddLine(ImVec2(c.x - h + 2, c.y + h - 2), ImVec2(c.x + h - 4, c.y - h + 4),
			color, 1.6f);
		draw->AddCircleFilled(ImVec2(c.x + h - 3, c.y - h + 3), 3.0f, color);
		break;
	case PaintEditor::Tool::Revert:
	default:
		draw->AddLine(ImVec2(c.x - h + 1, c.y + h - 1), ImVec2(c.x + h - 3, c.y - h + 3),
			color, 2.0f);
		draw->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), color, 1.2f);
		break;
	}
}

// What a slider row reports back. The interaction queries have to be made while
// the slider is still the last item, so they cannot be left to the caller once
// the value label has been drawn beside it.
struct SliderEdit {
	bool changed = false;
	bool activated = false;
	bool committed = false;   // released after an edit
};

// A slider whose value is a label to its right rather than text inside the
// track. ImGui centres the value in the track, where the grab covers it as soon
// as it is parked anywhere near the middle - which is exactly where a setting
// spends most of its life.
SliderEdit SliderRow(const char* id, const char* label, float* value, float min,
	float max, const char* fmt, const char* widest, ImGuiSliderFlags flags = 0)
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::SameLine();
	const float value_width = ImGui::CalcTextSize(widest).x;
	ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x
		- value_width - ImGui::GetStyle().ItemSpacing.x - 8.0f));
	SliderEdit edit;
	edit.changed = ImGui::SliderFloat(id, value, min, max, "", flags);
	edit.activated = ImGui::IsItemActivated();
	edit.committed = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SameLine();
	ImGui::Text(fmt, *value);
	return edit;
}

SliderEdit SliderRowInt(const char* id, const char* label, int* value, int min,
	int max, const char* fmt, const char* widest)
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::SameLine();
	const float value_width = ImGui::CalcTextSize(widest).x;
	ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x
		- value_width - ImGui::GetStyle().ItemSpacing.x - 8.0f));
	SliderEdit edit;
	edit.changed = ImGui::SliderInt(id, value, min, max, "");
	edit.activated = ImGui::IsItemActivated();
	edit.committed = ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SameLine();
	ImGui::Text(fmt, *value);
	return edit;
}
} // namespace

void PaintEditor::DrawTopStrip()
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted(FolderName(stats_.output_file).c_str());
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 10.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::TextUnformatted(FileName(stats_.input_file).c_str());
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 14.0f);
	Badge("EDITOR - OPTIMIZER PAUSED", theme::kWarning);
	ImGui::SameLine(0.0f, 14.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::Text("%.1fM evals   best %.6f",
		stats_.evaluations / 1e6, stats_.normalized_distance);
	ImGui::PopStyleColor();
}

void PaintEditor::DrawTargetRow()
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted("Editing");
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 8.0f);

	const bool destination_ok = stats_.destination_edit_available;
	for (int index = 0; index < 2; ++index) {
		const bool selected = (index == 0) == (target_ == Target::Mask);
		if (index > 0)
			ImGui::SameLine(0.0f, 2.0f);
		if (selected) {
			ImGui::PushStyleColor(ImGuiCol_Button, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
		}
		const bool disabled = index == 1 && !destination_ok;
		if (disabled)
			ImGui::BeginDisabled();
		if (ImGui::Button(index == 0 ? "Details mask" : "Destination")) {
			const Target wanted = index == 0 ? Target::Mask : Target::Destination;
			if (wanted != target_) {
				// Switching target keeps the session and its pause; only the
				// payload and picker change.
				target_ = wanted;
				layer_dirty_ = true;
				active_changes_.clear();
				stroke_active_ = false;
				primary_ = target_ == Target::Mask ? 192
					: (layer().empty() ? 0 : layer()[0]);
				secondary_ = 0;
				reference_ = target_ == Target::Mask ? 1 : 2;
				reference_dirty_ = true;
			}
		}
		if (disabled)
			ImGui::EndDisabled();
		if (selected)
			ImGui::PopStyleColor(4);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", index == 0
				? "Paint where detail matters. Applying repatches only the pixels\n"
				  "you touched, unless a parameter changed."
				: (destination_ok
					? "Repaint the picture the optimizer is aiming at.\n"
					  "Applying rebuilds all 128 error planes."
					: "Destination editing needs the single-frame target objective."));
		}
	}

	ImGui::SameLine(0.0f, 16.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::TextUnformatted(target_ == Target::Mask
		? "cheap - dirty rectangle" : "full rebuild");
	ImGui::PopStyleColor();

	// Reference and heatmap, right aligned. Mask only: painting the destination
	// paints the picture itself, which covers whatever is beneath it, so the
	// control would sit there promising something it cannot deliver.
	if (target_ != Target::Mask)
		return;

	const float controls = ImGui::GetFontSize() * 28.0f;
	const float right = ImGui::GetContentRegionMax().x - controls;
	if (right > ImGui::GetCursorPosX() + 8.0f)
		ImGui::SameLine(right);
	else
		ImGui::SameLine(0.0f, 12.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted("Beneath");
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 6.0f);
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
	const char* references = "None\0Target\0Source\0Best output\0";
	if (ImGui::Combo("##reference", &reference_, references))
		reference_dirty_ = true;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Which picture is drawn under your paint.");

	// How strongly it shows through. Dimming the picture is how you read faint
	// mask values against a busy image; turning it up is how you decide where
	// the detail is in the first place.
	ImGui::SameLine(0.0f, 8.0f);
	if (reference_ == 0)
		ImGui::BeginDisabled();
	float visibility = reference_opacity_ * 100.0f;
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
	if (ImGui::SliderFloat("##reference_opacity", &visibility, 10.0f, 100.0f, ""))
		reference_opacity_ = visibility / 100.0f;
	ImGui::SameLine(0.0f, 4.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::Text("%.0f%%", visibility);
	ImGui::PopStyleColor();
	if (reference_ == 0)
		ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("How visible the picture beneath your paint is.");

	ImGui::SameLine(0.0f, 10.0f);
	if (ImGui::Checkbox("error heatmap", &heatmap_))
		reference_dirty_ = true;
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Red where the current output is furthest from the target.\n"
			"This is what mask painting is answering.");
	}
}

void PaintEditor::DrawToolOptions()
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted(kToolNames[static_cast<int>(tool_)]);
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 12.0f);

	const bool sized = tool_ == Tool::Brush || tool_ == Tool::Revert;
	if (sized) {
		ImGui::TextUnformatted("size");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
		ImGui::SliderInt("##size", &brush_size_, 1, 33, "");
		ImGui::SameLine();
		ImGui::Text("%d px", brush_size_);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", kBrushSizeHint);
		ImGui::SameLine(0.0f, 10.0f);
		if (ImGui::RadioButton("round", round_brush_))
			round_brush_ = true;
		ImGui::SameLine(0.0f, 6.0f);
		if (ImGui::RadioButton("square", !round_brush_))
			round_brush_ = false;
	} else if (tool_ == Tool::Rectangle || tool_ == Tool::Ellipse) {
		ImGui::Checkbox("fill shapes", &fill_shapes_);
	} else if (tool_ == Tool::Line) {
		ImGui::TextUnformatted("width");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
		ImGui::SliderInt("##size", &brush_size_, 1, 33, "");
		ImGui::SameLine();
		ImGui::Text("%d px", brush_size_);
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::TextUnformatted(kToolHints[static_cast<int>(tool_)]);
		ImGui::PopStyleColor();
	}
}

void PaintEditor::DrawToolRail()
{
	const float side = kRailWidth - 12.0f;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	for (int index = 0; index < static_cast<int>(Tool::Count); ++index) {
		const Tool tool = static_cast<Tool>(index);
		const bool selected = tool == tool_;
		ImGui::PushID(index);
		if (selected) {
			ImGui::PushStyleColor(ImGuiCol_Button, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::ToVec4(theme::kAccent));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::ToVec4(theme::kAccent));
		}
		if (ImGui::Button("##tool", ImVec2(side, side)))
			tool_ = tool;
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		DrawToolIcon(draw, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
			side * 0.5f, tool,
			selected ? IM_COL32(0x14, 0x17, 0x1C, 0xFF) : theme::kTextStrong);
		if (selected)
			ImGui::PopStyleColor(3);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s  (%s)\n%s", kToolNames[index], kToolKeys[index],
				kToolHints[index]);
		}
		ImGui::PopID();
	}
}

void PaintEditor::DrawCanvas(const ImVec2& size)
{
	const float column_left = ImGui::GetCursorPosX();
	if (layer_dirty_)
		RebuildLayerTexture();
	if (reference_dirty_ || drawn_reference_ != reference_
		|| drawn_heatmap_ != heatmap_ || drawn_revision_ != content_revision_)
		RebuildReferenceTexture();

	const ImU32 border = target_ == Target::Mask
		? IM_COL32(0xF0, 0xA8, 0x3C, 0xB0) : IM_COL32(0xA7, 0x8B, 0xFA, 0xC0);
	ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(border));
	ImGui::BeginChild("canvas", size, ImGuiChildFlags_Border,
		ImGuiWindowFlags_HorizontalScrollbar);

	const float scale_x = kEditorPixelAspect * zoom_;
	const float scale_y = zoom_;
	const ImVec2 image_size(std::max(1.0f, width_ * scale_x),
		std::max(1.0f, height_ * scale_y));
	const ImVec2 room = ImGui::GetContentRegionAvail();
	if (room.x > image_size.x)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (room.x - image_size.x) * 0.5f);
	if (room.y > image_size.y)
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (room.y - image_size.y) * 0.5f);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("surface", image_size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
		| ImGuiButtonFlags_MouseButtonMiddle);
	const bool hovered = ImGui::IsItemHovered();
	ImDrawList* draw = ImGui::GetWindowDrawList();

	// Checkerboard, so an unpainted mask is distinguishable from empty space.
	const ImVec2 image_max(origin.x + image_size.x, origin.y + image_size.y);
	draw->AddRectFilled(origin, image_max, IM_COL32(0x10, 0x12, 0x17, 0xFF));
	if (reference_texture_ != nullptr && (reference_ != 0 || heatmap_)) {
		const int visibility = target_ == Target::Mask
			? static_cast<int>(reference_opacity_ * 255.0f) : 255;
		draw->AddImage(reinterpret_cast<ImTextureID>(reference_texture_),
			origin, image_max, ImVec2(0, 0), ImVec2(1, 1),
			IM_COL32(0xFF, 0xFF, 0xFF, visibility));
	}
	if (layer_texture_ != nullptr) {
		const float alpha = target_ == Target::Mask
			? (reference_ == 0 ? 1.0f : mask_opacity_) : 1.0f;
		const ImU32 tint = target_ == Target::Mask
			? IM_COL32(0xFF, 0xC8, 0x78, static_cast<int>(alpha * 255.0f))
			: IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
		draw->AddImage(reinterpret_cast<ImTextureID>(layer_texture_),
			origin, image_max, ImVec2(0, 0), ImVec2(1, 1), tint);
	}

	// Pixel grid, once the pixels are big enough for it to mean something.
	if (zoom_ >= 4.0f) {
		const ImU32 grid = IM_COL32(0xFF, 0xFF, 0xFF, 0x18);
		for (int x = 0; x <= width_; ++x)
			draw->AddLine(ImVec2(origin.x + x * scale_x, origin.y),
				ImVec2(origin.x + x * scale_x, image_max.y), grid);
		for (int y = 0; y <= height_; ++y)
			draw->AddLine(ImVec2(origin.x, origin.y + y * scale_y),
				ImVec2(image_max.x, origin.y + y * scale_y), grid);
	}

	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const int px = static_cast<int>(std::floor((mouse.x - origin.x) / scale_x));
	const int py = static_cast<int>(std::floor((mouse.y - origin.y) / scale_y));
	const bool inside = hovered && px >= 0 && py >= 0 && px < width_ && py < height_;
	hover_x_ = inside ? px : -1;
	hover_y_ = inside ? py : -1;

	// Ctrl+wheel zooms about the cursor; plain wheel scrolls, as everywhere else.
	if (hovered && ImGui::GetIO().KeyCtrl) {
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			const float before_x = (mouse.x - origin.x) / scale_x;
			const float before_y = (mouse.y - origin.y) / scale_y;
			zoom_ = std::max(0.5f, std::min(24.0f,
				zoom_ * (wheel > 0.0f ? 1.25f : 0.8f)));
			const float after_x = before_x * kEditorPixelAspect * zoom_;
			const float after_y = before_y * zoom_;
			ImGui::SetScrollX(after_x - (mouse.x - ImGui::GetWindowPos().x));
			ImGui::SetScrollY(after_y - (mouse.y - ImGui::GetWindowPos().y));
		}
	}

	const bool pan_drag = ImGui::IsItemActive()
		&& (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)
			|| (tool_ == Tool::Pan && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			|| (ImGui::IsKeyDown(ImGuiKey_Space)
				&& ImGui::IsMouseDragging(ImGuiMouseButton_Left)));
	if (pan_drag) {
		const ImVec2 delta = ImGui::GetIO().MouseDelta;
		ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
		ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
	} else if (tool_ != Tool::Pan) {
		const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		const bool right = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		if (inside && (left || right) && !stroke_active_ && ImGui::IsItemActive()) {
			stroke_active_ = true;
			stroke_secondary_ = right && !left;
			stroke_start_x_ = last_x_ = px;
			stroke_start_y_ = last_y_ = py;
			const unsigned char value = static_cast<unsigned char>(
				stroke_secondary_ ? secondary_ : primary_);
			if (tool_ == Tool::Eyedropper) {
				const size_t offset = static_cast<size_t>(py) * width_ + px;
				if (offset < layer().size())
					primary_ = layer()[offset];
			} else if (tool_ == Tool::Bucket) {
				BucketFill(px, py, value);
			} else if (tool_ == Tool::Brush || tool_ == Tool::Revert) {
				Stamp(px, py, value);
			}
		} else if (stroke_active_ && (left || right)) {
			const unsigned char value = static_cast<unsigned char>(
				stroke_secondary_ ? secondary_ : primary_);
			if (tool_ == Tool::Brush || tool_ == Tool::Revert) {
				StrokeLine(last_x_, last_y_, px, py, value);
			}
			last_x_ = px;
			last_y_ = py;
		}
		if (stroke_active_ && !left && !right) {
			const unsigned char value = static_cast<unsigned char>(
				stroke_secondary_ ? secondary_ : primary_);
			if (tool_ == Tool::Line || tool_ == Tool::Rectangle
				|| tool_ == Tool::Ellipse)
				StrokeShape(stroke_start_x_, stroke_start_y_, last_x_, last_y_, value);
			CommitStroke(kToolNames[static_cast<int>(tool_)]);
			stroke_active_ = false;
		}
	}

	// Preview. Both the brush footprint under the cursor and the shape being
	// dragged are drawn from the very pixels the operation would paint, so what
	// is on screen before the release is what the release commits. The old
	// hairline rubber band said nothing about brush width, and the old circular
	// cursor was neither the right shape for a square brush nor the right
	// proportions for a 2:1 pixel.
	bool shaping = stroke_active_ && (tool_ == Tool::Line
		|| tool_ == Tool::Rectangle || tool_ == Tool::Ellipse);
	bool footprint = !stroke_active_ && inside && tool_ != Tool::Pan
		&& tool_ != Tool::Bucket && tool_ != Tool::Eyedropper;
	int shape_x0 = stroke_start_x_, shape_y0 = stroke_start_y_;
	int shape_x1 = last_x_, shape_y1 = last_y_;
	int cursor_x = px, cursor_y = py;

	// Development hook: a synthetic cursor or drag, so the preview - the one
	// thing here that only exists mid-gesture - can be screenshotted headlessly.
	// "x0,y0,x1,y1[,tool index][,brush size]".
	if (const char* pose = SDL_getenv("RASTA_TEST_PREVIEW")) {
		int posed_tool = static_cast<int>(tool_);
		int posed_size = brush_size_;
		if (std::sscanf(pose, "%d,%d,%d,%d,%d,%d", &shape_x0, &shape_y0, &shape_x1,
				&shape_y1, &posed_tool, &posed_size) >= 4) {
			tool_ = static_cast<Tool>(std::max(0, std::min(
				static_cast<int>(Tool::Count) - 1, posed_tool)));
			brush_size_ = std::max(1, std::min(33, posed_size));
			shaping = tool_ == Tool::Line || tool_ == Tool::Rectangle
				|| tool_ == Tool::Ellipse;
			footprint = !shaping;
			cursor_x = shape_x1;
			cursor_y = shape_y1;
		}
	}

	if (shaping || footprint) {
		preview_cells_.clear();
		if (shaping)
			CollectShape(shape_x0, shape_y0, shape_x1, shape_y1, preview_cells_);
		else
			CollectStamp(cursor_x, cursor_y, preview_cells_);
		std::sort(preview_cells_.begin(), preview_cells_.end());
		preview_cells_.erase(std::unique(preview_cells_.begin(),
			preview_cells_.end()), preview_cells_.end());

		const unsigned char value = static_cast<unsigned char>(
			stroke_active_ && stroke_secondary_ ? secondary_ : primary_);
		ImU32 fill = IM_COL32(0xFF, 0xC8, 0x78, 0x90);
		if (target_ == Target::Mask) {
			// The mask overlay is drawn in this amber, so a preview in the same
			// colour reads as "this is what that area will look like".
			fill = IM_COL32(0xFF, 0xC8, 0x78, 0x40 + value / 2);
		} else if (tool_ != Tool::Revert) {
			const rgb& colour = atari_palette[value % 128];
			fill = IM_COL32(colour.r, colour.g, colour.b, 0xE0);
		} else {
			fill = IM_COL32(0xFF, 0xFF, 0xFF, 0x70);
		}

		// A big filled shape would be thousands of quads a frame; past this many
		// cells the outline alone carries the meaning, and the geometry below
		// still shows the extent.
		constexpr size_t kMaxPreviewCells = 6000;
		if (preview_cells_.size() <= kMaxPreviewCells) {
			for (int offset : preview_cells_) {
				const float cx = origin.x + (offset % width_) * scale_x;
				const float cy = origin.y + (offset / width_) * scale_y;
				draw->AddRectFilled(ImVec2(cx, cy),
					ImVec2(cx + scale_x, cy + scale_y), fill);
			}
		}

		// A thin outline of the geometry on top: at 100% zoom the cells alone
		// are too small to judge where a shape starts and ends.
		if (shaping) {
			const ImVec2 a(origin.x + (shape_x0 + 0.5f) * scale_x,
				origin.y + (shape_y0 + 0.5f) * scale_y);
			const ImVec2 b(origin.x + (shape_x1 + 0.5f) * scale_x,
				origin.y + (shape_y1 + 0.5f) * scale_y);
			if (tool_ == Tool::Line)
				draw->AddLine(a, b, theme::kAccent, 1.0f);
			else if (tool_ == Tool::Rectangle)
				draw->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
					ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), theme::kAccent);
			else
				draw->AddEllipse(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f),
					ImVec2(std::abs(b.x - a.x) * 0.5f, std::abs(b.y - a.y) * 0.5f),
					theme::kAccent);
		} else if (!preview_cells_.empty()) {
			// Bounding box of the footprint, so the cursor is findable even
			// where the fill matches what is underneath.
			int min_x = width_, max_x = -1, min_y = height_, max_y = -1;
			for (int offset : preview_cells_) {
				min_x = std::min(min_x, offset % width_);
				max_x = std::max(max_x, offset % width_);
				min_y = std::min(min_y, offset / width_);
				max_y = std::max(max_y, offset / width_);
			}
			draw->AddRect(ImVec2(origin.x + min_x * scale_x,
					origin.y + min_y * scale_y),
				ImVec2(origin.x + (max_x + 1) * scale_x,
					origin.y + (max_y + 1) * scale_y),
				IM_COL32(0xFF, 0xFF, 0xFF, 0x90));
		}
	}

	ImGui::EndChild();
	ImGui::PopStyleColor();

	// Status line: coordinates, and what the value under the cursor is worth.
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	if (hover_x_ >= 0) {
		const size_t offset = static_cast<size_t>(hover_y_) * width_ + hover_x_;
		const unsigned char value = offset < layer().size() ? layer()[offset] : 0;
		if (target_ == Target::Mask) {
			ImGui::Text("x=%d y=%d    mask %3u -> %.2fx error", hover_x_, hover_y_,
				value, 1.0 + parameters_.strength * value / 255.0);
		} else {
			const int index = value % 128;
			ImGui::Text("x=%d y=%d    colour %d   register $%02X", hover_x_, hover_y_,
				index, ((index / 8) << 4) | ((index % 8) << 1));
		}
	} else {
		ImGui::TextUnformatted("move over the canvas to inspect a pixel");
	}
	ImGui::PopStyleColor();

	ImGui::SameLine();
	char zoom_label[32];
	std::snprintf(zoom_label, sizeof(zoom_label), "%.0f%%", zoom_ * 100.0f);
	// Aligned against the canvas column, not the window: inside a group,
	// GetContentRegionMax() is the window's, and pushing the cursor there would
	// stretch the group and shove the inspector off the screen.
	const float zoom_width = ImGui::GetFontSize() * 11.0f;
	const float right = ImGui::GetCursorPosX()
		+ std::max(0.0f, size.x - zoom_width
			- (ImGui::GetCursorPosX() - column_left));
	if (right > ImGui::GetCursorPosX())
		ImGui::SameLine(right);
	if (ImGui::SmallButton("-"))
		zoom_ = std::max(0.5f, zoom_ * 0.8f);
	ImGui::SameLine(0.0f, 4.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted(zoom_label);
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 4.0f);
	if (ImGui::SmallButton("+"))
		zoom_ = std::min(24.0f, zoom_ * 1.25f);
	ImGui::SameLine(0.0f, 6.0f);
	if (ImGui::SmallButton("1:1"))
		zoom_ = 1.0f;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Ctrl+wheel zooms about the cursor.");
}

void PaintEditor::DrawValuePicker()
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted("VALUE");
	ImGui::PopStyleColor();

	auto swatch = [&](int value, const char* id) {
		const float level = value / 255.0f;
		ImGui::ColorButton(id, ImVec4(level, level, level, 1.0f),
			ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
			ImVec2(30.0f, 26.0f));
	};
	swatch(primary_, "##primary");
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::Text("%3d  left drag\n     %.2fx error", primary_,
		1.0 + parameters_.strength * primary_ / 255.0);
	ImGui::PopStyleColor();

	swatch(secondary_, "##secondary");
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::Text("%3d  right drag\n     %.2fx error", secondary_,
		1.0 + parameters_.strength * secondary_ / 255.0);
	ImGui::PopStyleColor();
	ImGui::SameLine();
	if (ImGui::SmallButton("swap"))
		std::swap(primary_, secondary_);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("X");

	// The ramp is the picker: click or drag anywhere along it.
	const float width = ImGui::GetContentRegionAvail().x;
	const ImVec2 ramp_min = ImGui::GetCursorScreenPos();
	const ImVec2 ramp_max(ramp_min.x + width, ramp_min.y + 22.0f);
	ImGui::InvisibleButton("##ramp", ImVec2(width, 22.0f));
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilledMultiColor(ramp_min, ramp_max, IM_COL32(0, 0, 0, 255),
		IM_COL32_WHITE, IM_COL32_WHITE, IM_COL32(0, 0, 0, 255));
	draw->AddRect(ramp_min, ramp_max, IM_COL32(0xFF, 0xFF, 0xFF, 0x40));
	if (ImGui::IsItemActive()) {
		const float t = std::min(1.0f, std::max(0.0f,
			(ImGui::GetIO().MousePos.x - ramp_min.x) / std::max(1.0f, width)));
		primary_ = static_cast<int>(t * 255.0f + 0.5f);
	}
	const float marker = ramp_min.x + width * (primary_ / 255.0f);
	draw->AddLine(ImVec2(marker, ramp_min.y - 2.0f), ImVec2(marker, ramp_max.y + 2.0f),
		theme::kAccent, 2.0f);

	static const int kPresets[] = {0, 64, 128, 192, 255};
	for (int index = 0; index < 5; ++index) {
		if (index > 0)
			ImGui::SameLine(0.0f, 4.0f);
		char label[8];
		std::snprintf(label, sizeof(label), "%d", kPresets[index]);
		if (ImGui::SmallButton(label))
			primary_ = kPresets[index];
	}
	SliderRowInt("##value", "Value", &primary_, 0, 255, "%d", "255");
}

void PaintEditor::DrawPalettePicker()
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted("COLOUR");
	ImGui::PopStyleColor();

	auto swatch = [&](int index, const char* id) {
		const rgb& color = atari_palette[index % 128];
		ImGui::ColorButton(id, ImVec4(color.r / 255.0f, color.g / 255.0f,
			color.b / 255.0f, 1.0f),
			ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
			ImVec2(30.0f, 26.0f));
	};
	swatch(primary_, "##primary");
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::Text("%3d  $%02X  left", primary_ % 128,
		(((primary_ % 128) / 8) << 4) | (((primary_ % 128) % 8) << 1));
	ImGui::PopStyleColor();
	swatch(secondary_, "##secondary");
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::Text("%3d  $%02X  right", secondary_ % 128,
		(((secondary_ % 128) / 8) << 4) | (((secondary_ % 128) % 8) << 1));
	ImGui::PopStyleColor();
	ImGui::SameLine();
	if (ImGui::SmallButton("swap"))
		std::swap(primary_, secondary_);

	// Which colours the picture actually contains; ringing them makes staying
	// inside the existing set a glance rather than a guess.
	std::vector<bool> used(128, false);
	for (unsigned char value : layer())
		used[value % 128] = true;

	ImGui::Checkbox("only colours in use", &palette_used_only_);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Dim the rest of the palette.");

	// The hardware's own arrangement: 16 hues down, 8 luminances across, which
	// is exactly how atari_palette is indexed (hue * 8 + luma).
	const float cell = std::max(12.0f,
		(ImGui::GetContentRegionAvail().x - 8.0f) / 8.0f);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	for (int hue = 0; hue < 16; ++hue) {
		for (int luma = 0; luma < 8; ++luma) {
			const int index = hue * 8 + luma;
			if (luma > 0)
				ImGui::SameLine(0.0f, 1.0f);
			ImGui::PushID(index);
			const rgb& color = atari_palette[index];
			const bool dim = palette_used_only_ && !used[index];
			ImVec4 shown(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
				dim ? 0.25f : 1.0f);
			if (ImGui::ColorButton("##cell", shown,
					ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop
					| ImGuiColorEditFlags_AlphaPreview, ImVec2(cell, cell * 0.8f)))
				primary_ = index;
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				secondary_ = index;
			if (used[index]) {
				draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
					IM_COL32(0xFF, 0xFF, 0xFF, 0xA0), 0.0f, 0, 1.5f);
			}
			if (index == primary_ % 128) {
				draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
					theme::kAccent, 0.0f, 0, 2.5f);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("colour %d   register $%02X   hue %d luma %d%s",
					index, (hue << 4) | (luma << 1), hue, luma,
					used[index] ? "\nused by the picture" : "");
			}
			ImGui::PopID();
		}
	}
}

void PaintEditor::DrawMaskPanel()
{
	Divider();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted("MASK");
	ImGui::PopStyleColor();

	const MaskParameters before = parameters_;
	float strength = static_cast<float>(parameters_.strength);
	const SliderEdit strength_edit = SliderRow("##strength", "Strength", &strength,
		0.0f, 15.0f, "%.2f", "15.00", ImGuiSliderFlags_Logarithmic);
	if (strength_edit.changed)
		parameters_.strength = strength;
	if (strength_edit.committed)
		PushParameterChange(parameter_drag_active_ ? parameter_drag_start_ : before,
			"strength");
	if (strength_edit.activated) {
		parameter_drag_start_ = before;
		parameter_drag_active_ = true;
	}
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	if (parameters_.mode == "legacy")
		ImGui::Text("white = %.2fx error, black = 1.00x", 1.0 + parameters_.strength);
	else
		ImGui::Text("fully masked = %.2fx the floor; the map is then\n"
			"normalized to mean 1", 1.0 + parameters_.strength);
	ImGui::PopStyleColor();

	if (ImGui::TreeNode("Advanced")) {
		int mode = parameters_.mode == "refined" ? 2
			: (parameters_.mode == "normalized" ? 1 : 0);
		const MaskParameters mode_before = parameters_;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 8.0f);
		if (ImGui::Combo("##mode", &mode, "legacy\0normalized\0refined\0")) {
			parameters_.mode = mode == 2 ? "refined"
				: (mode == 1 ? "normalized" : "legacy");
			PushParameterChange(mode_before, "mask mode");
		}
		const bool normalized = parameters_.mode != "legacy";
		if (!normalized)
			ImGui::BeginDisabled();
		float floor = static_cast<float>(parameters_.floor);
		const MaskParameters floor_before = parameters_;
		const SliderEdit floor_edit = SliderRow("##floor", "Floor", &floor, 0.01f,
			1.0f, "%.2f", "1.00");
		if (floor_edit.changed)
			parameters_.floor = floor;
		if (floor_edit.committed)
			PushParameterChange(floor_before, "floor");
		int feather = parameters_.feather;
		const MaskParameters feather_before = parameters_;
		const SliderEdit feather_edit = SliderRowInt("##feather", "Feather",
			&feather, 0, 8, "%d px", "8 px");
		if (feather_edit.changed)
			parameters_.feather = feather;
		if (feather_edit.committed)
			PushParameterChange(feather_before, "feather");
		if (!normalized)
			ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !normalized)
			ImGui::SetTooltip("Floor and feather belong to normalized and refined modes.");
		const MaskParameters score_before = parameters_;
		if (ImGui::Checkbox("apply to scoring", &parameters_.score))
			PushParameterChange(score_before, "scoring");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Off, the mask only steers which scanlines get mutated.\n"
				"Painting turns it on, because otherwise a stroke changes nothing.");
		}
		ImGui::TreePop();
	}

	// Percent, not a 0-1 fraction: ImGui does not scale for a "%%" format, so
	// 0.55 would render as "1%".
	float opacity = mask_opacity_ * 100.0f;
	if (SliderRow("##opacity", "Overlay", &opacity, 5.0f, 100.0f, "%.0f%%",
			"100%").changed)
		mask_opacity_ = opacity / 100.0f;
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::TextWrapped("A view setting - it does not change what the mask does.");
	ImGui::PopStyleColor();
}

void PaintEditor::DrawHistoryPanel()
{
	Divider();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted("HISTORY");
	ImGui::PopStyleColor();
	ImGui::SameLine();
	const float buttons = ImGui::GetFontSize() * 6.0f;
	const float right = ImGui::GetContentRegionMax().x - buttons;
	if (right > ImGui::GetCursorPosX())
		ImGui::SameLine(right);
	ImGui::BeginDisabled(history_.empty());
	if (ImGui::SmallButton("undo"))
		Undo();
	ImGui::EndDisabled();
	ImGui::SameLine(0.0f, 4.0f);
	ImGui::BeginDisabled(redo_.empty());
	if (ImGui::SmallButton("redo"))
		Redo();
	ImGui::EndDisabled();

	if (history_.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::TextUnformatted("nothing changed yet");
		ImGui::PopStyleColor();
		return;
	}
	ImGui::BeginChild("history_list", ImVec2(0.0f, ImGui::GetFontSize() * 7.0f));
	for (size_t index = 0; index < history_.size(); ++index) {
		const HistoryEntry& entry = history_[index];
		const bool last = index + 1 == history_.size();
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(
			last ? theme::kAccent : theme::kTextMuted));
		if (entry.parameters) {
			if (entry.label == "strength")
				ImGui::Text("strength %.2f -> %.2f", entry.before.strength,
					entry.after.strength);
			else
				ImGui::Text("%s changed", entry.label.c_str());
		} else {
			ImGui::Text("%s  %zu px", entry.label.c_str(), entry.pixels.size());
		}
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();
}

void PaintEditor::DrawInspector()
{
	if (target_ == Target::Mask) {
		DrawValuePicker();
		DrawMaskPanel();
	} else {
		DrawPalettePicker();
	}
	DrawHistoryPanel();
}

PaintEditor::Action PaintEditor::DrawBottomBar()
{
	Action action;
	const std::vector<GuiMaskPixelChange> pixels = PendingPixels();
	const bool parameters_changed = target_ == Target::Mask
		&& parameters_ != parameter_baseline_;
	const bool has_edits = !pixels.empty() || parameters_changed;

	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	if (!has_edits) {
		ImGui::TextUnformatted("Nothing changed yet. Resume leaves the run exactly "
			"as it was.");
	} else if (target_ == Target::Destination) {
		ImGui::Text("%zu pixels repainted - applying rebuilds all 128 error planes, "
			"flushes the line caches and resets the acceptance history.",
			pixels.size());
	} else if (parameters_changed) {
		ImGui::Text("%zu pixels and a parameter change - applying reweighs the whole "
			"picture and resets the acceptance history.", pixels.size());
	} else {
		ImGui::Text("%zu pixels - applying repatches just those, flushes the line "
			"caches and resets the acceptance history.", pixels.size());
	}
	ImGui::PopStyleColor();

	const float button_height = ImGui::GetFrameHeight() * 1.25f;
	const float apply_width = ImGui::GetFontSize() * 9.0f;
	const float discard_width = ImGui::GetFontSize() * 7.0f;
	const float right = ImGui::GetContentRegionMax().x
		- apply_width - discard_width - ImGui::GetStyle().ItemSpacing.x * 2.0f
		- ImGui::GetFontSize() * 2.0f;
	if (right > ImGui::GetCursorPosX())
		ImGui::SetCursorPosX(right);

	if (ImGui::Button(has_edits ? "Discard" : "Resume",
			ImVec2(discard_width, button_height)))
		action.discard = true;
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip(has_edits
			? "Throw the edits away and resume the search (Esc)."
			: "Resume the search (Esc).");
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(!has_edits);
	ImGui::PushStyleColor(ImGuiCol_Button, theme::ToVec4(theme::kAccent));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.76f, 0.36f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.62f, 0.22f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
	const bool apply_clicked = ImGui::Button("Apply", ImVec2(apply_width, button_height));
	ImGui::PopStyleColor(4);
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && has_edits) {
		ImGui::SetTooltip("Commit to %s and resume (Enter).",
			FolderName(stats_.output_file).c_str());
	}

	ImGui::SameLine(0.0f, 2.0f);
	ImGui::BeginDisabled(!has_edits);
	const bool menu_clicked = ImGui::Button("v", ImVec2(ImGui::GetFontSize() * 1.6f,
		button_height));
	ImGui::EndDisabled();
	if (menu_clicked) {
		// Probing the filesystem for the next free folder is not something to
		// do every frame; the name is resolved when the menu opens.
		branch_label_ = FolderName(AllocateRunOutputPath(stats_.input_file));
		ImGui::OpenPopup("apply_menu");
	}
	bool branch = false;
	if (ImGui::BeginPopup("apply_menu")) {
		char here[160];
		std::snprintf(here, sizeof(here), "Apply to this run (%s)",
			FolderName(stats_.output_file).c_str());
		if (ImGui::MenuItem(here))
			action.apply = true;
		char there[160];
		std::snprintf(there, sizeof(there), "Apply to a new run (%s)",
			branch_label_.c_str());
		if (ImGui::MenuItem(there)) {
			action.apply = true;
			branch = true;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::TextUnformatted("A new run copies this state and its edits into a\n"
			"fresh folder and carries on there; this one is left as it is.");
		ImGui::PopStyleColor();
		ImGui::EndPopup();
	}

	// Keyboard: Enter applies here, Esc discards.
	if (has_edits && ImGui::IsKeyPressed(ImGuiKey_Enter, false)
		&& !ImGui::GetIO().WantTextInput)
		action.apply = true;
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)
		action.discard = true;

	if (apply_clicked)
		action.apply = true;
	if (action.apply)
		BuildApply(branch);
	return action;
}

PaintEditor::Action PaintEditor::Draw()
{
	Action action;
	if (!active_)
		return action;

	// Tool shortcuts, ignored while a text field has focus.
	if (!ImGui::GetIO().WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_B, false)) tool_ = Tool::Brush;
		if (ImGui::IsKeyPressed(ImGuiKey_L, false)) tool_ = Tool::Line;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) tool_ = Tool::Rectangle;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) tool_ = Tool::Ellipse;
		if (ImGui::IsKeyPressed(ImGuiKey_G, false)) tool_ = Tool::Bucket;
		if (ImGui::IsKeyPressed(ImGuiKey_I, false)) tool_ = Tool::Eyedropper;
		if (ImGui::IsKeyPressed(ImGuiKey_V, false)) tool_ = Tool::Revert;
		if (ImGui::IsKeyPressed(ImGuiKey_X, false)) std::swap(primary_, secondary_);
		if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false))
			brush_size_ = std::max(1, brush_size_ - 2);
		if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false))
			brush_size_ = std::min(33, brush_size_ + 2);
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			if (ImGui::GetIO().KeyShift) Redo(); else Undo();
		}
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
			Redo();
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
	ImGui::Begin("##editor", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar();

	DrawTopStrip();
	Divider();
	DrawTargetRow();
	DrawToolOptions();
	Divider();

	const ImVec2 size = ImGui::GetContentRegionAvail();
	const float bottom_height = ImGui::GetFrameHeight() * 2.6f;
	const float body_height = std::max(120.0f, size.y - bottom_height - 8.0f);

	ImGui::BeginChild("rail", ImVec2(kRailWidth, body_height));
	DrawToolRail();
	ImGui::EndChild();

	ImGui::SameLine(0.0f, 6.0f);
	ImGui::BeginGroup();
	const float canvas_width = std::max(120.0f,
		size.x - kRailWidth - kInspectorWidth - 28.0f);
	DrawCanvas(ImVec2(canvas_width, body_height - ImGui::GetFrameHeight() - 6.0f));
	ImGui::EndGroup();

	ImGui::SameLine(0.0f, 8.0f);
	ImGui::BeginChild("inspector", ImVec2(kInspectorWidth, body_height),
		ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
	DrawInspector();
	ImGui::EndChild();

	ImGui::BeginChild("apply", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border
		| ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
	action = DrawBottomBar();
	ImGui::EndChild();

	ImGui::End();
	return action;
}

} // namespace rc_live_ui

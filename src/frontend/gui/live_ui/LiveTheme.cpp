#include "LiveTheme.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace rc_live_ui {

namespace theme {

ImVec4 ToVec4(ImU32 packed)
{
	return ImGui::ColorConvertU32ToFloat4(packed);
}

} // namespace theme

namespace {

// Candidate UI fonts, best first. The list covers the common desktop Linux
// font packages, macOS and Windows; the first readable file wins.
const char* const kRegularFontCandidates[] = {
	"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf",
	"/Library/Fonts/SFNSDisplay.ttf",
	"/System/Library/Fonts/SFNS.ttf",
	"/System/Library/Fonts/Helvetica.ttc",
	"C:\\Windows\\Fonts\\segoeui.ttf",
	"C:\\Windows\\Fonts\\arial.ttf",
};

const char* const kBoldFontCandidates[] = {
	"/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf",
	"/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
	"/usr/share/fonts/noto/NotoSans-Bold.ttf",
	"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
	"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
	"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
	"/System/Library/Fonts/SFNS.ttf",
	"C:\\Windows\\Fonts\\segoeuib.ttf",
	"C:\\Windows\\Fonts\\arialbd.ttf",
};

const char* const kMonoFontCandidates[] = {
	"/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
	"/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/System/Library/Fonts/Menlo.ttc",
	"C:\\Windows\\Fonts\\consola.ttf",
};

template <size_t N>
const char* FirstReadable(const char* const (&candidates)[N])
{
	for (size_t i = 0; i < N; ++i) {
		if (SDL_GetPathInfo(candidates[i], nullptr))
			return candidates[i];
	}
	return nullptr;
}

ImFont* AddFont(const char* path, float size_px)
{
	if (path == nullptr)
		return nullptr;
	ImFontConfig config;
	config.OversampleH = 2;
	config.OversampleV = 1;
	config.PixelSnapH = true;
	return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size_px, &config);
}

// Label column width as a fraction of the form width, bounded so long labels
// stay readable in a narrow pane and do not eat a wide one.
float LabelColumnWidth()
{
	const float available = ImGui::GetContentRegionAvail().x;
	// Roughly 13 characters of label, bounded so it neither squeezes long
	// labels in a narrow pane nor eats a wide one.
	const float preferred = ImGui::GetFontSize() * 13.0f;
	return std::max(ImGui::GetFontSize() * 6.0f,
		std::min(preferred, available * 0.45f));
}

} // namespace

float DetectPixelDensity(SDL_Window* window)
{
	float density = window != nullptr ? SDL_GetWindowPixelDensity(window) : 1.0f;
	if (!(density > 0.0f))
		density = 1.0f;
	return std::min(4.0f, std::max(1.0f, density));
}

void ApplyRenderScale(SDL_Renderer* renderer, float pixel_density)
{
	if (renderer != nullptr)
		SDL_SetRenderScale(renderer, pixel_density, pixel_density);
}

FontSet LoadFonts(float pixel_density)
{
	FontSet fonts;
	const char* regular = FirstReadable(kRegularFontCandidates);
	const char* bold = FirstReadable(kBoldFontCandidates);
	const char* mono = FirstReadable(kMonoFontCandidates);
	if (regular == nullptr)
		return fonts; // Keep ImGui's built-in font.

	// Rasterize at device resolution, then scale back so every layout
	// measurement stays in points.
	fonts.body = AddFont(regular, 16.0f * pixel_density);
	fonts.small_ = AddFont(regular, 13.0f * pixel_density);
	fonts.heading = AddFont(bold != nullptr ? bold : regular, 15.0f * pixel_density);
	fonts.title = AddFont(bold != nullptr ? bold : regular, 20.0f * pixel_density);
	fonts.mono = AddFont(mono != nullptr ? mono : regular, 14.0f * pixel_density);
	if (fonts.body != nullptr) {
		ImGui::GetIO().FontDefault = fonts.body;
		ImGui::GetIO().FontGlobalScale = 1.0f / pixel_density;
	}
	return fonts;
}

void ApplyTheme()
{
	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle();

	style.WindowPadding     = ImVec2(16, 14);
	style.FramePadding      = ImVec2(10, 6);
	style.CellPadding       = ImVec2(8, 5);
	style.ItemSpacing       = ImVec2(10, 8);
	style.ItemInnerSpacing  = ImVec2(8, 6);
	style.IndentSpacing     = 18.0f;
	style.ScrollbarSize     = 12.0f;
	style.GrabMinSize       = 11.0f;

	style.WindowBorderSize  = 0.0f;
	style.ChildBorderSize   = 1.0f;
	style.PopupBorderSize   = 1.0f;
	style.FrameBorderSize   = 1.0f;
	style.TabBorderSize     = 0.0f;

	style.WindowRounding    = 0.0f;
	style.ChildRounding     = 8.0f;
	style.FrameRounding     = 5.0f;
	style.PopupRounding     = 6.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding      = 5.0f;
	style.TabRounding       = 6.0f;

	style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
	style.ButtonTextAlign   = ImVec2(0.5f, 0.5f);
	style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
	style.SeparatorTextBorderSize = 1.0f;
	style.SeparatorTextPadding = ImVec2(18, 6);

	style.AntiAliasedLines = true;
	style.AntiAliasedFill = true;

	ImVec4* colors = style.Colors;
	const ImVec4 accent = theme::ToVec4(theme::kAccent);
	const ImVec4 surface = theme::ToVec4(theme::kSurface);
	const ImVec4 surface_high = theme::ToVec4(theme::kSurfaceHigh);

	colors[ImGuiCol_Text]                   = theme::ToVec4(theme::kText);
	colors[ImGuiCol_TextDisabled]           = theme::ToVec4(theme::kTextFaint);
	colors[ImGuiCol_WindowBg]               = ImVec4(0.075f, 0.082f, 0.102f, 1.00f);
	colors[ImGuiCol_ChildBg]                = ImVec4(0.098f, 0.106f, 0.133f, 1.00f);
	colors[ImGuiCol_PopupBg]                = ImVec4(0.118f, 0.129f, 0.161f, 0.98f);
	colors[ImGuiCol_Border]                 = ImVec4(0.200f, 0.220f, 0.275f, 0.80f);
	colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg]                = ImVec4(0.145f, 0.157f, 0.196f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.184f, 0.200f, 0.251f, 1.00f);
	colors[ImGuiCol_FrameBgActive]          = ImVec4(0.216f, 0.235f, 0.294f, 1.00f);
	colors[ImGuiCol_TitleBg]                = surface;
	colors[ImGuiCol_TitleBgActive]          = surface_high;
	colors[ImGuiCol_TitleBgCollapsed]       = surface;
	colors[ImGuiCol_MenuBarBg]              = surface;
	colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.243f, 0.263f, 0.322f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.310f, 0.333f, 0.404f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive]    = accent;
	colors[ImGuiCol_CheckMark]              = accent;
	colors[ImGuiCol_SliderGrab]             = accent;
	colors[ImGuiCol_SliderGrabActive]       = ImVec4(1.00f, 0.76f, 0.36f, 1.00f);
	colors[ImGuiCol_Button]                 = ImVec4(0.176f, 0.192f, 0.243f, 1.00f);
	colors[ImGuiCol_ButtonHovered]          = ImVec4(0.235f, 0.255f, 0.318f, 1.00f);
	colors[ImGuiCol_ButtonActive]           = ImVec4(0.275f, 0.298f, 0.376f, 1.00f);
	colors[ImGuiCol_Header]                 = ImVec4(accent.x, accent.y, accent.z, 0.18f);
	colors[ImGuiCol_HeaderHovered]          = ImVec4(accent.x, accent.y, accent.z, 0.28f);
	colors[ImGuiCol_HeaderActive]           = ImVec4(accent.x, accent.y, accent.z, 0.36f);
	colors[ImGuiCol_Separator]              = ImVec4(0.200f, 0.220f, 0.275f, 0.70f);
	colors[ImGuiCol_SeparatorHovered]       = accent;
	colors[ImGuiCol_SeparatorActive]        = accent;
	colors[ImGuiCol_ResizeGrip]             = ImVec4(0.243f, 0.263f, 0.322f, 0.60f);
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4(accent.x, accent.y, accent.z, 0.60f);
	colors[ImGuiCol_ResizeGripActive]       = accent;
	colors[ImGuiCol_Tab]                    = ImVec4(0.129f, 0.141f, 0.176f, 1.00f);
	colors[ImGuiCol_TabHovered]             = ImVec4(accent.x, accent.y, accent.z, 0.30f);
	colors[ImGuiCol_TabSelected]            = ImVec4(0.196f, 0.212f, 0.267f, 1.00f);
	colors[ImGuiCol_TabDimmed]              = ImVec4(0.110f, 0.122f, 0.153f, 1.00f);
	colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.169f, 0.184f, 0.231f, 1.00f);
	colors[ImGuiCol_PlotLines]              = accent;
	colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.76f, 0.36f, 1.00f);
	colors[ImGuiCol_PlotHistogram]          = accent;
	colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.76f, 0.36f, 1.00f);
	colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.129f, 0.141f, 0.176f, 1.00f);
	colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.200f, 0.220f, 0.275f, 1.00f);
	colors[ImGuiCol_TableBorderLight]       = ImVec4(0.161f, 0.176f, 0.220f, 1.00f);
	colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.018f);
	colors[ImGuiCol_TextSelectedBg]         = ImVec4(accent.x, accent.y, accent.z, 0.35f);
	colors[ImGuiCol_DragDropTarget]         = accent;
	colors[ImGuiCol_NavCursor]              = accent;
	colors[ImGuiCol_NavWindowingHighlight]  = accent;
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.02f, 0.02f, 0.03f, 0.70f);

}

void Badge(const char* text, ImU32 color, bool filled)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 text_size = ImGui::CalcTextSize(text);
	const ImVec2 padding(7.0f, 2.0f);
	const ImVec2 size(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 corner(origin.x + size.x, origin.y + size.y);
	const float rounding = size.y * 0.5f;

	if (filled) {
		draw->AddRectFilled(origin, corner, color, rounding);
	} else {
		ImVec4 soft = ImGui::ColorConvertU32ToFloat4(color);
		soft.w = 0.16f;
		draw->AddRectFilled(origin, corner, ImGui::GetColorU32(soft), rounding);
		draw->AddRect(origin, corner, color, rounding, 0, 1.0f);
	}
	const ImU32 text_color = filled ? IM_COL32(0x14, 0x16, 0x1B, 0xFF) : color;
	draw->AddText(ImVec2(origin.x + padding.x, origin.y + padding.y), text_color, text);
	ImGui::Dummy(size);
}

void HelpMarker(const std::string& help, const char* cli_flag)
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::TextUnformatted("(?)");
	ImGui::PopStyleColor();
	if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		return;
	ImGui::BeginTooltip();
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
	ImGui::TextUnformatted(help.c_str());
	if (cli_flag != nullptr && cli_flag[0] != '\0') {
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kAccent));
		ImGui::Text("/%s", cli_flag);
		ImGui::PopStyleColor();
	}
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

bool BeginSection(const char* id, const char* title, const std::string& summary,
	int modified_count, bool* p_open, bool has_warning)
{
	ImGui::PushID(id);
	ImDrawList* draw = ImGui::GetWindowDrawList();

	// InvisibleButton asserts on a non-positive size, which a very narrow pane
	// can produce.
	const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
	const float height = ImGui::GetFrameHeight() + 6.0f;
	const ImVec2 origin = ImGui::GetCursorScreenPos();

	const bool clicked = ImGui::InvisibleButton("header", ImVec2(width, height));
	if (clicked)
		*p_open = !*p_open;
	const bool hovered = ImGui::IsItemHovered();

	ImVec4 background = ImGui::ColorConvertU32ToFloat4(theme::kSurfaceHigh);
	background.w = hovered ? 1.0f : 0.75f;
	draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
		ImGui::GetColorU32(background), 6.0f);
	// Accent spine on the left edge marks an expanded section.
	if (*p_open) {
		draw->AddRectFilled(origin, ImVec2(origin.x + 3.0f, origin.y + height),
			theme::kAccent, 2.0f);
	}

	const float text_y = origin.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
	float x = origin.x + 12.0f;

	// Disclosure triangle.
	const float arrow = ImGui::GetTextLineHeight() * 0.32f;
	const ImVec2 center(x + arrow, origin.y + height * 0.5f);
	if (*p_open) {
		draw->AddTriangleFilled(ImVec2(center.x - arrow, center.y - arrow * 0.6f),
			ImVec2(center.x + arrow, center.y - arrow * 0.6f),
			ImVec2(center.x, center.y + arrow * 0.8f), theme::kTextMuted);
	} else {
		draw->AddTriangleFilled(ImVec2(center.x - arrow * 0.6f, center.y - arrow),
			ImVec2(center.x - arrow * 0.6f, center.y + arrow),
			ImVec2(center.x + arrow * 0.8f, center.y), theme::kTextMuted);
	}
	x += arrow * 2.0f + 10.0f;

	draw->AddText(ImVec2(x, text_y), theme::kTextStrong, title);
	x += ImGui::CalcTextSize(title).x + 12.0f;

	// Badges are laid out from the right so the summary can take what is left.
	float right = origin.x + width - 12.0f;
	if (has_warning) {
		const char* mark = "!";
		const float w = ImGui::CalcTextSize(mark).x + 12.0f;
		draw->AddRectFilled(ImVec2(right - w, origin.y + 7.0f),
			ImVec2(right, origin.y + height - 7.0f), theme::kWarning, 6.0f);
		draw->AddText(ImVec2(right - w + 6.0f, text_y), IM_COL32(0x14, 0x16, 0x1B, 0xFF), mark);
		right -= w + 6.0f;
	}
	if (modified_count > 0) {
		char label[24];
		std::snprintf(label, sizeof(label), "%d", modified_count);
		const float w = ImGui::CalcTextSize(label).x + 14.0f;
		ImVec4 soft = ImGui::ColorConvertU32ToFloat4(theme::kAccent);
		soft.w = 0.20f;
		draw->AddRectFilled(ImVec2(right - w, origin.y + 7.0f),
			ImVec2(right, origin.y + height - 7.0f), ImGui::GetColorU32(soft), 6.0f);
		draw->AddText(ImVec2(right - w + 7.0f, text_y), theme::kAccent, label);
		right -= w + 6.0f;
	}

	if (!summary.empty() && right > x + 40.0f) {
		// Shorten with an ellipsis rather than slicing a word in half at the
		// clip edge, and offer the whole thing on hover.
		const float room = right - x;
		std::string shown = summary;
		if (ImGui::CalcTextSize(shown.c_str()).x > room) {
			const float ellipsis = ImGui::CalcTextSize("...").x;
			while (!shown.empty()
				&& ImGui::CalcTextSize(shown.c_str()).x + ellipsis > room) {
				shown.pop_back();
			}
			while (!shown.empty() && shown.back() == ' ')
				shown.pop_back();
			shown += "...";
		}
		draw->PushClipRect(ImVec2(x, origin.y), ImVec2(right, origin.y + height), true);
		draw->AddText(ImVec2(x, text_y), theme::kTextFaint, shown.c_str());
		draw->PopClipRect();
		if (hovered && shown != summary)
			ImGui::SetTooltip("%s", summary.c_str());
	}

	ImGui::Dummy(ImVec2(0.0f, 2.0f));
	if (!*p_open) {
		ImGui::PopID();
		return false;
	}
	ImGui::Indent(6.0f);
	return true;
}

void EndSection()
{
	ImGui::Unindent(6.0f);
	ImGui::Dummy(ImVec2(0.0f, 6.0f));
	ImGui::PopID();
}

bool BeginForm(const char* id)
{
	if (!ImGui::BeginTable(id, 2,
			ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
		return false;
	ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, LabelColumnWidth());
	ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
	return true;
}

void EndForm()
{
	ImGui::EndTable();
}

void FormRow(const char* label, const std::string& help, const char* cli_flag, bool modified)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	// Vertically centre the label against the control on the right.
	ImGui::AlignTextToFramePadding();
	if (modified) {
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		draw->AddCircleFilled(ImVec2(pos.x - 5.0f, pos.y + ImGui::GetTextLineHeight() * 0.5f),
			3.0f, theme::kAccent);
	}
	ImGui::PushStyleColor(ImGuiCol_Text,
		theme::ToVec4(modified ? theme::kTextStrong : theme::kText));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	if (!help.empty()) {
		ImGui::SameLine(0.0f, 6.0f);
		HelpMarker(help, cli_flag);
	}
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-FLT_MIN);
}

namespace {

// Draws the read-out box and leaves the cursor ready for the slider. Returns
// the width the slider should take.
float ValueBox(const char* text, float total_width)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float box_width = std::max(56.0f,
		ImGui::CalcTextSize("-100.00").x + 12.0f);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float height = ImGui::GetFrameHeight();
	draw->AddRectFilled(origin, ImVec2(origin.x + box_width, origin.y + height),
		ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
	const ImVec2 text_size = ImGui::CalcTextSize(text);
	// Right-aligned, so digits line up down a column of sliders.
	draw->AddText(ImVec2(origin.x + box_width - text_size.x - 7.0f,
		origin.y + (height - text_size.y) * 0.5f),
		ImGui::GetColorU32(ImGuiCol_Text), text);
	ImGui::Dummy(ImVec2(box_width, height));
	ImGui::SameLine(0.0f, 8.0f);
	const float remaining = ImGui::GetContentRegionAvail().x;
	if (total_width <= 0.0f)
		return remaining;
	return std::max(24.0f, std::min(remaining, total_width - box_width - 8.0f));
}

} // namespace

bool ValueSliderInt(const char* id, int* value, int min, int max,
	const char* format, float total_width)
{
	char text[64];
	std::snprintf(text, sizeof(text), format, *value);
	ImGui::PushID(id);
	const float width = ValueBox(text, total_width);
	ImGui::SetNextItemWidth(width);
	const bool changed = ImGui::SliderInt("##slider", value, min, max, "");
	ImGui::PopID();
	return changed;
}

bool ValueSliderFloat(const char* id, float* value, float min, float max,
	const char* format, ImGuiSliderFlags flags, float total_width)
{
	char text[64];
	std::snprintf(text, sizeof(text), format, *value);
	ImGui::PushID(id);
	const float width = ValueBox(text, total_width);
	ImGui::SetNextItemWidth(width);
	const bool changed = ImGui::SliderFloat("##slider", value, min, max, "", flags);
	ImGui::PopID();
	return changed;
}

void InlineNote(const char* text, ImU32 color)
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(color));
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
}

void Divider(const char* caption)
{
	if (caption == nullptr) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		return;
	}
	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::SeparatorText(caption);
	ImGui::PopStyleColor();
}

void PanelBackground(ImU32 color, float rounding)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 min = ImGui::GetWindowPos();
	const ImVec2 size = ImGui::GetWindowSize();
	draw->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), color, rounding);
}

} // namespace rc_live_ui

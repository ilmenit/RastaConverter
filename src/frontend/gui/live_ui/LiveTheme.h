#pragma once

// Visual language for the RastaConverter live UI.
//
// Everything cosmetic lives here so the screens stay about structure. The
// palette is deliberately warm-neutral with a single amber accent borrowed from
// the Atari hardware palette, and the widget helpers exist so a form row looks
// the same everywhere it is written.

#include <imgui.h>

#include <string>

struct SDL_Renderer;
struct SDL_Window;

namespace rc_live_ui {

// Semantic colours. Anything drawn by hand should take its colour from here
// rather than from a literal, so a theme change stays a one-file change.
namespace theme {

constexpr ImU32 kAccent      = IM_COL32(0xF0, 0xA8, 0x3C, 0xFF); // amber - primary
constexpr ImU32 kAccentDim   = IM_COL32(0xA8, 0x74, 0x28, 0xFF);
constexpr ImU32 kAccentSoft  = IM_COL32(0xF0, 0xA8, 0x3C, 0x28);
constexpr ImU32 kInfo        = IM_COL32(0x5C, 0xC0, 0xD8, 0xFF); // teal - secondary
constexpr ImU32 kSuccess     = IM_COL32(0x74, 0xC9, 0x70, 0xFF);
constexpr ImU32 kWarning     = IM_COL32(0xF2, 0xC1, 0x4E, 0xFF);
constexpr ImU32 kDanger      = IM_COL32(0xE4, 0x64, 0x5A, 0xFF);
constexpr ImU32 kTextStrong  = IM_COL32(0xEC, 0xEF, 0xF5, 0xFF);
constexpr ImU32 kText        = IM_COL32(0xC8, 0xCE, 0xDC, 0xFF);
constexpr ImU32 kTextMuted   = IM_COL32(0x8B, 0x93, 0xA6, 0xFF);
constexpr ImU32 kTextFaint   = IM_COL32(0x64, 0x6B, 0x7C, 0xFF);
constexpr ImU32 kSurface     = IM_COL32(0x1B, 0x1E, 0x26, 0xFF);
constexpr ImU32 kSurfaceHigh = IM_COL32(0x24, 0x28, 0x33, 0xFF);
constexpr ImU32 kSurfaceLow  = IM_COL32(0x12, 0x14, 0x1A, 0xFF);
constexpr ImU32 kBorder      = IM_COL32(0x33, 0x38, 0x46, 0xFF);

ImVec4 ToVec4(ImU32 packed);

} // namespace theme

// Fonts loaded once at startup. Any may be null if no system font was found,
// in which case the caller simply keeps the ImGui default.
struct FontSet {
	ImFont* body = nullptr;   // default UI text
	ImFont* small_ = nullptr; // captions, summaries, badges
	ImFont* heading = nullptr;// section titles
	ImFont* title = nullptr;  // screen title
	ImFont* mono = nullptr;   // command lines, numbers
};

// Installs colours, spacing and rounding. Layout units are points, never
// device pixels, so the same numbers work at any display density.
void ApplyTheme();

// Discovers a readable system UI font and builds the FontSet.
//
// `pixel_density` is device pixels per point. Glyphs are rasterized at that
// density and scaled back down through FontGlobalScale, so text is crisp on a
// HiDPI display while layout still measures in points. Falls back to the
// built-in font when no system font is installed.
FontSet LoadFonts(float pixel_density);

// Device pixels per point for `window`. 1.0 when unknown.
//
// This is SDL_GetWindowPixelDensity, not SDL_GetWindowDisplayScale: the former
// is the ratio the renderer needs, the latter is the user's preferred UI
// magnification and must not be applied to geometry that is already in points.
float DetectPixelDensity(SDL_Window* window);

// Makes one ImGui unit equal one point for `renderer`. Must be called before
// ImGui_ImplSDLRenderer3_RenderDrawData on a high-pixel-density window,
// otherwise ImGui's point-space vertices are drawn as device pixels: the UI
// renders undersized and every mouse hit-test is offset from what is drawn.
void ApplyRenderScale(SDL_Renderer* renderer, float pixel_density);

//
// ---- Widget helpers -------------------------------------------------------
//

// A pill-shaped label. Used for tier badges, counts and status.
void Badge(const char* text, ImU32 color, bool filled = false);

// "?" affordance carrying the option's help text and CLI flag.
void HelpMarker(const std::string& help, const char* cli_flag = nullptr);

// A collapsible section with a state summary in its header (design P3).
// `summary` is drawn right-aligned and dimmed; `modified_count` shows a badge.
// Returns true when the body should be drawn.
bool BeginSection(const char* id, const char* title, const std::string& summary,
	int modified_count, bool* p_open, bool has_warning = false);
void EndSection();

// Two-column form layout. Begin once per section body, then call FormRow before
// each control. Label column width is proportional so it survives resizing.
bool BeginForm(const char* id);
void EndForm();
// Starts a row and writes the label; the control goes in the next column and
// should be drawn full-width. `modified` dots the label with the accent colour.
void FormRow(const char* label, const std::string& help = std::string(),
	const char* cli_flag = nullptr, bool modified = false);

// Sliders with the value in its own box to the left, as in the design's
// wireframes. A plain ImGui slider centres its value text under the grab
// handle, where the handle hides it exactly at the values that matter most.
// `total_width` is the space the value box and the slider share; 0 takes the
// rest of the row. Pass it explicitly whenever something follows on the same
// line, since SetNextItemWidth cannot reach through to the inner slider.
bool ValueSliderInt(const char* id, int* value, int min, int max,
	const char* format = "%d", float total_width = 0.0f,
	ImGuiSliderFlags flags = 0);
bool ValueSliderFloat(const char* id, float* value, float min, float max,
	const char* format = "%.2f", ImGuiSliderFlags flags = 0,
	float total_width = 0.0f);

// Full-width note under a control. Used for P6 disabled reasons and warnings.
void InlineNote(const char* text, ImU32 color);

// Horizontal rule with optional caption, for grouping inside a section.
void Divider(const char* caption = nullptr);

// Draws a rounded panel background behind the current window's content region.
void PanelBackground(ImU32 color, float rounding = 8.0f);

} // namespace rc_live_ui

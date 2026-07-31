#pragma once

// Visual language for the RastaConverter live UI.
//
// Everything cosmetic lives here so the screens stay about structure. The
// palettes share a warm-neutral visual language with a single amber accent
// borrowed from the Atari hardware palette, and the widget helpers exist so a
// form row looks the same everywhere it is written.

#include <imgui.h>

#include <string>

struct SDL_Renderer;
struct SDL_Window;

namespace rc_live_ui {

// Semantic colours. Anything drawn by hand should take its colour from here
// rather than from a literal, so a theme change stays a one-file change.
namespace theme {

// These are variables because every custom-drawn label, chart and badge must
// follow the selected theme too. ApplyTheme updates them as one semantic set.
extern ImU32 kAccent;      // amber - primary
extern ImU32 kAccentDim;
extern ImU32 kAccentSoft;
extern ImU32 kInfo;        // teal/blue - secondary
extern ImU32 kSuccess;
extern ImU32 kWarning;
extern ImU32 kDanger;
extern ImU32 kTextStrong;
extern ImU32 kText;
extern ImU32 kTextMuted;
extern ImU32 kTextFaint;
extern ImU32 kSurface;
extern ImU32 kSurfaceHigh;
extern ImU32 kSurfaceLow;
extern ImU32 kBorder;

// Recent-card badge palette. Background and foreground move together with the
// selected theme; general-purpose kTextStrong becomes dark in the Light theme
// and therefore cannot safely be used over coloured badge backgrounds.
extern ImU32 kBadgeLabel;
extern ImU32 kBadgeText;   // green - ANTIC 4
extern ImU32 kBadgeGfx;    // blue - ANTIC E
extern ImU32 kBadgeWide;   // violet - wide playfield
extern ImU32 kBadgeNormal; // amber - normal playfield

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

enum class UiTheme {
	Dark = 0,
	HighContrast,
	Light
};

UiTheme CurrentUiTheme();
const char* UiThemeName(UiTheme selected);
void SetUiTheme(UiTheme selected);

// Discovers a readable system UI font and builds the FontSet.
//
// `pixel_density` is device pixels per point. Glyphs are rasterized at that
// density and scaled back down through FontGlobalScale, so text is crisp on a
// HiDPI display while layout still measures in points. Falls back to the
// built-in font when no system font is installed.
FontSet LoadFonts(float pixel_density);

// User-selected UI magnification. The default is 1.0 and the supported range
// is 1.0-2.0; larger values make the fixed top-level toolbars wider than a
// normal desktop window. The value is persisted in SDL's per-user preference
// directory and applies to Setup, Dashboard, Recent and the paint editor.
float UiFontScale();
void SetUiFontScale(float scale);

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

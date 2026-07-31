#include "LiveTheme.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace rc_live_ui {

namespace theme {

ImU32 kAccent      = IM_COL32(0xF0, 0xA8, 0x3C, 0xFF);
ImU32 kAccentDim   = IM_COL32(0xB5, 0x7A, 0x25, 0xFF);
ImU32 kAccentSoft  = IM_COL32(0xF0, 0xA8, 0x3C, 0x28);
ImU32 kInfo        = IM_COL32(0x65, 0xC7, 0xDE, 0xFF);
ImU32 kSuccess     = IM_COL32(0x83, 0xD2, 0x7E, 0xFF);
ImU32 kWarning     = IM_COL32(0xF4, 0xC8, 0x5B, 0xFF);
ImU32 kDanger      = IM_COL32(0xEF, 0x78, 0x70, 0xFF);
ImU32 kTextStrong  = IM_COL32(0xF5, 0xF7, 0xFB, 0xFF);
ImU32 kText        = IM_COL32(0xD8, 0xDE, 0xE9, 0xFF);
ImU32 kTextMuted   = IM_COL32(0xAE, 0xB7, 0xC9, 0xFF);
ImU32 kTextFaint   = IM_COL32(0x91, 0x9C, 0xB1, 0xFF);
ImU32 kSurface     = IM_COL32(0x1B, 0x1F, 0x29, 0xFF);
ImU32 kSurfaceHigh = IM_COL32(0x25, 0x2B, 0x38, 0xFF);
ImU32 kSurfaceLow  = IM_COL32(0x12, 0x15, 0x1C, 0xFF);
ImU32 kBorder      = IM_COL32(0x4B, 0x55, 0x68, 0xFF);
ImU32 kBadgeLabel  = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
ImU32 kBadgeText   = IM_COL32(0x26, 0x7A, 0x49, 0xFF);
ImU32 kBadgeGfx    = IM_COL32(0x28, 0x6F, 0xAE, 0xFF);
ImU32 kBadgeWide   = IM_COL32(0x76, 0x56, 0xB5, 0xFF);
ImU32 kBadgeNormal = IM_COL32(0xA3, 0x62, 0x12, 0xFF);

ImVec4 ToVec4(ImU32 packed)
{
	return ImGui::ColorConvertU32ToFloat4(packed);
}

} // namespace theme

namespace {

constexpr float kMinimumFontScale = 1.0f;
constexpr float kMaximumFontScale = 2.0f;
float g_pixel_density = 1.0f;
float g_font_scale = 0.0f; // zero means not loaded yet
int g_ui_theme = -1; // negative means not loaded yet

std::string PreferencePath(const char* filename)
{
	char* pref = SDL_GetPrefPath("RastaConverter", "RastaConverter");
	if (pref == nullptr)
		return std::string();
	const std::string path = std::string(pref) + filename;
	SDL_free(pref);
	return path;
}

float LoadFontScale()
{
	if (const char* override_scale = SDL_getenv("RASTA_UI_FONT_SCALE")) {
		const float value = std::strtof(override_scale, nullptr);
		if (value > 0.0f) {
			return std::max(kMinimumFontScale,
				std::min(kMaximumFontScale, value));
		}
	}
	const std::string path = PreferencePath("ui-font-scale.txt");
	if (path.empty())
		return 1.0f;
	size_t size = 0;
	void* data = SDL_LoadFile(path.c_str(), &size);
	if (data == nullptr)
		return 1.0f;
	const std::string text(static_cast<const char*>(data), size);
	SDL_free(data);
	const float value = std::strtof(text.c_str(), nullptr);
	return std::max(kMinimumFontScale, std::min(kMaximumFontScale,
		value > 0.0f ? value : 1.0f));
}

void SaveFontScale(float scale)
{
	const std::string path = PreferencePath("ui-font-scale.txt");
	if (path.empty())
		return;
	char text[32];
	const int length = std::snprintf(text, sizeof(text), "%.2f\n", scale);
	if (length > 0)
		SDL_SaveFile(path.c_str(), text, static_cast<size_t>(length));
}

UiTheme ParseTheme(const std::string& text)
{
	std::string value = text;
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (value.find("high") != std::string::npos || value == "1")
		return UiTheme::HighContrast;
	if (value.find("light") != std::string::npos || value == "2")
		return UiTheme::Light;
	return UiTheme::Dark;
}

UiTheme LoadUiTheme()
{
	if (const char* override_theme = SDL_getenv("RASTA_UI_THEME"))
		return ParseTheme(override_theme);
	const std::string path = PreferencePath("ui-theme.txt");
	if (path.empty())
		return UiTheme::Dark;
	size_t size = 0;
	void* data = SDL_LoadFile(path.c_str(), &size);
	if (data == nullptr)
		return UiTheme::Dark;
	const std::string text(static_cast<const char*>(data), size);
	SDL_free(data);
	return ParseTheme(text);
}

void SaveUiTheme(UiTheme selected)
{
	const std::string path = PreferencePath("ui-theme.txt");
	if (path.empty())
		return;
	const char* name = UiThemeName(selected);
	SDL_SaveFile(path.c_str(), name, std::strlen(name));
}

void ApplySemanticPalette(UiTheme selected)
{
	using namespace theme;
	if (selected == UiTheme::Light) {
		kAccent      = IM_COL32(0xD9, 0x8A, 0x18, 0xFF);
		kAccentDim   = IM_COL32(0x8A, 0x54, 0x08, 0xFF);
		kAccentSoft  = IM_COL32(0xD9, 0x8A, 0x18, 0x28);
		kInfo        = IM_COL32(0x0B, 0x68, 0x7D, 0xFF);
		kSuccess     = IM_COL32(0x2D, 0x72, 0x33, 0xFF);
		kWarning     = IM_COL32(0x83, 0x58, 0x00, 0xFF);
		kDanger      = IM_COL32(0xB1, 0x32, 0x2B, 0xFF);
		kTextStrong  = IM_COL32(0x18, 0x1B, 0x22, 0xFF);
		kText        = IM_COL32(0x29, 0x2E, 0x38, 0xFF);
		kTextMuted   = IM_COL32(0x4F, 0x59, 0x6A, 0xFF);
		kTextFaint   = IM_COL32(0x65, 0x70, 0x82, 0xFF);
		kSurface     = IM_COL32(0xF4, 0xF2, 0xED, 0xFF);
		kSurfaceHigh = IM_COL32(0xFF, 0xFE, 0xFA, 0xFF);
		kSurfaceLow  = IM_COL32(0xE7, 0xE4, 0xDD, 0xFF);
		kBorder      = IM_COL32(0xA9, 0xA6, 0x9E, 0xFF);
		kBadgeLabel  = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
		kBadgeText   = IM_COL32(0x17, 0x66, 0x3A, 0xFF);
		kBadgeGfx    = IM_COL32(0x17, 0x5D, 0x9C, 0xFF);
		kBadgeWide   = IM_COL32(0x67, 0x41, 0x9B, 0xFF);
		kBadgeNormal = IM_COL32(0x8A, 0x4E, 0x00, 0xFF);
		return;
	}
	if (selected == UiTheme::HighContrast) {
		kAccent      = IM_COL32(0xFF, 0xB5, 0x38, 0xFF);
		kAccentDim   = IM_COL32(0xD6, 0x8E, 0x1D, 0xFF);
		kAccentSoft  = IM_COL32(0xFF, 0xB5, 0x38, 0x30);
		kInfo        = IM_COL32(0x78, 0xD9, 0xEF, 0xFF);
		kSuccess     = IM_COL32(0x93, 0xE2, 0x8D, 0xFF);
		kWarning     = IM_COL32(0xFF, 0xD2, 0x63, 0xFF);
		kDanger      = IM_COL32(0xFF, 0x87, 0x7E, 0xFF);
		kTextStrong  = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
		kText        = IM_COL32(0xF0, 0xF3, 0xF8, 0xFF);
		kTextMuted   = IM_COL32(0xD0, 0xD7, 0xE3, 0xFF);
		kTextFaint   = IM_COL32(0xB2, 0xBD, 0xCF, 0xFF);
		kSurface     = IM_COL32(0x10, 0x14, 0x1A, 0xFF);
		kSurfaceHigh = IM_COL32(0x1A, 0x21, 0x2B, 0xFF);
		kSurfaceLow  = IM_COL32(0x07, 0x09, 0x0D, 0xFF);
		kBorder      = IM_COL32(0x68, 0x75, 0x8C, 0xFF);
		kBadgeLabel  = IM_COL32(0x08, 0x0B, 0x10, 0xFF);
		kBadgeText   = IM_COL32(0x78, 0xED, 0x9A, 0xFF);
		kBadgeGfx    = IM_COL32(0x6B, 0xC7, 0xFF, 0xFF);
		kBadgeWide   = IM_COL32(0xD5, 0xA6, 0xFF, 0xFF);
		kBadgeNormal = IM_COL32(0xFF, 0xD1, 0x66, 0xFF);
		return;
	}
	kAccent      = IM_COL32(0xF0, 0xA8, 0x3C, 0xFF);
	kAccentDim   = IM_COL32(0xB5, 0x7A, 0x25, 0xFF);
	kAccentSoft  = IM_COL32(0xF0, 0xA8, 0x3C, 0x28);
	kInfo        = IM_COL32(0x65, 0xC7, 0xDE, 0xFF);
	kSuccess     = IM_COL32(0x83, 0xD2, 0x7E, 0xFF);
	kWarning     = IM_COL32(0xF4, 0xC8, 0x5B, 0xFF);
	kDanger      = IM_COL32(0xEF, 0x78, 0x70, 0xFF);
	kTextStrong  = IM_COL32(0xF5, 0xF7, 0xFB, 0xFF);
	kText        = IM_COL32(0xD8, 0xDE, 0xE9, 0xFF);
	kTextMuted   = IM_COL32(0xAE, 0xB7, 0xC9, 0xFF);
	kTextFaint   = IM_COL32(0x91, 0x9C, 0xB1, 0xFF);
	kSurface     = IM_COL32(0x1B, 0x1F, 0x29, 0xFF);
	kSurfaceHigh = IM_COL32(0x25, 0x2B, 0x38, 0xFF);
	kSurfaceLow  = IM_COL32(0x12, 0x15, 0x1C, 0xFF);
	kBorder      = IM_COL32(0x4B, 0x55, 0x68, 0xFF);
	kBadgeLabel  = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
	kBadgeText   = IM_COL32(0x26, 0x7A, 0x49, 0xFF);
	kBadgeGfx    = IM_COL32(0x28, 0x6F, 0xAE, 0xFF);
	kBadgeWide   = IM_COL32(0x76, 0x56, 0xB5, 0xFF);
	kBadgeNormal = IM_COL32(0xA3, 0x62, 0x12, 0xFF);
}

ImVec4 Rgba(unsigned red, unsigned green, unsigned blue, unsigned alpha = 255)
{
	return ImVec4(red / 255.0f, green / 255.0f, blue / 255.0f,
		alpha / 255.0f);
}

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
	g_pixel_density = std::max(1.0f, pixel_density);
	FontSet fonts;
	const char* regular = FirstReadable(kRegularFontCandidates);
	const char* bold = FirstReadable(kBoldFontCandidates);
	const char* mono = FirstReadable(kMonoFontCandidates);
	if (regular == nullptr) {
		ImGui::GetIO().FontGlobalScale = UiFontScale() / g_pixel_density;
		return fonts; // Keep ImGui's built-in font.
	}

	// Rasterize at device resolution, then scale back so every layout
	// measurement stays in points.
	fonts.body = AddFont(regular, 16.0f * pixel_density);
	fonts.small_ = AddFont(regular, 13.0f * pixel_density);
	fonts.heading = AddFont(bold != nullptr ? bold : regular, 15.0f * pixel_density);
	fonts.title = AddFont(bold != nullptr ? bold : regular, 20.0f * pixel_density);
	fonts.mono = AddFont(mono != nullptr ? mono : regular, 14.0f * pixel_density);
	if (fonts.body != nullptr) {
		ImGui::GetIO().FontDefault = fonts.body;
		ImGui::GetIO().FontGlobalScale = UiFontScale() / g_pixel_density;
	}
	return fonts;
}

float UiFontScale()
{
	if (!(g_font_scale > 0.0f))
		g_font_scale = LoadFontScale();
	return g_font_scale;
}

void SetUiFontScale(float scale)
{
	const float clamped =
		std::max(kMinimumFontScale, std::min(kMaximumFontScale, scale));
	if (clamped == UiFontScale())
		return;
	g_font_scale = clamped;
	if (ImGui::GetCurrentContext() != nullptr)
		ImGui::GetIO().FontGlobalScale = g_font_scale / g_pixel_density;
	SaveFontScale(g_font_scale);
}

UiTheme CurrentUiTheme()
{
	if (g_ui_theme < 0)
		g_ui_theme = static_cast<int>(LoadUiTheme());
	return static_cast<UiTheme>(g_ui_theme);
}

const char* UiThemeName(UiTheme selected)
{
	switch (selected) {
	case UiTheme::HighContrast: return "High contrast";
	case UiTheme::Light: return "Light";
	case UiTheme::Dark:
	default: return "Dark";
	}
}

void SetUiTheme(UiTheme selected)
{
	if (selected == CurrentUiTheme())
		return;
	g_ui_theme = static_cast<int>(selected);
	ApplySemanticPalette(selected);
	if (ImGui::GetCurrentContext() != nullptr)
		ApplyTheme();
	SaveUiTheme(selected);
}

void ApplyTheme()
{
	const UiTheme selected = CurrentUiTheme();
	ApplySemanticPalette(selected);
	const bool light = selected == UiTheme::Light;
	const bool high_contrast = selected == UiTheme::HighContrast;

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
	colors[ImGuiCol_WindowBg] = light ? Rgba(244, 242, 237)
		: high_contrast ? Rgba(7, 9, 13) : Rgba(18, 21, 28);
	colors[ImGuiCol_ChildBg] = light ? Rgba(255, 254, 250)
		: high_contrast ? Rgba(16, 20, 26) : Rgba(27, 31, 41);
	colors[ImGuiCol_PopupBg] = light ? Rgba(255, 255, 252, 252)
		: high_contrast ? Rgba(22, 28, 36, 252) : Rgba(31, 36, 47, 252);
	colors[ImGuiCol_Border] = theme::ToVec4(theme::kBorder);
	colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = light ? Rgba(228, 225, 217)
		: high_contrast ? Rgba(30, 38, 49) : Rgba(38, 44, 57);
	colors[ImGuiCol_FrameBgHovered] = light ? Rgba(216, 211, 201)
		: high_contrast ? Rgba(44, 55, 70) : Rgba(50, 58, 74);
	colors[ImGuiCol_FrameBgActive] = light ? Rgba(203, 197, 185)
		: high_contrast ? Rgba(57, 70, 89) : Rgba(61, 70, 90);
	colors[ImGuiCol_TitleBg]                = surface;
	colors[ImGuiCol_TitleBgActive]          = surface_high;
	colors[ImGuiCol_TitleBgCollapsed]       = surface;
	colors[ImGuiCol_MenuBarBg]              = surface;
	colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_ScrollbarGrab] = light ? Rgba(166, 162, 153)
		: high_contrast ? Rgba(91, 104, 125) : Rgba(72, 81, 101);
	colors[ImGuiCol_ScrollbarGrabHovered] = light ? Rgba(134, 129, 119)
		: high_contrast ? Rgba(121, 136, 160) : Rgba(94, 105, 129);
	colors[ImGuiCol_ScrollbarGrabActive]    = accent;
	colors[ImGuiCol_CheckMark]              = accent;
	colors[ImGuiCol_SliderGrab]             = accent;
	colors[ImGuiCol_SliderGrabActive]       = theme::ToVec4(theme::kAccentDim);
	colors[ImGuiCol_Button] = light ? Rgba(222, 218, 209)
		: high_contrast ? Rgba(34, 43, 55) : Rgba(43, 50, 65);
	colors[ImGuiCol_ButtonHovered] = light ? Rgba(207, 201, 190)
		: high_contrast ? Rgba(52, 64, 81) : Rgba(58, 67, 86);
	colors[ImGuiCol_ButtonActive] = light ? Rgba(192, 184, 170)
		: high_contrast ? Rgba(66, 81, 102) : Rgba(70, 80, 102);
	colors[ImGuiCol_Header]                 = ImVec4(accent.x, accent.y, accent.z, 0.18f);
	colors[ImGuiCol_HeaderHovered]          = ImVec4(accent.x, accent.y, accent.z, 0.28f);
	colors[ImGuiCol_HeaderActive]           = ImVec4(accent.x, accent.y, accent.z, 0.36f);
	colors[ImGuiCol_Separator]              = theme::ToVec4(theme::kBorder);
	colors[ImGuiCol_SeparatorHovered]       = accent;
	colors[ImGuiCol_SeparatorActive]        = accent;
	colors[ImGuiCol_ResizeGrip]             = colors[ImGuiCol_ScrollbarGrab];
	colors[ImGuiCol_ResizeGripHovered]      = ImVec4(accent.x, accent.y, accent.z, 0.60f);
	colors[ImGuiCol_ResizeGripActive]       = accent;
	colors[ImGuiCol_Tab] = light ? Rgba(231, 227, 219)
		: high_contrast ? Rgba(24, 30, 39) : Rgba(33, 38, 50);
	colors[ImGuiCol_TabHovered]             = ImVec4(accent.x, accent.y, accent.z, 0.30f);
	colors[ImGuiCol_TabSelected] = light ? Rgba(211, 205, 194)
		: high_contrast ? Rgba(48, 59, 75) : Rgba(50, 57, 73);
	colors[ImGuiCol_TabDimmed] = light ? Rgba(238, 235, 229)
		: high_contrast ? Rgba(17, 22, 29) : Rgba(28, 32, 42);
	colors[ImGuiCol_TabDimmedSelected] = light ? Rgba(221, 216, 206)
		: high_contrast ? Rgba(38, 47, 61) : Rgba(43, 49, 63);
	colors[ImGuiCol_PlotLines]              = accent;
	colors[ImGuiCol_PlotLinesHovered]       = theme::ToVec4(theme::kAccentDim);
	colors[ImGuiCol_PlotHistogram]          = accent;
	colors[ImGuiCol_PlotHistogramHovered]   = theme::ToVec4(theme::kAccentDim);
	colors[ImGuiCol_TableHeaderBg]          = colors[ImGuiCol_Tab];
	colors[ImGuiCol_TableBorderStrong]      = theme::ToVec4(theme::kBorder);
	colors[ImGuiCol_TableBorderLight]       = colors[ImGuiCol_FrameBgHovered];
	colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt] = light
		? ImVec4(0.0f, 0.0f, 0.0f, 0.025f)
		: ImVec4(1.0f, 1.0f, 1.0f, high_contrast ? 0.035f : 0.025f);
	colors[ImGuiCol_TextSelectedBg]         = ImVec4(accent.x, accent.y, accent.z, 0.35f);
	colors[ImGuiCol_DragDropTarget]         = accent;
	colors[ImGuiCol_NavCursor]              = accent;
	colors[ImGuiCol_NavWindowingHighlight]  = accent;
	colors[ImGuiCol_NavWindowingDimBg] = light
		? ImVec4(0.15f, 0.14f, 0.12f, 0.35f)
		: ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
	colors[ImGuiCol_ModalWindowDimBg] = light
		? ImVec4(0.15f, 0.14f, 0.12f, 0.45f)
		: ImVec4(0.02f, 0.02f, 0.03f, 0.72f);

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

// Width of the read-out field. Wide enough for the longest value any form row
// shows ("-100.00", "50000"), so a column of them lines up.
float ValueBoxWidth()
{
	return std::max(64.0f, ImGui::CalcTextSize("-100.00").x + 16.0f);
}

// Space left for the slider once the field has taken its share.
float SliderWidth(float total_width)
{
	const float remaining = ImGui::GetContentRegionAvail().x;
	if (total_width <= 0.0f)
		return remaining;
	return std::max(24.0f, std::min(remaining,
		total_width - ValueBoxWidth() - 8.0f));
}

} // namespace

// A slider with its value in an editable field beside it, rather than as text
// inside the track where the grab covers it. The field is the reason a slider
// can have a wide range without becoming useless: a range that needs a
// logarithmic scale to be draggable is still exact to type into.
//
// The field commits on Enter or when it loses focus, not on every keystroke -
// otherwise typing "50" into a field whose minimum is 1 would clamp the "5"
// away before the "0" arrived, and half-typed numbers would reach the
// conversion and rebuild the preview.
bool ValueSliderInt(const char* id, int* value, int min, int max,
	const char* format, float total_width, ImGuiSliderFlags flags)
{
	ImGui::PushID(id);
	bool changed = false;

	int typed = *value;
	ImGui::SetNextItemWidth(ValueBoxWidth());
	ImGui::InputInt("##value", &typed, 0, 0,
		ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsDecimal);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		const int clamped = std::max(min, std::min(max, typed));
		if (clamped != *value) {
			*value = clamped;
			changed = true;
		}
	}
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
		char range[96];
		std::snprintf(range, sizeof(range), "Type a value between %d and %d.",
			min, max);
		ImGui::SetTooltip("%s", range);
	}

	ImGui::SameLine(0.0f, 8.0f);
	ImGui::SetNextItemWidth(SliderWidth(total_width));
	// The format is spent on the field, so the track carries no text.
	(void)format;
	changed |= ImGui::SliderInt("##slider", value, min, max, "",
		flags | ImGuiSliderFlags_AlwaysClamp);
	ImGui::PopID();
	return changed;
}

bool ValueSliderFloat(const char* id, float* value, float min, float max,
	const char* format, ImGuiSliderFlags flags, float total_width)
{
	ImGui::PushID(id);
	bool changed = false;

	float typed = *value;
	ImGui::SetNextItemWidth(ValueBoxWidth());
	ImGui::InputFloat("##value", &typed, 0.0f, 0.0f, format,
		ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsScientific);
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		const float clamped = std::max(min, std::min(max, typed));
		if (clamped != *value) {
			*value = clamped;
			changed = true;
		}
	}
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
		char range[128];
		std::string pattern = "Type a value between ";
		pattern += format;
		pattern += " and ";
		pattern += format;
		pattern += ".";
		std::snprintf(range, sizeof(range), pattern.c_str(), min, max);
		ImGui::SetTooltip("%s", range);
	}

	ImGui::SameLine(0.0f, 8.0f);
	ImGui::SetNextItemWidth(SliderWidth(total_width));
	changed |= ImGui::SliderFloat("##slider", value, min, max, "",
		flags | ImGuiSliderFlags_AlwaysClamp);
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

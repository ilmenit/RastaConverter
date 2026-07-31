#pragma once

#include <string>
#include <string_view>

namespace rc_live_ui {

// Application-level choices that describe the editor itself, not an image
// conversion. Conversion recipes and optimizer state deliberately do not own
// these values.
struct UiPreferences {
	bool run_subfolder = true;
	int setup_window_width = 1420;
	int setup_window_height = 900;
	float setup_form_width = 560.0f;
	bool setup_only_modified = false;
	unsigned setup_open_sections = 0x5u; // Source and Colour.
};

UiPreferences ParseUiPreferences(std::string_view text);
std::string SerializeUiPreferences(const UiPreferences& preferences);

// Stored in SDL's per-user preference directory beside the existing theme,
// font-scale, and recent-run files. Missing or malformed values use defaults.
UiPreferences LoadUiPreferences();
bool SaveUiPreferences(const UiPreferences& preferences);

} // namespace rc_live_ui

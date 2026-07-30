#pragma once

// What the two halves of the setup screen share.
//
// SetupScreen.cpp owns the window: the title bar, the form column, the bottom
// bar and the run loop. SetupSections.cpp owns the bodies of the collapsible
// sections and their collapsed-state summaries - the part that grows every time
// an option is added, and the reason the file was worth splitting.
//
// Nothing here is meant for the rest of the live UI, hence the nested namespace
// and the header living next to its two users rather than with the public ones.

#include <array>
#include <cstring>
#include <string>

#include <imgui.h>

#include "ConfigModel.h"
#include "FileDialog.h"
#include "LiveTheme.h"
#include "config.h"

struct SDL_Window;

namespace rc_live_ui {
namespace setup {

constexpr int kCategoryCount = static_cast<int>(Category::Count);

// A fixed-size editable text field backed by std::string.
struct TextField {
	std::array<char, 1024> buffer{};

	void Set(const std::string& value)
	{
		const size_t length = std::min(value.size(), buffer.size() - 1);
		std::memcpy(buffer.data(), value.data(), length);
		buffer[length] = '\0';
	}
	std::string Get() const { return std::string(buffer.data()); }
};

struct SetupState {
	Configuration* cfg = nullptr;

	TextField input;
	TextField output;
	TextField palette;
	TextField details;
	TextField onoff;

	std::array<char, 128> search{};
	bool only_modified = false;
	bool section_open[kCategoryCount] = {};

	bool height_auto = true;
	bool antic_e_dual_mode = false;
	int input_width = 0;
	int input_height = 0;
	bool seed_random = true;
	int seed_value = 0;
	bool max_evals_unlimited = true;
	unsigned long long max_evals_value = 100000000ULL;
	bool autosave_auto = true;
	int autosave_value = 100000;
	int cache_mb = 64;
	int solutions = 1;

	bool output_touched = false; // user typed a name, stop deriving it

	// The form keeps the smaller share; the image is the point of the screen.
	float form_width = 560.0f;

	std::string copied_notice;
	double copied_at = 0.0;
	// Set when the run folder could not be created, so Convert can refuse
	// rather than let the conversion fail later on every save.
	std::string folder_error;

	// The recent-conversions browser takes over the viewer column while it is
	// open; it opens by itself when there is no image to preview yet.
	bool show_recent = false;
	bool recent_dismissed = false;
	bool recent_refresh = false;

	// Drag-and-drop feedback. The image row is the drop target, so it has to
	// know where the pointer is while a drag is in progress.
	bool drag_active = false;
	bool drag_inside_zone = false;
	ImVec2 drag_point = ImVec2(-1.0f, -1.0f);
	ImVec4 drop_zone = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // screen-space rect
};

// ---- enablement rules (design §7.3) --------------------------------------

struct Rules {
	bool dual_on = false;
	bool has_mask = false;
	bool objective_available = true;
	bool details_available = true;
};

Rules EvaluateRules(const Configuration& cfg);

// Wraps a group in a disabled state and explains why (design P2, P6).
struct DisabledGroup {
	bool active = false;
	explicit DisabledGroup(bool disabled, const char* reason) : active(disabled)
	{
		if (!active)
			return;
		InlineNote(reason, theme::kWarning);
		ImGui::Spacing();
		ImGui::BeginDisabled();
	}
	~DisabledGroup()
	{
		if (active)
			ImGui::EndDisabled();
	}
};

// ---- form helpers ---------------------------------------------------------

std::string FileName(const std::string& path);

// The option table entry for `id`. Every id used by the form is present; a miss
// is a build-time bug and yields an empty descriptor.
const OptionDesc& Opt(const char* id);

// Starts a form row for `id`, writing its label, help and modified marker.
// Returns false when the row is filtered out, in which case the caller skips
// its control.
bool Row(const char* id, const Configuration& cfg);

// True when a search term or the modified-only toggle is narrowing the form.
bool FilterActive();

bool ComboTokens(const char* id, int* value, const char* const* labels, int count);

// A path field with a native Browse button. Returns true when the text changed.
// `total_width` is the space the field and its button share; pass 0 to take
// whatever is left on the row. Sizing it explicitly matters wherever something
// follows on the same line, which is otherwise pushed off the edge.
bool PathField(const char* id, TextField& field, FileDialogs& dialogs,
	SDL_Window* window, FileDialogs::Kind kind, bool save, const char* placeholder,
	float total_width = 0.0f);

// ---- section bodies (SetupSections.cpp) -----------------------------------

void DrawSourceSection(SetupState& state, FileDialogs& dialogs, SDL_Window* window);
void DrawColourSection(SetupState& state);
void DrawDetailsSection(SetupState& state, const Rules& rules, FileDialogs& dialogs,
	SDL_Window* window);

// The Algorithm section, and the three groups it is made of. The groups are
// declared here so they can be defined in the order they read best rather than
// the order they are called in.
void DrawAlgorithmSection(SetupState& state, const Rules& rules,
	FileDialogs& dialogs, SDL_Window* window);
void DrawDualGroup(SetupState& state);
void DrawSearchGroup(SetupState& state, FileDialogs& dialogs, SDL_Window* window);
void DrawObjectiveGroup(SetupState& state, const Rules& rules);

// A section that is closed still has to say what it is currently set to, so
// the whole configuration can be audited by reading headers (design P3).
std::string SectionSummary(Category category, const Configuration& cfg,
	const SetupState& state);

} // namespace setup
} // namespace rc_live_ui

#include "SetupScreen.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "ConfigModel.h"
#include "FileDialog.h"
#include "ImageViewer.h"
#include "LiveTheme.h"
#include "RecentGallery.h"
#include "RecentRuns.h"
#include "TargetPreview.h"

namespace rc_live_ui {

namespace {

constexpr int kCategoryCount = static_cast<int>(Category::Count);

// Below this width the form and a usable image cannot coexist (design §7.1d).
constexpr float kOverlayViewerBreakpoint = 850.0f;

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

std::string FileName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string DirectoryOf(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Bundled resources are addressed relative to the working directory, which is
// wrong as soon as the program is launched from a desktop shortcut or a file
// manager. Falls back to the directory the executable lives in.
std::string ResolveResourcePath(const std::string& path)
{
	if (path.empty() || SDL_GetPathInfo(path.c_str(), nullptr))
		return path;
	const char* base = SDL_GetBasePath();
	if (base == nullptr)
		return path;
	const std::string candidate = std::string(base) + path;
	if (SDL_GetPathInfo(candidate.c_str(), nullptr))
		return candidate;
	return path;
}

// A path as the user gave it, made absolute. The recipe stored in the .opt
// header outlives the working directory it was typed in - Reuse and resume both
// read it back from wherever the program happens to be running next - so a
// relative /details or /pal would silently fail to load later.
std::string AbsolutePath(const std::string& path)
{
	if (path.empty() || path[0] == '/' || path[0] == '\\')
		return path;
#if defined(_WIN32)
	if (path.size() >= 2 && path[1] == ':')
		return path;
#endif
	char* cwd = SDL_GetCurrentDirectory();
	if (cwd == nullptr)
		return path;
	std::string result = std::string(cwd) + path; // SDL terminates it with a separator
	SDL_free(cwd);
	return result;
}

// Fixes every path of an accepted job to an absolute one, so the run folder,
// the history index and the recorded recipe all mean the same thing no matter
// where the program is started from next time.
void AbsolutizeJobPaths(Configuration& cfg)
{
	cfg.input_file = AbsolutePath(cfg.input_file);
	cfg.output_file = AbsolutePath(cfg.output_file);
	cfg.details_file = AbsolutePath(cfg.details_file);
	cfg.on_off_file = AbsolutePath(cfg.on_off_file);
	// The bundled palette is addressed relative to the install directory, not
	// the working directory, so it has to be found before it can be absolute.
	cfg.palette_file = AbsolutePath(ResolveResourcePath(cfg.palette_file));
}

// Each conversion writes into its own folder beside the input image, so a
// working folder never fills up with a dozen artifacts per attempt and a run
// stays a single self-contained thing. See RecentRuns.h.
std::string DeriveOutputName(const std::string& input)
{
	return AllocateRunOutputPath(input);
}

// Artifacts a run writes, checked so an accidental overwrite is visible before
// it happens rather than discovered hours later.
bool OutputArtifactsExist(const std::string& output, std::string* found)
{
	static const char* const kSuffixes[] = {
		".opt", ".mic", ".opt.h", ".opt.ini", ".pmg", "-dst.png"};
	if (output.empty())
		return false;
	for (const char* suffix : kSuffixes) {
		const std::string candidate = output + suffix;
		if (SDL_GetPathInfo(candidate.c_str(), nullptr)) {
			if (found != nullptr)
				*found = FileName(candidate);
			return true;
		}
	}
	return false;
}

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

	// Drag-and-drop feedback. The image row is the drop target, so it has to
	// know where the pointer is while a drag is in progress.
	// The recent-conversions browser takes over the viewer column while it is
	// open; it opens by itself when there is no image to preview yet.
	bool show_recent = false;
	bool recent_dismissed = false;
	bool recent_refresh = false;

	bool drag_active = false;
	bool drag_inside_zone = false;
	ImVec2 drag_point = ImVec2(-1.0f, -1.0f);
	ImVec4 drop_zone = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // screen-space rect
};

double NowMs()
{
	return static_cast<double>(SDL_GetTicksNS()) / 1.0e6;
}

// Several widgets keep their own view of a Configuration field - a checkbox
// standing in for a sentinel value, or a value in different units. This brings
// all of them back in line with the configuration, and runs both at startup and
// whenever a whole configuration is loaded at once.
void SyncStateFromConfig(SetupState& state)
{
	Configuration& cfg = *state.cfg;
	state.input.Set(cfg.input_file);
	state.output.Set(cfg.output_file);
	state.palette.Set(cfg.palette_file);
	state.details.Set(cfg.details_file);
	state.onoff.Set(cfg.on_off_file);

	state.height_auto = cfg.height <= 0;
	state.cache_mb = std::max(1, cfg.cache_size / (1024 * 1024));
	state.solutions = GetSolutions();

	const Configuration defaults = DefaultConfiguration();
	state.max_evals_unlimited = cfg.max_evals >= defaults.max_evals;
	if (!state.max_evals_unlimited)
		state.max_evals_value = cfg.max_evals;
	state.autosave_auto = cfg.save_period < 0;
	if (!state.autosave_auto)
		state.autosave_value = cfg.save_period;

	state.seed_random = cfg.initial_seed == 0;
	if (!state.seed_random)
		state.seed_value = static_cast<int>(cfg.initial_seed);
}

// ---- form helpers ---------------------------------------------------------

const OptionDesc& Opt(const char* id)
{
	const OptionDesc* option = FindOption(id);
	// Every id used below is present in the table; a miss is a build-time bug.
	static const OptionDesc fallback{};
	return option != nullptr ? *option : fallback;
}

// The search box filters the live form rather than listing matches separately,
// so a match is the real control, ready to edit. Held here because every row
// has to consult it and threading it through each section body would be noise.
const std::string* g_filter = nullptr;
bool g_only_modified = false;

bool RowMatchesFilter(const OptionDesc& option, const Configuration& cfg)
{
	// Options with a permanent control elsewhere are never rows in the form.
	if (!option.in_form)
		return false;
	// An option with no control right now must not be offered by the search
	// either, or a match leads to an empty section.
	if (!IsOptionAvailable(option, cfg))
		return false;
	if (g_only_modified && !option.modified(cfg))
		return false;
	if (g_filter == nullptr || g_filter->empty())
		return true;
	return MatchesQuery(option, *g_filter);
}

// Starts a form row for `id`, writing its label, help and modified marker.
// Returns false when the row is filtered out, in which case the caller skips
// its control.
bool Row(const char* id, const Configuration& cfg)
{
	const OptionDesc& option = Opt(id);
	if (!RowMatchesFilter(option, cfg))
		return false;
	std::string help = option.help;
	help += "\n\n";
	help += TierTooltip(option.tier);
	FormRow(option.label.c_str(), help, option.cli.c_str(), option.modified(cfg));
	return true;
}

// True when a category still has something to show under the active filter.
bool CategoryHasVisibleRows(Category category, const Configuration& cfg)
{
	for (const OptionDesc& option : AllOptions()) {
		if (option.category == category && RowMatchesFilter(option, cfg))
			return true;
	}
	return false;
}

bool FilterActive()
{
	return g_only_modified || (g_filter != nullptr && !g_filter->empty());
}

bool ComboTokens(const char* id, int* value, const char* const* labels, int count)
{
	std::string items;
	for (int i = 0; i < count; ++i) {
		items += labels[i];
		items.push_back('\0');
	}
	items.push_back('\0');
	return ImGui::Combo(id, value, items.c_str());
}

// A path field with a native Browse button. Returns true when the text changed.
// `total_width` is the space the field and its button share; pass 0 to take
// whatever is left on the row. Sizing it explicitly matters wherever something
// follows on the same line, which is otherwise pushed off the edge.
bool PathField(const char* id, TextField& field, FileDialogs& dialogs,
	SDL_Window* window, FileDialogs::Kind kind, bool save, const char* placeholder,
	float total_width = 0.0f)
{
	ImGui::PushID(id);
	const float button_width = ImGui::CalcTextSize("Browse...").x
		+ ImGui::GetStyle().FramePadding.x * 2.0f;
	const float reserved = button_width + ImGui::GetStyle().ItemSpacing.x;
	if (total_width > 0.0f)
		ImGui::SetNextItemWidth(std::max(60.0f, total_width - reserved));
	else
		ImGui::SetNextItemWidth(-reserved);
	const bool changed = ImGui::InputTextWithHint("##path", placeholder,
		field.buffer.data(), field.buffer.size());
	// A long path is scrolled inside the field; show all of it on hover.
	if (ImGui::IsItemHovered() && field.buffer[0] != '\0'
		&& ImGui::CalcTextSize(field.buffer.data()).x > ImGui::GetItemRectSize().x)
		ImGui::SetTooltip("%s", field.buffer.data());
	ImGui::SameLine();
	if (ImGui::Button("Browse...")) {
		if (save)
			dialogs.RequestSave(window, id, kind, field.Get());
		else
			dialogs.RequestOpen(window, id, kind, field.Get());
	}
	ImGui::PopID();
	return changed;
}

// ---- enablement rules (design §7.3) --------------------------------------

struct Rules {
	bool dual_on = false;
	bool has_mask = false;
	bool objective_available = true;
	bool details_available = true;
};

Rules EvaluateRules(const Configuration& cfg)
{
	Rules rules;
	rules.dual_on = cfg.dual_mode;
	rules.has_mask = !cfg.details_file.empty();
	// Verified by absence of any reference under src/core/dual/: neither the
	// objective family nor the details-mask family is read in dual mode.
	rules.objective_available = !cfg.dual_mode;
	rules.details_available = !cfg.dual_mode;
	return rules;
}

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

// ---- section bodies -------------------------------------------------------

void DrawSourceSection(SetupState& state, FileDialogs& dialogs, SDL_Window* window)
{
	Configuration& cfg = *state.cfg;
	if (!BeginForm("source"))
		return;

	if (Row("height", cfg)) {
		ImGui::PushID("height");
		const float check_width = ImGui::CalcTextSize("Auto").x
			+ ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::BeginDisabled(state.height_auto);
		int height = state.height_auto ? 240 : cfg.height;
		if (ValueSliderInt("h", &height, 16, 240, "%d",
				ImGui::GetContentRegionAvail().x - check_width))
			cfg.height = height;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Checkbox("Auto", &state.height_auto))
			cfg.height = state.height_auto ? -1 : 240;
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Derive the height from the source aspect ratio.");
		ImGui::PopID();
	}

	if (Row("filter", cfg)) {
		int filter = FilterIndex(cfg.rescale_filter);
		if (ComboTokens("##filter", &filter, kFilterLabels, 6))
			cfg.rescale_filter = kFilterValues[filter];
	}

	if (Row("palette", cfg)) {
		if (PathField("palette", state.palette, dialogs, window,
				FileDialogs::Kind::Palette, false, "Palettes/laoo.act"))
			cfg.palette_file = state.palette.Get();
	}

	EndForm();
}

void DrawColourSection(SetupState& state)
{
	Configuration& cfg = *state.cfg;
	if (!BeginForm("colour"))
		return;

	if (Row("brightness", cfg))
		ValueSliderInt("brightness", &cfg.brightness, -100, 100);
	if (Row("contrast", cfg))
		ValueSliderInt("contrast", &cfg.contrast, -100, 100);
	if (Row("gamma", cfg)) {
		float gamma = static_cast<float>(cfg.gamma);
		if (ValueSliderFloat("gamma", &gamma, 0.1f, 4.0f, "%.2f"))
			cfg.gamma = gamma;
	}
	if (Row("saturation", cfg))
		ValueSliderInt("saturation", &cfg.saturation, -100, 100);
	if (Row("vibrance", cfg))
		ValueSliderInt("vibrance", &cfg.vibrance, -100, 100);

	EndForm();

	if (cfg.saturation > 40 && !FilterActive()) {
		InlineNote("High saturation can push colours past what the palette holds, "
			"collapsing them together. Check the palette usage count.", theme::kTextMuted);
	}
}

void DrawDitheringSection(SetupState& state)
{
	Configuration& cfg = *state.cfg;
	if (!BeginForm("dithering"))
		return;

	if (Row("predistance", cfg)) {
		int distance = static_cast<int>(cfg.pre_dstf);
		if (ComboTokens("##predistance", &distance, kDistanceLabels, 6))
			cfg.pre_dstf = static_cast<e_distance_function>(distance);
	}

	if (Row("dither", cfg)) {
		int dither = static_cast<int>(cfg.dither);
		if (ComboTokens("##dither", &dither, kDitherLabels, 10))
			cfg.dither = static_cast<e_dither_type>(dither);
	}

	// The strength and randomness rows hide themselves when the dither type has
	// no use for them; here we only have to grey out the type that documents
	// them as inert (design P2, §7.3).
	const bool inert = cfg.dither == E_DITHER_RFLOYD;
	{
		ImGui::BeginDisabled(inert);
		if (Row("dither_val", cfg)) {
			float strength = static_cast<float>(cfg.dither_strength);
			if (ValueSliderFloat("dither_val", &strength, 0.0f, 2.0f, "%.2f"))
				cfg.dither_strength = strength;
		}
		if (Row("dither_rand", cfg)) {
			float randomness = static_cast<float>(cfg.dither_randomness);
			if (ValueSliderFloat("dither_rand", &randomness, 0.0f, 1.0f, "%.2f"))
				cfg.dither_randomness = randomness;
		}
		ImGui::EndDisabled();
	}
	EndForm();

	if (FilterActive())
		return;
	if (inert) {
		InlineNote("Random Floyd quantizes without error diffusion, so strength "
			"and randomness have no effect on it.", theme::kTextMuted);
	}
	if (cfg.pre_dstf == E_DISTANCE_CIEDE && cfg.dither == E_DITHER_KNOLL) {
		InlineNote("CIEDE2000 together with Knoll dithering is a very slow "
			"combination; preprocessing may take minutes.", theme::kWarning);
	}
}

void DrawObjectiveSection(SetupState& state, const Rules& rules)
{
	Configuration& cfg = *state.cfg;
	DisabledGroup disabled(!rules.objective_available,
		"Dual-frame mode uses its own joint objective, so these do not apply.");

	if (!BeginForm("objective"))
		return;

	if (Row("objective", cfg)) {
		int objective = static_cast<int>(cfg.visual_objective);
		if (ComboTokens("##objective", &objective, kObjectiveLabels, 6))
			cfg.visual_objective = static_cast<e_visual_objective>(objective);
	}

	if (Row("distance", cfg)) {
		int distance = static_cast<int>(cfg.dstf);
		if (ComboTokens("##distance", &distance, kDistanceLabels, 6))
			cfg.dstf = static_cast<e_distance_function>(distance);
	}

	// Exactly one weight is ever live; show that one, relabelled, rather than
	// three fields with two greyed out (design P2).
	const char* weight_id = nullptr;
	double* weight = nullptr;
	switch (cfg.visual_objective) {
	case E_OBJECTIVE_SOURCE_COMPOSITE:
		weight_id = "spatial_weight";
		weight = &cfg.spatial_weight;
		break;
	case E_OBJECTIVE_SOURCE_EDGE:
		weight_id = "edge_weight";
		weight = &cfg.edge_weight;
		break;
	case E_OBJECTIVE_SOURCE_REGION:
		weight_id = "region_weight";
		weight = &cfg.region_weight;
		break;
	default:
		break;
	}
	if (weight != nullptr && Row(weight_id, cfg)) {
		float value = static_cast<float>(*weight);
		if (ValueSliderFloat("weight", &value, 0.001f, 1.0f, "%.3f",
				ImGuiSliderFlags_Logarithmic))
			*weight = value;
	}

	EndForm();

	if (!FilterActive() && cfg.visual_objective == E_OBJECTIVE_LEGACY_TARGET) {
		InlineNote("Scoring against the quantized target. The source-based "
			"objectives compare against the original image instead, which usually "
			"looks better but changes what a resumed run can be compared to.",
			theme::kTextMuted);
	}
}

void DrawDetailsSection(SetupState& state, const Rules& rules,
	FileDialogs& dialogs, SDL_Window* window)
{
	Configuration& cfg = *state.cfg;
	DisabledGroup disabled(!rules.details_available,
		"The details mask is single-frame only and is ignored in dual-frame mode.");

	if (!BeginForm("details"))
		return;

	if (Row("details", cfg)) {
		if (PathField("details", state.details, dialogs, window,
				FileDialogs::Kind::MaskImage, false,
				"No mask - all pixels weighted equally"))
			cfg.details_file = state.details.Get();
	}

	// Every other control here is gated on a mask actually being chosen.
	ImGui::BeginDisabled(!rules.has_mask);

	if (Row("details_mode", cfg)) {
		int mode = DetailsModeIndex(cfg.details_mode);
		if (ComboTokens("##details_mode", &mode, kDetailsModeLabels, 3))
			cfg.details_mode = kDetailsModeTokens[mode];
	}

	if (Row("details_val", cfg)) {
		// The engine puts no ceiling on this, and the classic way to force
		// detail into a face is a heavy multiplier - x4 or x16 - so the control
		// has to reach that far. Logarithmic, because the useful range spans
		// two orders of magnitude and the low end still needs fine control.
		float strength = static_cast<float>(cfg.details_strength);
		if (ValueSliderFloat("details_val", &strength, 0.0f, 15.0f, "%.2f",
				ImGuiSliderFlags_Logarithmic))
			cfg.details_strength = strength;
	}

	// Mode-specific parameters hide themselves through the model's rules.
	if (Row("details_floor", cfg)) {
		float floor_value = static_cast<float>(cfg.details_floor);
		if (ValueSliderFloat("details_floor", &floor_value, 0.01f, 1.0f, "%.2f"))
			cfg.details_floor = floor_value;
	}
	if (Row("details_feather", cfg)) {
		int feather = static_cast<int>(cfg.details_feather);
		if (ValueSliderInt("details_feather", &feather, 0, 8))
			cfg.details_feather = static_cast<unsigned>(feather);
	}
	if (Row("details_refine_mix", cfg)) {
		float mix = static_cast<float>(cfg.details_refine_mix);
		if (ValueSliderFloat("details_refine_mix", &mix, 0.0f, 1.0f, "%.2f"))
			cfg.details_refine_mix = mix;
	}

	if (Row("details_score", cfg))
		ImGui::Checkbox("##details_score", &cfg.details_score);

	// Allocation exists in every mode but does nothing in legacy.
	const bool allocate_available = cfg.details_mode != "legacy";
	ImGui::BeginDisabled(!allocate_available);
	if (Row("details_allocate", cfg))
		ImGui::Checkbox("##details_allocate", &cfg.details_allocate);
	if (Row("details_global_period", cfg)) {
		int period = static_cast<int>(cfg.details_global_period);
		if (ValueSliderInt("details_global_period", &period, 2, 100))
			cfg.details_global_period = static_cast<unsigned>(period);
	}
	ImGui::EndDisabled();

	ImGui::EndDisabled();
	EndForm();

	if (FilterActive())
		return;
	if (!rules.has_mask && rules.details_available) {
		InlineNote("Choose a mask image to enable these.", theme::kTextMuted);
		return;
	}
	if (rules.has_mask) {
		// Say what the strength actually buys, in the units of the active mode.
		char note[220];
		if (cfg.details_mode == "legacy") {
			std::snprintf(note, sizeof(note),
				"White areas of the mask weigh %.2fx the error of black areas "
				"(1 + strength). The classic heavy-emphasis settings are 3 for 4x "
				"and 15 for 16x.", 1.0 + std::max(0.0, cfg.details_strength));
		} else {
			const double ratio = cfg.details_floor > 0.0
				? (cfg.details_floor + std::max(0.0, cfg.details_strength)) / cfg.details_floor
				: 1.0;
			std::snprintf(note, sizeof(note),
				"Fully masked areas weigh %.2fx the background floor; the map is "
				"then normalized so the average weight stays 1.", ratio);
		}
		InlineNote(note, theme::kTextMuted);
	}
	if (rules.has_mask && !allocate_available)
		InlineNote("Mutation biasing needs normalized or refined mask mode.", theme::kTextMuted);
}

void DrawAlgorithmSection(SetupState& state, FileDialogs& dialogs, SDL_Window* window)
{
	Configuration& cfg = *state.cfg;
	if (!BeginForm("algorithm"))
		return;

	if (Row("optimizer", cfg)) {
		int optimizer = static_cast<int>(cfg.optimizer);
		if (ComboTokens("##optimizer", &optimizer, kOptimizerLabels, 3))
			cfg.optimizer = static_cast<Configuration::e_optimizer>(optimizer);
	}

	if (Row("solutions", cfg)) {
		if (ValueSliderInt("solutions", &state.solutions, 1, 64))
			SetSolutions(state.solutions);
	}

	if (Row("init", cfg)) {
		int init = static_cast<int>(cfg.init_type);
		if (ComboTokens("##init", &init, kInitLabels, 4))
			cfg.init_type = static_cast<e_init_type>(init);
	}

	if (Row("unstuck_after", cfg)) {
		double evals = static_cast<double>(cfg.unstuck_after);
		if (ImGui::InputDouble("##unstuck_after", &evals, 100000.0, 1000000.0, "%.0f"))
			cfg.unstuck_after = static_cast<unsigned long long>(std::max(0.0, evals));
	}

	// Drift only means anything once escalation is armed.
	ImGui::BeginDisabled(cfg.unstuck_after == 0);
	if (Row("unstuck_drift", cfg)) {
		float drift = static_cast<float>(cfg.unstuck_drift_norm);
		if (ValueSliderFloat("unstuck_drift", &drift, 0.0f, 0.01f, "%.5f"))
			cfg.unstuck_drift_norm = drift;
	}
	ImGui::EndDisabled();

	if (Row("seed", cfg)) {
		ImGui::PushID("seed");
		const float check_width = ImGui::CalcTextSize("Random").x
			+ ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - check_width);
		ImGui::BeginDisabled(state.seed_random);
		if (ImGui::InputInt("##seed", &state.seed_value, 0, 0)) {
			state.seed_value = std::max(0, state.seed_value);
			cfg.initial_seed = static_cast<unsigned long>(state.seed_value);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Checkbox("Random", &state.seed_random))
			cfg.initial_seed = state.seed_random ? 0 : static_cast<unsigned long>(state.seed_value);
		ImGui::PopID();
	}

	if (Row("onoff", cfg)) {
		if (PathField("onoff", state.onoff, dialogs, window,
				FileDialogs::Kind::OnOffText, false,
				"No restrictions - every register available"))
			cfg.on_off_file = state.onoff.Get();
	}

	EndForm();

	if (!FilterActive() && cfg.unstuck_after == 0)
		InlineNote("Escalation is off, so drift is inactive.", theme::kTextMuted);
}

void DrawDualSection(SetupState& state)
{
	Configuration& cfg = *state.cfg;
	if (!BeginForm("dual"))
		return;

	if (Row("dual", cfg))
		ImGui::Checkbox("##dual", &cfg.dual_mode);

	// All sub-options hide themselves while dual is off (design §7.3).
	{
		if (Row("first_dual_steps", cfg)) {
			double steps = static_cast<double>(cfg.first_dual_steps);
			if (ImGui::InputDouble("##first_dual_steps", &steps, 10000.0, 100000.0, "%.0f"))
				cfg.first_dual_steps = static_cast<unsigned long long>(std::max(0.0, steps));
		}

		if (Row("after_dual_steps", cfg)) {
			static const char* const kLabels[2] = {"Copy frame A", "Generate a fresh B"};
			int choice = cfg.after_dual_steps == "generate" ? 1 : 0;
			if (ComboTokens("##after_dual_steps", &choice, kLabels, 2))
				cfg.after_dual_steps = choice == 1 ? "generate" : "copy";
		}

		if (Row("altering_dual_steps", cfg)) {
			double steps = static_cast<double>(cfg.altering_dual_steps);
			if (ImGui::InputDouble("##altering_dual_steps", &steps, 10000.0, 100000.0, "%.0f"))
				cfg.altering_dual_steps = static_cast<unsigned long long>(std::max(0.0, steps));
		}

		if (Row("dual_blending", cfg)) {
			static const char* const kLabels[2] = {"YUV", "RGB"};
			int choice = cfg.dual_blending == "rgb" ? 1 : 0;
			if (ComboTokens("##dual_blending", &choice, kLabels, 2))
				cfg.dual_blending = choice == 1 ? "rgb" : "yuv";
		}

		if (Row("dual_luma", cfg)) {
			float luma = static_cast<float>(cfg.dual_luma);
			if (ValueSliderFloat("dual_luma", &luma, 0.0f, 2.0f, "%.3f"))
				cfg.dual_luma = luma;
		}
		if (Row("dual_chroma", cfg)) {
			float chroma = static_cast<float>(cfg.dual_chroma);
			if (ValueSliderFloat("dual_chroma", &chroma, 0.0f, 2.0f, "%.3f"))
				cfg.dual_chroma = chroma;
		}

		if (Row("dual_dither", cfg)) {
			int dither = static_cast<int>(cfg.dual_dither);
			if (ComboTokens("##dual_dither", &dither, kDualDitherLabels, 6))
				cfg.dual_dither = static_cast<e_dual_dither_type>(dither);
		}
		if (Row("dual_dither_val", cfg)) {
			float value = static_cast<float>(cfg.dual_dither_val);
			if (ValueSliderFloat("dual_dither_val", &value, 0.0f, 2.0f, "%.3f"))
				cfg.dual_dither_val = value;
		}
		if (Row("dual_dither_rand", cfg)) {
			float value = static_cast<float>(cfg.dual_dither_rand);
			if (ValueSliderFloat("dual_dither_rand", &value, 0.0f, 1.0f, "%.3f"))
				cfg.dual_dither_rand = value;
		}
	}

	EndForm();

	if (!FilterActive() && cfg.dual_mode && cfg.after_dual_steps == "generate") {
		InlineNote("Generating a fresh frame B costs a second bootstrap of the same "
			"length before alternation begins.", theme::kTextMuted);
	}
}

// ---- collapsed-header state summaries (design P3) ------------------------

// A section that is closed still has to say what it is currently set to, so
// the whole configuration can be audited by reading headers.
std::string SectionSummary(Category category, const Configuration& cfg,
	const SetupState& state)
{
	auto value = [&cfg](const char* id) {
		return DisplayValue(Opt(id), cfg);
	};
	std::string summary;
	auto append = [&summary](const std::string& text) {
		if (!summary.empty())
			summary += ", ";
		summary += text;
	};

	switch (category) {
	case Category::Source:
		append(state.height_auto ? "auto height" : value("height") + " lines");
		append(value("filter"));
		append(FileName(cfg.palette_file));
		break;
	case Category::TargetColour:
		if (cfg.brightness != 0) append("brightness " + value("brightness"));
		if (cfg.contrast != 0) append("contrast " + value("contrast"));
		if (cfg.gamma != 1.0) append("gamma " + value("gamma"));
		if (cfg.saturation != 0) append("saturation " + value("saturation"));
		if (cfg.vibrance != 0) append("vibrance " + value("vibrance"));
		if (summary.empty()) summary = "no adjustments";
		break;
	case Category::TargetDithering:
		append(value("dither"));
		if (cfg.dither != E_DITHER_NONE && cfg.dither != E_DITHER_RFLOYD) {
			append("strength " + value("dither_val"));
			if (cfg.dither_randomness > 0.0)
				append("randomness " + value("dither_rand"));
		}
		append("build " + value("predistance"));
		break;
	case Category::Objective:
		if (cfg.dual_mode) {
			summary = "dual mode uses its own objective";
		} else {
			append(value("objective"));
			append(value("distance"));
		}
		break;
	case Category::Details:
		if (cfg.dual_mode)
			summary = "single-frame only";
		else if (cfg.details_file.empty())
			summary = "no mask";
		else {
			append(FileName(cfg.details_file));
			append(value("details_mode"));
			append("strength " + value("details_val"));
		}
		break;
	case Category::Algorithm:
		append(value("optimizer"));
		append("history " + value("solutions"));
		append("init " + value("init"));
		if (cfg.unstuck_after != 0)
			append("escalates after " + value("unstuck_after"));
		if (!cfg.on_off_file.empty())
			append("register limits");
		break;
	case Category::DualFrame:
		if (!cfg.dual_mode) {
			summary = "off";
		} else {
			append("on");
			append(cfg.after_dual_steps == "generate" ? "generate B" : "copy A to B");
			append("alternate every " + value("altering_dual_steps"));
		}
		break;
	default:
		break;
	}
	return summary;
}

// ---- form column ----------------------------------------------------------

void DrawForm(SetupState& state, FileDialogs& dialogs, SDL_Window* window)
{
	Configuration& cfg = *state.cfg;
	const Rules rules = EvaluateRules(cfg);

	// Search and the modified-only view live at the top of the form now that
	// there is no rail. Both filter the real controls, so a match is something
	// you can edit on the spot rather than a description of an option.
	const float toggle_width = ImGui::CalcTextSize("Only changed").x
		+ ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - toggle_width);
	ImGui::InputTextWithHint("##search", "Search options...",
		state.search.data(), state.search.size());
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Matches option names, help text and command-line flags. "
			"Type /details_floor to jump straight to it.");
	}
	ImGui::SameLine();
	ImGui::Checkbox("Only changed", &state.only_modified);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Show just the options that differ from their defaults.");

	const std::string query(state.search.data());
	g_filter = &query;
	g_only_modified = state.only_modified;
	const bool filtering = FilterActive();

	ImGui::Spacing();
	ImGui::BeginChild("form_scroll", ImVec2(0.0f, 0.0f));

	int visible_sections = 0;
	for (int i = 0; i < kCategoryCount; ++i) {
		const Category category = static_cast<Category>(i);
		if (category == Category::RunOutput)
			continue; // lives in the bottom bar

		if (!CategoryHasVisibleRows(category, cfg))
			continue;
		++visible_sections;

		char title[96];
		const char* ordinal = CategoryOrdinal(category);
		if (ordinal[0] != '\0')
			std::snprintf(title, sizeof(title), "%s  %s", ordinal, CategoryTitle(category));
		else
			std::snprintf(title, sizeof(title), "%s", CategoryTitle(category));

		const bool inert = (category == Category::Objective && cfg.dual_mode)
			|| (category == Category::Details && cfg.dual_mode);
		// While filtering, everything that matched is expanded: collapsing a
		// search result would hide the thing that was searched for.
		bool open = filtering ? true : state.section_open[i];
		const std::string summary = open
			? std::string() : SectionSummary(category, cfg, state);

		if (BeginSection(CategoryTitle(category), title, summary,
				ModifiedCountInCategory(cfg, category), &open, inert)) {
			switch (category) {
			case Category::Source:          DrawSourceSection(state, dialogs, window); break;
			case Category::TargetColour:    DrawColourSection(state); break;
			case Category::TargetDithering: DrawDitheringSection(state); break;
			case Category::Objective:       DrawObjectiveSection(state, rules); break;
			case Category::Details:         DrawDetailsSection(state, rules, dialogs, window); break;
			case Category::Algorithm:       DrawAlgorithmSection(state, dialogs, window); break;
			case Category::DualFrame:       DrawDualSection(state); break;
			default: break;
			}
			EndSection();
		}
		if (!filtering)
			state.section_open[i] = open;
	}

	if (visible_sections == 0) {
		ImGui::Spacing();
		if (state.only_modified && query.empty()) {
			InlineNote("Everything is at its default. Whatever you change appears "
				"here, and that same set is what the command line records.",
				theme::kTextMuted);
		} else {
			// An option can match the query and still have no control right
			// now. Saying "no match" would be wrong and unhelpful, so name it
			// and explain what would bring it back.
			bool explained = false;
			for (const OptionDesc& option : AllOptions()) {
				if (!MatchesQuery(option, query))
					continue;
				if (state.only_modified && !option.modified(cfg))
					continue;
				char note[256];
				if (!option.in_form) {
					// It has a control, just not in this list.
					std::snprintf(note, sizeof(note), "%s (/%s): %s",
						option.label.c_str(), option.cli.c_str(),
						option.location_hint.c_str());
				} else if (option.available != nullptr && !option.available(cfg)) {
					std::snprintf(note, sizeof(note),
						"%s (/%s) is not available here. %s",
						option.label.c_str(), option.cli.c_str(),
						option.unavailable_hint.c_str());
				} else {
					continue;
				}
				InlineNote(note, theme::kTextMuted);
				explained = true;
			}
			if (!explained) {
				InlineNote("No option matches that. Search covers names, "
					"command-line flags and help text.", theme::kTextMuted);
			}
		}
	}

	ImGui::Dummy(ImVec2(0.0f, 16.0f));
	ImGui::EndChild();

	g_filter = nullptr;
	g_only_modified = false;
}

// ---- bottom bar -----------------------------------------------------------

void DrawBottomBar(SetupState& state, FileDialogs& dialogs, SDL_Window* window,
	bool& running, bool& accepted)
{
	Configuration& cfg = *state.cfg;
	const ImGuiStyle& style = ImGui::GetStyle();

	auto caption = [](const char* text) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
		ImGui::SameLine();
	};

	// The primary action owns a fixed column on the right; the option rows lay
	// themselves out in what is left, so neither can push the other off screen.
	const float action_width = ImGui::GetFontSize() * 11.0f;
	const float action_height = ImGui::GetFrameHeight() * 1.6f;
	const float fields_width = std::max(ImGui::GetFontSize() * 20.0f,
		ImGui::GetContentRegionAvail().x - action_width - style.ItemSpacing.x * 2.0f);
	const float bar_height = ImGui::GetContentRegionAvail().y;

	// A horizontal scrollbar rather than NoScrollbar: on a narrow window the
	// run options must stay reachable instead of being silently clipped.
	ImGui::BeginChild("fields", ImVec2(fields_width, bar_height), ImGuiChildFlags_None,
		ImGuiWindowFlags_HorizontalScrollbar);

	// Row 1: where the result goes, and what the machine will spend on it.
	// Widths are measured rather than guessed so nothing runs off the edge.
	const int hardware = static_cast<int>(std::thread::hardware_concurrency());
	const float slider_width = ImGui::GetFontSize() * 10.0f;
	const float threads_block = ImGui::CalcTextSize("Threads").x + slider_width
		+ style.ItemSpacing.x * 3.0f;
	const float cache_block = ImGui::CalcTextSize("Cache/thread").x + slider_width
		+ style.ItemSpacing.x * 3.0f;
	const float output_label = ImGui::CalcTextSize("Output").x + style.ItemSpacing.x;
	const float output_width = std::max(ImGui::GetFontSize() * 8.0f,
		ImGui::GetContentRegionAvail().x - output_label - threads_block - cache_block);

	caption("Output");
	if (PathField("output", state.output, dialogs, window,
			FileDialogs::Kind::OutputImage, true, "output.png", output_width)) {
		cfg.output_file = state.output.Get();
		state.output_touched = true;
	}

	ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
	caption("Threads");
	ValueSliderInt("threads", &cfg.threads, 1, hardware > 0 ? hardware : 16,
		"%d", slider_width);
	if (ImGui::IsItemHovered() && hardware > 0)
		ImGui::SetTooltip("This machine reports %d hardware threads.", hardware);

	ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
	caption("Cache/thread");
	if (ValueSliderInt("cache", &state.cache_mb, 8, 1024, "%d MB", slider_width))
		cfg.cache_size = state.cache_mb * 1024 * 1024;

	// Row 2: run limits and modes.
	caption("Stop after");
	if (ImGui::Checkbox("Run until stopped", &state.max_evals_unlimited)) {
		cfg.max_evals = state.max_evals_unlimited
			? DefaultConfiguration().max_evals : state.max_evals_value;
	}
	if (!state.max_evals_unlimited) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
		double evals = static_cast<double>(state.max_evals_value);
		if (ImGui::InputDouble("##max_evals", &evals, 0.0, 0.0, "%.0f evals")) {
			state.max_evals_value = static_cast<unsigned long long>(std::max(1.0, evals));
			cfg.max_evals = state.max_evals_value;
		}
	}

	ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
	caption("Autosave");
	if (ImGui::Checkbox("Automatic", &state.autosave_auto))
		cfg.save_period = state.autosave_auto ? -1 : state.autosave_value;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Saves roughly every 30 seconds.");
	if (!state.autosave_auto) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
		if (ImGui::InputInt("##save", &state.autosave_value, 0, 0)) {
			state.autosave_value = std::max(1, state.autosave_value);
			cfg.save_period = state.autosave_value;
		}
	}

	ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
	ImGui::Checkbox("Resume stopped run", &cfg.continue_processing);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Continues from the existing files for this output name.");
	ImGui::SameLine();
	ImGui::Checkbox("Preprocess only", &cfg.preprocess_only);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Writes the source and target images, then exits.");

	if (!state.folder_error.empty())
		InlineNote(state.folder_error.c_str(), theme::kDanger);

	// Warnings that matter right before committing.
	std::string existing;
	if (!cfg.continue_processing && OutputArtifactsExist(cfg.output_file, &existing)) {
		char message[320];
		std::snprintf(message, sizeof(message),
			"%s already exists and will be overwritten. Tick \"Resume stopped run\" "
			"to continue it instead, or choose another output name.", existing.c_str());
		InlineNote(message, theme::kWarning);
	}

	ImGui::EndChild();

	ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
	// Vertically centre the action against the two option rows.
	ImGui::SetCursorPosY(ImGui::GetCursorPosY()
		+ std::max(0.0f, (bar_height - action_height) * 0.5f));

	const bool ready = !cfg.input_file.empty();
	if (!ready) {
		ImGui::BeginDisabled();
		ImGui::Button("Convert", ImVec2(action_width, action_height));
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Choose an input file first.");
	} else {
		ImGui::PushStyleColor(ImGuiCol_Button, theme::ToVec4(theme::kAccent));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.76f, 0.36f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.62f, 0.22f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
		if (ImGui::Button("Convert", ImVec2(action_width, action_height))) {
			// Create the run folder up front: better to refuse here than to
			// discover on the first autosave that nothing can be written.
			state.folder_error.clear();
			AbsolutizeJobPaths(cfg);
			if (CreateRunFolder(cfg.output_file, &state.folder_error)) {
				RegisterRecentRun(DirectoryOf(cfg.output_file));
				accepted = true;
				running = false;
			}
		}
		ImGui::PopStyleColor(4);
	}
}

// ---- title bar ------------------------------------------------------------

void DrawTitleBar(SetupState& state, FileDialogs& dialogs, SDL_Window* window)
{
	Configuration& cfg = *state.cfg;
	const ImGuiStyle& style = ImGui::GetStyle();

	const int modified = static_cast<int>(ModifiedOptions(cfg).size());
	char changed_label[64];
	std::snprintf(changed_label, sizeof(changed_label), "%d option%s changed",
		modified, modified == 1 ? "" : "s");
	const float tools_width = ImGui::CalcTextSize(changed_label).x
		+ ImGui::CalcTextSize("Recent").x + style.FramePadding.x * 2.0f
		+ ImGui::CalcTextSize("Copy command line").x + style.FramePadding.x * 2.0f
		+ ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f
		+ style.ItemSpacing.x * 5.0f;

	// The image row sits on its own panel with breathing room, and that panel
	// is the drop target, so the affordance and the hit area are the same
	// thing rather than an invisible whole-window catch.
	const float zone_padding = 10.0f;
	const float zone_width = std::max(ImGui::GetFontSize() * 16.0f,
		ImGui::GetContentRegionAvail().x - tools_width - style.ItemSpacing.x * 3.0f);
	const float zone_height = ImGui::GetFrameHeight() + zone_padding * 2.0f;
	const ImVec2 zone_min = ImGui::GetCursorScreenPos();
	const ImVec2 zone_max(zone_min.x + zone_width, zone_min.y + zone_height);
	state.drop_zone = ImVec4(zone_min.x, zone_min.y, zone_max.x, zone_max.y);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const bool armed = state.drag_active;
	const bool targeted = armed && state.drag_inside_zone;
	{
		ImVec4 fill = ImGui::ColorConvertU32ToFloat4(
			targeted ? theme::kAccent : theme::kSurfaceHigh);
		fill.w = targeted ? 0.22f : 0.55f;
		draw->AddRectFilled(zone_min, zone_max, ImGui::GetColorU32(fill), 8.0f);
		if (armed) {
			draw->AddRect(zone_min, zone_max,
				targeted ? theme::kAccent : theme::kAccentDim, 8.0f, 0, 2.0f);
		}
	}

	ImGui::SetCursorScreenPos(ImVec2(zone_min.x + zone_padding + 4.0f,
		zone_min.y + zone_padding));
	ImGui::BeginGroup();
	ImGui::AlignTextToFramePadding();
	ImGui::PushStyleColor(ImGuiCol_Text,
		theme::ToVec4(armed ? theme::kAccent : theme::kTextMuted));
	ImGui::TextUnformatted(armed ? "Drop image" : "Image");
	ImGui::PopStyleColor();
	ImGui::SameLine();

	const float label_width = ImGui::GetCursorScreenPos().x - zone_min.x;
	const float picker_width = std::max(ImGui::GetFontSize() * 10.0f,
		zone_width - label_width - zone_padding * 2.0f);
	if (PathField("input", state.input, dialogs, window,
			FileDialogs::Kind::InputImage, false,
			"Choose an image to convert, or drag one onto this panel", picker_width)) {
		cfg.input_file = state.input.Get();
		if (!state.output_touched) {
			cfg.output_file = DeriveOutputName(cfg.input_file);
			state.output.Set(cfg.output_file);
		}
	}
	ImGui::EndGroup();

	// Command-line parity tools, right-aligned (design §8).
	ImGui::SetCursorScreenPos(ImVec2(zone_max.x + style.ItemSpacing.x * 2.0f,
		zone_min.y + zone_padding));
	ImGui::AlignTextToFramePadding();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::TextUnformatted(changed_label);
	ImGui::PopStyleColor();
	ImGui::SameLine();

	if (ImGui::Button("Recent")) {
		state.show_recent = !state.show_recent;
		state.recent_dismissed = !state.show_recent;
		state.recent_refresh = state.show_recent;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Previous conversions: continue one, or reuse its "
			"settings for a new run.");
	}
	ImGui::SameLine();

	if (ImGui::Button("Copy command line")) {
		ImGui::SetClipboardText(BuildCommandLine(cfg).c_str());
		state.copied_notice = "Command line copied";
		state.copied_at = NowMs();
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", BuildCommandLine(cfg).c_str());

	ImGui::SameLine();
	if (ImGui::Button("Reset")) {
		const std::string input = cfg.input_file;
		const std::string output = cfg.output_file;
		const int threads = cfg.threads;
		Configuration fresh = DefaultConfiguration();
		fresh.parser = cfg.parser;
		fresh.input_file = input;
		fresh.output_file = output;
		fresh.threads = threads;
		fresh.command_line = cfg.command_line;
		cfg = fresh;
		SetSolutions(1);
		state.solutions = 1;
		state.height_auto = true;
		state.seed_random = true;
		state.cache_mb = 64;
		state.max_evals_unlimited = true;
		state.autosave_auto = true;
		state.palette.Set(cfg.palette_file);
		state.details.Set(cfg.details_file);
		state.onoff.Set(cfg.on_off_file);
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Return every option to its default, keeping the file paths.");

	if (!state.copied_notice.empty()) {
		if (NowMs() - state.copied_at > 2200.0) {
			state.copied_notice.clear();
		} else {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kSuccess));
			ImGui::TextUnformatted(state.copied_notice.c_str());
			ImGui::PopStyleColor();
		}
	}
}

// ---- frame assembly -------------------------------------------------------

void DrawSetupFrame(SetupState& state, FileDialogs& dialogs, SDL_Window* window,
	TargetPreview& preview, ImageViewer& viewer, RecentGallery& gallery,
	RecentGallery::Result& pending_choice, bool& running, bool& accepted)
{
	const ImVec2 size = ImGui::GetContentRegionAvail();
	// Below this the form and a usable image cannot share the width, so the
	// viewer stands down and the form takes the window.
	const bool hide_viewer = size.x < kOverlayViewerBreakpoint;

	const float bottom_height = ImGui::GetFrameHeight() * 4.1f + 20.0f;
	const float title_height = ImGui::GetFrameHeight() + 20.0f + 16.0f;
	const float body_height = size.y - bottom_height - title_height
		- ImGui::GetStyle().ItemSpacing.y * 3.0f;

	// Title bar: the input picker plus the command-line tools.
	ImGui::SetCursorPosX(8.0f);
	ImGui::BeginChild("title", ImVec2(size.x - 16.0f, title_height),
		ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
	DrawTitleBar(state, dialogs, window);
	ImGui::EndChild();

	// The form column is sized so the viewer keeps the majority of the width.
	const float form_width = hide_viewer
		? size.x - 16.0f
		: std::min(std::max(state.form_width, ImGui::GetFontSize() * 20.0f),
			size.x - ImGui::GetFontSize() * 20.0f);

	ImGui::SetCursorPosX(8.0f);
	ImGui::BeginChild("form", ImVec2(form_width, body_height),
		ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
	DrawForm(state, dialogs, window);
	ImGui::EndChild();

	if (!hide_viewer) {
		ImGui::SameLine(0.0f, 4.0f);
		// Draggable splitter between the form and the viewer.
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::ToVec4(theme::kAccentSoft));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::ToVec4(theme::kAccent));
		ImGui::Button("##splitter", ImVec2(5.0f, body_height));
		ImGui::PopStyleColor(3);
		if (ImGui::IsItemActive())
			state.form_width += ImGui::GetIO().MouseDelta.x;
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

		ImGui::SameLine(0.0f, 4.0f);
		ImGui::BeginChild("viewer",
			ImVec2(std::max(80.0f, size.x - form_width - 30.0f), body_height),
			ImGuiChildFlags_AlwaysUseWindowPadding);
		// With no image chosen there is nothing to preview, so the space goes
		// to the history instead of an empty placeholder.
		const bool gallery_visible = state.show_recent
			|| (state.cfg->input_file.empty() && !state.recent_dismissed
				&& !gallery.empty());
		if (gallery_visible) {
			const RecentGallery::Result choice =
				gallery.Draw(/*closable*/ !state.cfg->input_file.empty()
					|| state.show_recent);
			switch (choice.action) {
			case RecentGallery::Action::Dismiss:
				state.show_recent = false;
				state.recent_dismissed = true;
				break;
			case RecentGallery::Action::Continue:
			case RecentGallery::Action::UseSettings:
				pending_choice = choice;
				break;
			case RecentGallery::Action::None:
			default:
				break;
			}
		} else if (viewer.Draw(preview.Busy(),
				state.cfg->input_file.empty()
					? "Choose an image above to see the target preview"
					: "Preparing preview...")) {
			preview.ForceRefresh();
		}
		ImGui::EndChild();
	}

	// Bottom bar.
	ImGui::SetCursorPosX(8.0f);
	ImGui::BeginChild("bottom", ImVec2(size.x - 16.0f, bottom_height),
		ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar);
	DrawBottomBar(state, dialogs, window, running, accepted);
	ImGui::EndChild();
}

} // namespace

bool RunSetupScreen(Configuration& cfg, bool show_recent)
{
	// Development hook: go straight to the run with the command line as given,
	// so the dashboard can be exercised without a human pressing Convert. The
	// value is how many runs to accept before ending the session, and the run
	// folder is allocated exactly as the Convert button would do it.
	if (const char* skip = SDL_getenv("RASTA_LIVE_UI_SKIP_SETUP")) {
		static int remaining = std::max(1, SDL_atoi(skip));
		if (remaining-- <= 0 || cfg.input_file.empty())
			return false;
		if (cfg.output_file == DefaultConfiguration().output_file)
			cfg.output_file = AllocateRunOutputPath(cfg.input_file);
		AbsolutizeJobPaths(cfg);
		std::string error;
		if (!CreateRunFolder(cfg.output_file, &error)) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return false;
		}
		RegisterRecentRun(cfg.output_file.substr(0, cfg.output_file.find_last_of('/')));
		cfg.command_line = BuildCommandLineArgs(cfg);
		return true;
	}

	if (!SDL_Init(SDL_INIT_VIDEO))
		return false;

	SDL_Window* window = SDL_CreateWindow("RastaConverter", 1420, 900,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	SDL_Renderer* renderer = window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
	if (window == nullptr || renderer == nullptr) {
		if (renderer != nullptr) SDL_DestroyRenderer(renderer);
		if (window != nullptr) SDL_DestroyWindow(window);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr; // window layout is derived, not persisted

	const float pixel_density = DetectPixelDensity(window);
	LoadFonts(pixel_density);
	ApplyTheme();

	if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)
		|| !ImGui_ImplSDLRenderer3_Init(renderer)) {
		ImGui::DestroyContext();
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		return false;
	}

	SetupState state;
	state.cfg = &cfg;

	// A seed of "random" arrives here already resolved to a clock value; the
	// UI wants it back as the word "random" unless it was given explicitly.
	if (cfg.parser.getValue("seed", "random") == "random")
		cfg.initial_seed = 0;

	// Threads: the parser default is 1, which leaves most of the machine idle.
	// A GUI launched with no command line picks a sensible default instead;
	// an explicit /threads on the command line is respected.
	if (cfg.parser.getValue("threads", "").empty() && cfg.parser.getValue("t", "").empty()) {
		const unsigned hardware = std::thread::hardware_concurrency();
		if (hardware > 0)
			cfg.threads = static_cast<int>(hardware);
	}

	if (cfg.output_file == DefaultConfiguration().output_file && !cfg.input_file.empty())
		cfg.output_file = DeriveOutputName(cfg.input_file);

	SyncStateFromConfig(state);

	for (int i = 0; i < kCategoryCount; ++i) {
		// Stages 1 and 2 open, stage 3 collapsed to its summaries (§7.1c).
		const Category category = static_cast<Category>(i);
		state.section_open[i] = category == Category::Source
			|| category == Category::TargetColour
			|| category == Category::TargetDithering;
	}

	FileDialogs dialogs;
	TargetPreview preview;
	ImageViewer viewer(renderer);
	RecentGallery gallery(renderer);
	gallery.Refresh();
	// After a conversion the history leads with the run that just finished,
	// which is the closest thing to a result screen until §11 exists.
	state.show_recent = show_recent && !gallery.empty();
	PreviewResult latest;

	// Development hook: render a fixed number of frames, save the window to a
	// PNG and exit. Combined with SDL_VIDEODRIVER=offscreen this makes the
	// layout checkable without a display.
	const char* screenshot_path = SDL_getenv("RASTA_LIVE_UI_SCREENSHOT");
	const char* screenshot_frames = SDL_getenv("RASTA_LIVE_UI_SCREENSHOT_FRAMES");
	int frames_left = screenshot_path != nullptr
		? (screenshot_frames != nullptr ? SDL_atoi(screenshot_frames) : 240) : -1;

	bool accepted = false;
	bool running = true;
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT)
				running = false;
			// Drag and drop. The image panel is the advertised target and
			// lights up while a drag is over it, but a drop anywhere in the
			// window is still accepted rather than silently ignored.
			switch (event.type) {
			case SDL_EVENT_DROP_BEGIN:
				state.drag_active = true;
				break;
			case SDL_EVENT_DROP_POSITION:
				state.drag_active = true;
				state.drag_point = ImVec2(event.drop.x, event.drop.y);
				break;
			case SDL_EVENT_DROP_COMPLETE:
				state.drag_active = false;
				state.drag_inside_zone = false;
				state.drag_point = ImVec2(-1.0f, -1.0f);
				break;
			case SDL_EVENT_DROP_FILE:
				state.drag_active = false;
				state.drag_inside_zone = false;
				if (event.drop.data != nullptr) {
					state.input.Set(event.drop.data);
					cfg.input_file = state.input.Get();
					if (!state.output_touched) {
						cfg.output_file = DeriveOutputName(cfg.input_file);
						state.output.Set(cfg.output_file);
					}
				}
				break;
			default:
				break;
			}
		}

		// Drain any completed file dialog.
		std::string target;
		std::string chosen;
		while (dialogs.Poll(&target, &chosen)) {
			if (target == "input") {
				state.input.Set(chosen);
				cfg.input_file = chosen;
				if (!state.output_touched) {
					cfg.output_file = DeriveOutputName(chosen);
					state.output.Set(cfg.output_file);
				}
			} else if (target == "output") {
				state.output.Set(chosen);
				cfg.output_file = chosen;
				state.output_touched = true;
			} else if (target == "palette") {
				state.palette.Set(chosen);
				cfg.palette_file = chosen;
			} else if (target == "details") {
				state.details.Set(chosen);
				cfg.details_file = chosen;
			} else if (target == "onoff") {
				state.onoff.Set(chosen);
				cfg.on_off_file = chosen;
			}
		}

		// Drop coordinates arrive in window points, the same space the drop
		// zone rectangle was recorded in.
		{
			const ImVec2 origin = ImGui::GetMainViewport()->Pos;
			const float px = state.drag_point.x + origin.x;
			const float py = state.drag_point.y + origin.y;
			state.drag_inside_zone = state.drag_active
				&& px >= state.drop_zone.x && px <= state.drop_zone.z
				&& py >= state.drop_zone.y && py <= state.drop_zone.w;
		}

		preview.Request(cfg);
		if (preview.Fetch(&latest))
			viewer.SetContent(latest);

		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("##setup", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleVar();

		if (state.recent_refresh) {
			state.recent_refresh = false;
			gallery.Refresh();
		}
		RecentGallery::Result choice;
		DrawSetupFrame(state, dialogs, window, preview, viewer, gallery, choice,
			running, accepted);

		ImGui::End();

		// Acting on a gallery choice rewrites the whole configuration, so it
		// happens after the frame rather than in the middle of drawing it.
		if (choice.action == RecentGallery::Action::Continue) {
			cfg.input_file = choice.run.input_file;
			state.input.Set(cfg.input_file);
			cfg.output_file = choice.run.output_base;
			state.output.Set(cfg.output_file);
			state.output_touched = true;
			cfg.continue_processing = true;
			RegisterRecentRun(choice.run.folder);
			accepted = true;
			running = false;
		} else if (choice.action == RecentGallery::Action::UseSettings) {
			// The stored command line is the run's full recipe; re-parsing it
			// is the same path a pasted command line would take. It resets
			// every option to its default first, so nothing from the current
			// form leaks into the reused settings.
			if (!choice.run.command_line.empty()) {
				// ProcessCmdLine rebuilds the parser from the stored tokens,
				// which decide live_gui by looking for /livegui - absent from a
				// recipe, so the reused run would have dropped back to the old
				// three-blit display. It is a property of this session, not of
				// the recipe.
				const bool live_gui = cfg.live_gui;
				cfg.command_line = choice.run.command_line;
				cfg.ProcessCmdLine();
				cfg.live_gui = live_gui;
			}
			if (!choice.run.input_file.empty())
				cfg.input_file = choice.run.input_file;
			cfg.continue_processing = false;
			// A reused recipe gets its own folder; the old run stays intact.
			cfg.output_file = AllocateRunOutputPath(cfg.input_file);
			state.output_touched = false;
			SyncStateFromConfig(state);
			state.show_recent = false;
			state.recent_dismissed = true;
		}

		ImGui::Render();
		// The window can be dragged between displays of different density, so
		// this is re-evaluated every frame rather than cached at startup.
		ApplyRenderScale(renderer, DetectPixelDensity(window));
		SDL_SetRenderDrawColor(renderer, 19, 21, 26, 255);
		SDL_RenderClear(renderer);
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

		if (frames_left > 0 && --frames_left == 0) {
			if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
				SDL_SaveBMP(shot, screenshot_path);
				SDL_DestroySurface(shot);
			}
			running = false;
		}

		SDL_RenderPresent(renderer);
		SDL_Delay(6);
	}

	// The preview owns the process-global palette and distance function while
	// it runs; stop it before the conversion configures them for real.
	preview.Shutdown();

	// Paths are pinned down only once the job is accepted, so the form can keep
	// showing the tidy relative default for the bundled palette while it is
	// being edited.
	if (accepted) {
		AbsolutizeJobPaths(cfg);
		// Settings changed in the form never reached the parser, so
		// cfg.command_line still described the launch arguments - nothing at all
		// for a double-clicked binary. That string is what the .opt header
		// records, and what both resume and the Recent browser's Reuse read
		// back, so a GUI-configured run has to write its own recipe here.
		cfg.command_line = BuildCommandLineArgs(cfg);
	}

	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	return accepted;
}

} // namespace rc_live_ui

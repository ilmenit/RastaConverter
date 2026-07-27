// The bodies of the setup form's collapsible sections, plus the one-line
// summaries a collapsed section shows. Split out of SetupScreen.cpp: this is
// the half that grows with every new option, while the window around it does
// not. What the two halves share is in SetupInternal.h.

#include "SetupInternal.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "Desktop.h"

namespace rc_live_ui {
namespace setup {

namespace {

// The palettes shipped beside the executable. Choosing one is the common case -
// a browse dialog for a file the program already carries is a poor trade - but
// the field stays a path, so anything else on disk is still reachable through
// Browse.
struct BundledPalette {
	std::string label;   // file name without the extension
	std::string path;
};

const std::vector<BundledPalette>& BundledPalettes()
{
	static const std::vector<BundledPalette> palettes = [] {
		std::vector<BundledPalette> found;
		const std::string folder = BundledPath("Palettes");
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
			if (ec || !entry.is_regular_file())
				continue;
			std::string extension = entry.path().extension().string();
			for (char& c : extension)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (extension != ".act")
				continue;
			found.push_back({entry.path().stem().string(),
				entry.path().string()});
		}
		std::sort(found.begin(), found.end(),
			[](const BundledPalette& a, const BundledPalette& b) {
				return a.label < b.label;
			});
		return found;
	}();
	return palettes;
}

// True when two paths name the same palette. Comparing the file name is enough
// and survives the difference between the relative path a recipe carries and
// the absolute one BundledPalettes reports.
bool SamePalette(const std::string& a, const std::string& b)
{
	return FileName(a) == FileName(b) && !a.empty();
}

} // namespace

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
		ImGui::PushID("palette");
		const std::vector<BundledPalette>& bundled = BundledPalettes();
		// The current file, when it is not one of the bundled ones, is the last
		// entry rather than no entry: a combo that cannot show what is selected
		// is worse than one extra line.
		int selected = -1;
		for (size_t i = 0; i < bundled.size(); ++i) {
			if (SamePalette(bundled[i].path, cfg.palette_file))
				selected = static_cast<int>(i);
		}
		std::vector<std::string> labels;
		labels.reserve(bundled.size() + 1);
		for (const BundledPalette& palette : bundled)
			labels.push_back(palette.label);
		if (selected < 0 && !cfg.palette_file.empty()) {
			labels.push_back(FileName(cfg.palette_file) + "  (browsed)");
			selected = static_cast<int>(labels.size()) - 1;
		}

		const float button_width = ImGui::CalcTextSize("Browse...").x
			+ ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_width
			- ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::BeginCombo("##palette",
				selected >= 0 ? labels[selected].c_str() : "Choose a palette")) {
			for (size_t i = 0; i < labels.size(); ++i) {
				const bool is_selected = static_cast<int>(i) == selected;
				if (ImGui::Selectable(labels[i].c_str(), is_selected)
					&& i < bundled.size()) {
					cfg.palette_file = bundled[i].path;
					state.palette.Set(cfg.palette_file);
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered() && !cfg.palette_file.empty())
			ImGui::SetTooltip("%s", cfg.palette_file.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Browse..."))
			dialogs.RequestOpen(window, "palette", FileDialogs::Kind::Palette,
				cfg.palette_file);
		ImGui::PopID();
	}

	EndForm();
}

// Everything that decides what colours the picture ends up with: the
// corrections applied to the source, then the two settings that turn the
// corrected picture into the 128 Atari colours - which colour each pixel
// becomes, and how the leftover error is spread. Those two were a section of
// their own; they are the same subject as the sliders above them, and a
// separate header made the colour decisions read as two unrelated topics.
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
	if (cfg.saturation > 40) {
		InlineNote("High saturation can push colours past what the palette holds, "
			"collapsing them together. Check the palette usage count.", theme::kTextMuted);
	}
	if (inert) {
		InlineNote("Random Floyd quantizes without error diffusion, so strength "
			"and randomness have no effect on it.", theme::kTextMuted);
	}
	if (cfg.pre_dstf == E_DISTANCE_CIEDE && cfg.dither == E_DITHER_KNOLL) {
		InlineNote("CIEDE2000 together with Knoll dithering is a very slow "
			"combination; preprocessing may take minutes.", theme::kWarning);
	}
}

void DrawObjectiveGroup(SetupState& state, const Rules& rules)
{
	Configuration& cfg = *state.cfg;
	DisabledGroup disabled(!rules.objective_available,
		"Dual-frame mode uses its own joint objective, so these do not apply.");

	if (!BeginForm("objective"))
		return;

	if (Row("objective", cfg)) {
		int objective = static_cast<int>(cfg.visual_objective);
		if (ComboTokens("##objective", &objective, kObjectiveLabels, 2))
			cfg.visual_objective = static_cast<e_visual_objective>(objective);
	}

	if (Row("distance", cfg)) {
		int distance = static_cast<int>(cfg.dstf);
		if (ComboTokens("##distance", &distance, kDistanceLabels, 6))
			cfg.dstf = static_cast<e_distance_function>(distance);
	}

	EndForm();

	// What the choice actually costs and buys. The six entries in that combo
	// are not equally sensible, and nothing else on the screen says so.
	if (!FilterActive()) {
		switch (cfg.visual_objective) {
		case E_OBJECTIVE_LEGACY_TARGET:
			InlineNote("Scores against the Target picture - the quantized, "
				"dithered one the viewer shows under that name. This is the "
				"historical behaviour, so scores stay comparable with older runs, "
				"and it is the only mode in which the paused editor can repaint "
				"the destination.\n"
				"Scoring the Source picture instead usually lands closer to the "
				"original, most clearly with dithering off.", theme::kTextMuted);
			break;
		case E_OBJECTIVE_SOURCE:
		default:
			InlineNote("Scores against the Source picture - the resized, "
				"colour-corrected original. The target still seeds the search and "
				"steers mutation, but the optimizer no longer has to reproduce the "
				"dither pattern, which is why this usually lands closer.\n"
				"Destination painting is unavailable here: the target picture is "
				"no longer what a candidate is measured against.", theme::kTextMuted);
			break;
		}
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

void DrawSearchGroup(SetupState& state, FileDialogs& dialogs, SDL_Window* window)
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
		// Logarithmic, because the useful range spans four orders of magnitude:
		// the interesting settings are 1-100, but long runs use tens of
		// thousands, and a linear slider over 50 000 cannot reach either end
		// precisely. The field beside it types any value exactly.
		if (ValueSliderInt("solutions", &state.solutions, 1, 50000, "%d", 0.0f,
				ImGuiSliderFlags_Logarithmic))
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

// Everything about the search, in the order the decisions are made: whether
// there are one or two frames at all, then how the optimizer explores, then
// what it is scoring against. Objective and dual-frame were sections of their
// own; neither is a decision anyone makes without the optimizer in front of
// them, and as separate headers they read as three unrelated topics.
void DrawAlgorithmSection(SetupState& state, const Rules& rules,
	FileDialogs& dialogs, SDL_Window* window)
{
	Configuration& cfg = *state.cfg;

	// ---- frames -----------------------------------------------------------
	// First, because it decides which of the rest even applies: dual mode
	// brings its own joint objective and rules out the details mask.
	Divider("Frames");
	DrawDualGroup(state);

	// ---- search -----------------------------------------------------------
	Divider("Search");
	DrawSearchGroup(state, dialogs, window);

	// ---- objective --------------------------------------------------------
	// Last, because it is the one that changes what every score means, and the
	// one a first-time user should not have to pass through to reach the rest.
	Divider("Objective");
	DrawObjectiveGroup(state, rules);
}

void DrawDualGroup(SetupState& state)
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
	case Category::Colour:
		if (cfg.brightness != 0) append("brightness " + value("brightness"));
		if (cfg.contrast != 0) append("contrast " + value("contrast"));
		if (cfg.gamma != 1.0) append("gamma " + value("gamma"));
		if (cfg.saturation != 0) append("saturation " + value("saturation"));
		if (cfg.vibrance != 0) append("vibrance " + value("vibrance"));
		if (summary.empty()) summary = "no adjustments";
		append("match " + value("predistance"));
		append("dither " + value("dither"));
		if (cfg.dither != E_DITHER_NONE && cfg.dither != E_DITHER_RFLOYD) {
			append("strength " + value("dither_val"));
			if (cfg.dither_randomness > 0.0)
				append("randomness " + value("dither_rand"));
		}
		break;
	case Category::Algorithm:
		// Read in the order the section is laid out: frames, then search, then
		// what it is all being scored against. Dual is only worth a word when
		// it is on, since off is what nearly every run is.
		if (cfg.dual_mode) {
			append("dual A/B");
			append(cfg.after_dual_steps == "generate" ? "generate B" : "copy A to B");
		}
		append(value("optimizer"));
		append("history " + value("solutions"));
		append("init " + value("init"));
		if (cfg.unstuck_after != 0)
			append("escalates after " + value("unstuck_after"));
		if (!cfg.on_off_file.empty())
			append("register limits");
		if (cfg.dual_mode)
			append("joint dual objective");
		else
			append(value("objective") + " / " + value("distance"));
		break;
	default:
		break;
	}
	return summary;
}

} // namespace setup
} // namespace rc_live_ui

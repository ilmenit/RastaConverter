#include "Dashboard.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "LiveTheme.h"

namespace rc_live_ui {

namespace {

// Enough history for a long run without unbounded growth; older samples are
// dropped from the front.
constexpr size_t kMaxSamples = 600;

// Minimum spacing between samples, so a fast run does not fill the buffer in
// seconds and a slow one still records something.
constexpr double kSampleIntervalMs = 500.0;

double NowMs()
{
	return static_cast<double>(SDL_GetTicksNS()) / 1.0e6;
}

// Thousands separators, matching the converter's own number formatting.
std::string WithCommas(unsigned long long value)
{
	std::string digits = std::to_string(value);
	std::string out;
	int count = 0;
	for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
		if (count > 0 && count % 3 == 0)
			out.push_back(',');
		out.push_back(*it);
		++count;
	}
	std::reverse(out.begin(), out.end());
	return out;
}

// "4 min 02 s", "1 h 12 min" - readable at a glance during a long run.
std::string Duration(double seconds)
{
	if (seconds < 0.0)
		return "-";
	char buffer[64];
	if (seconds < 60.0)
		std::snprintf(buffer, sizeof(buffer), "%.0f s", seconds);
	else if (seconds < 3600.0)
		std::snprintf(buffer, sizeof(buffer), "%d min %02d s",
			static_cast<int>(seconds) / 60, static_cast<int>(seconds) % 60);
	else
		std::snprintf(buffer, sizeof(buffer), "%d h %02d min",
			static_cast<int>(seconds) / 3600, (static_cast<int>(seconds) % 3600) / 60);
	return buffer;
}

// Compact magnitudes for evaluation counts, which reach the billions.
// Basename for the title bar; the full path lives in a tooltip and in the
// configuration recap, where there is room for it.
std::string FileName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Magnitude(unsigned long long value)
{
	// Descending, so each threshold is reachable.
	char buffer[32];
	if (value >= 1000000000000000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.2f P", value / 1e15);
	else if (value >= 1000000000000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.2f T", value / 1e12);
	else if (value >= 1000000000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.2f G", value / 1e9);
	else if (value >= 1000000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.2f M", value / 1e6);
	else if (value >= 1000ULL)
		std::snprintf(buffer, sizeof(buffer), "%.1f k", value / 1e3);
	else
		std::snprintf(buffer, sizeof(buffer), "%llu", value);
	return buffer;
}

void StatLine(const char* label, const std::string& value, ImU32 value_color)
{
	// Keyed by the label: every table in a window needs its own id, or they
	// share column state with each other.
	if (!ImGui::BeginTable(label, 2, ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch, 0.55f);
	ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.45f);
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::TableSetColumnIndex(1);
	// Right-align inside the cell so a long value shortens rather than collides.
	const float offset = ImGui::GetContentRegionAvail().x
		- ImGui::CalcTextSize(value.c_str()).x;
	if (offset > 0.0f)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(value_color));
	ImGui::TextUnformatted(value.c_str());
	ImGui::PopStyleColor();
	ImGui::EndTable();
}

} // namespace

void ProgressTrace::Sample(unsigned long long evaluations, double normalized_distance)
{
	if (!(normalized_distance > 0.0))
		return;
	if (!samples_.empty() && evaluations == last_sample_evaluations_)
		return;
	last_sample_evaluations_ = evaluations;

	samples_.push_back({evaluations, normalized_distance});
	while (samples_.size() > kMaxSamples)
		samples_.pop_front();

	values_.clear();
	values_.reserve(samples_.size());
	best_ = samples_.front().distance;
	worst_ = samples_.front().distance;
	for (const Point& sample : samples_) {
		values_.push_back(static_cast<float>(sample.distance));
		best_ = std::min(best_, sample.distance);
		worst_ = std::max(worst_, sample.distance);
	}
}

void ProgressTrace::Clear()
{
	samples_.clear();
	values_.clear();
	best_ = 0.0;
	worst_ = 0.0;
	last_sample_evaluations_ = 0;
}

Dashboard::Dashboard(SDL_Renderer* renderer) : viewer_(renderer)
{
	// The run shows three pictures, not the four preview stages, and the
	// palette utilization strip belongs to Setup.
	static const char* const kLabels[4] = {"Source", "", "Target", "Output"};
	static const char* const kHints[4] = {
		"The resized source image.",
		"",
		"The quantized, dithered picture the optimizer is aiming at.",
		"What the current best raster program actually renders.",
	};
	viewer_.SetStageLabels(kLabels, kHints, (1u << 0) | (1u << 2) | (1u << 3));
	viewer_.set_stage(PreviewStage::Dithered);
	viewer_.set_show_palette(false);
}

void Dashboard::SetStats(const LiveStats& stats)
{
	// Follow the pipeline on the two transitions that matter, then leave the
	// selection alone so a deliberate choice is never overridden.
	if (stats.preprocessing && !was_preprocessing_)
		viewer_.set_stage(PreviewStage::Quantized);
	if (!search_started_ && stats.evaluations > 0) {
		search_started_ = true;
		viewer_.set_stage(PreviewStage::Dithered);
	}
	was_preprocessing_ = stats.preprocessing;

	stats_ = stats;
	const double now = NowMs();
	if (now - last_sample_ms_ >= kSampleIntervalMs) {
		last_sample_ms_ = now;
		trace_.Sample(stats.evaluations, stats.normalized_distance);
	}
}

void Dashboard::SetImage(PreviewStage stage, int width, int height,
	const std::uint32_t* pixels)
{
	if (pixels == nullptr || width <= 0 || height <= 0)
		return;
	PreviewImage image;
	image.width = width;
	image.height = height;
	image.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height);

	switch (stage) {
	case PreviewStage::Source:    content_.source = std::move(image); break;
	case PreviewStage::Corrected: content_.corrected = std::move(image); break;
	case PreviewStage::Quantized: content_.quantized = std::move(image); break;
	case PreviewStage::Dithered:
	default:                      content_.dithered = std::move(image); break;
	}
	content_dirty_ = true;
}

void Dashboard::SetMask(const PreviewImage& mask)
{
	// Kept in the same content block as the pictures. Setting it straight on
	// the viewer instead would be undone by the next image publish, because
	// SetContent re-applies content_.mask - which would still be empty.
	content_.mask = mask;
	content_dirty_ = true;
}

void Dashboard::DrawProgressPanel()
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted("PROGRESS");
	ImGui::PopStyleColor();
	ImGui::Spacing();

	// The headline number, large. Until the search has scored something there
	// is genuinely nothing to report, and inventing a value would be worse.
	const bool has_score = stats_.evaluations > 0 && stats_.normalized_distance > 0.0;
	char distance[32];
	if (has_score)
		std::snprintf(distance, sizeof(distance), "%.6f", stats_.normalized_distance);
	else
		std::snprintf(distance, sizeof(distance), "%s", "-");
	ImGui::PushStyleColor(ImGuiCol_Text,
		theme::ToVec4(has_score ? theme::kAccent : theme::kTextFaint));
	ImGui::TextUnformatted(distance);
	ImGui::PopStyleColor();
	if (!has_score) {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::TextUnformatted(stats_.preprocessing
			? "building the target picture" : "waiting for the first evaluation");
		ImGui::PopStyleColor();
		return;
	}
	ImGui::SameLine();

	const unsigned long long plateau = stats_.evaluations > stats_.last_best_evaluation
		? stats_.evaluations - stats_.last_best_evaluation : 0;
	// "Improving" means the plateau is short relative to the run.
	const bool improving = plateau < std::max<unsigned long long>(1, stats_.evaluations / 20);
	ImGui::PushStyleColor(ImGuiCol_Text,
		theme::ToVec4(improving ? theme::kSuccess : theme::kTextMuted));
	ImGui::TextUnformatted(improving ? "improving" : "plateaued");
	ImGui::PopStyleColor();

	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::TextUnformatted("normalized distance - lower is closer to the target");
	ImGui::PopStyleColor();
	ImGui::Spacing();

	// Convergence curve. Plain PlotLines is enough here and adds no dependency.
	if (trace_.size() > 1) {
		const float span = ImGui::GetContentRegionAvail().x;
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.07f, 0.09f, 1.0f));
		ImGui::PlotLines("##convergence", trace_.values().data(),
			static_cast<int>(trace_.values().size()), 0, nullptr,
			static_cast<float>(trace_.best()) * 0.98f,
			static_cast<float>(trace_.worst()) * 1.02f,
			ImVec2(span, 88.0f));
		ImGui::PopStyleColor();
	} else {
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
		ImGui::TextUnformatted("Collecting convergence samples...");
		ImGui::PopStyleColor();
	}

	ImGui::Spacing();
	StatLine("Last improvement", plateau == 0 ? std::string("just now")
		: Magnitude(plateau) + " evals ago", theme::kText);
	if (stats_.rate > 0.0) {
		StatLine("Rate", Magnitude(static_cast<unsigned long long>(stats_.rate)) + "/s",
			theme::kText);
	}
	StatLine("Evaluations", Magnitude(stats_.evaluations), theme::kText);
	StatLine("Running for", Duration(stats_.elapsed_seconds), theme::kText);

	// No fabricated ETA: a progress bar only exists when a limit was set.
	if (stats_.max_evals > 0 && stats_.max_evals < (1ULL << 62)) {
		const float fraction = std::min(1.0f,
			static_cast<float>(static_cast<double>(stats_.evaluations)
				/ static_cast<double>(stats_.max_evals)));
		ImGui::Spacing();
		char overlay[64];
		std::snprintf(overlay, sizeof(overlay), "%.1f%% of %s evaluations",
			fraction * 100.0f, Magnitude(stats_.max_evals).c_str());
		ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), overlay);
	} else {
		StatLine("Stops", std::string("when you stop it"), theme::kTextMuted);
	}

	// Escalation, when armed, is otherwise invisible.
	if (stats_.unstuck_after > 0) {
		ImGui::Spacing();
		if (stats_.normalized_drift > 0.0 && plateau >= stats_.unstuck_after) {
			char note[128];
			std::snprintf(note, sizeof(note),
				"Escalating: accepting up to +%.6f worse while stuck.",
				stats_.normalized_drift);
			InlineNote(note, theme::kWarning);
		} else {
			char note[128];
			std::snprintf(note, sizeof(note),
				"Escalates after %s evaluations without improvement.",
				Magnitude(stats_.unstuck_after).c_str());
			InlineNote(note, theme::kTextFaint);
		}
	}
}

void Dashboard::DrawMutationPanel()
{
	if (stats_.mutations.empty())
		return;
	unsigned long long total = 0;
	for (const LiveStats::MutationStat& stat : stats_.mutations)
		total += stat.count;
	if (total == 0)
		return;

	// Sorted bars: which operators are firing is legible at a glance and
	// illegible as a column of numbers.
	std::vector<const LiveStats::MutationStat*> sorted;
	sorted.reserve(stats_.mutations.size());
	for (const LiveStats::MutationStat& stat : stats_.mutations)
		sorted.push_back(&stat);
	std::sort(sorted.begin(), sorted.end(),
		[](const LiveStats::MutationStat* a, const LiveStats::MutationStat* b) {
			return a->count > b->count;
		});

	if (!ImGui::BeginTable("mutations", 2, ImGuiTableFlags_SizingStretchProp))
		return;
	ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.58f);
	ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthStretch, 0.42f);
	for (const LiveStats::MutationStat* stat : sorted) {
		const float share = static_cast<float>(
			static_cast<double>(stat->count) / static_cast<double>(total));
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kText));
		ImGui::TextUnformatted(stat->name.c_str());
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s applications", WithCommas(stat->count).c_str());
		ImGui::TableSetColumnIndex(1);
		char label[48];
		std::snprintf(label, sizeof(label), "%.1f%%", share * 100.0f);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::ToVec4(theme::kAccentDim));
		ImGui::ProgressBar(share,
			ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()), label);
		ImGui::PopStyleColor();
	}
	ImGui::EndTable();
}

void Dashboard::DrawDualPanel()
{
	if (!stats_.dual_mode)
		return;
	StatLine("Phase", stats_.dual_phase, theme::kText);
	StatLine("Optimizing", std::string(stats_.dual_focus_b ? "frame B" : "frame A"),
		theme::kText);
	if (stats_.dual_block_steps > 0) {
		const float fraction = std::min(1.0f,
			static_cast<float>(static_cast<double>(stats_.dual_block_progress)
				/ static_cast<double>(stats_.dual_block_steps)));
		char overlay[64];
		std::snprintf(overlay, sizeof(overlay), "%s / %s",
			Magnitude(stats_.dual_block_progress).c_str(),
			Magnitude(stats_.dual_block_steps).c_str());
		ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), overlay);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Position within the current bootstrap or alternation block.");
	}
}

void Dashboard::DrawDiagnosticsPanel()
{
	StatLine("Accepted moves", WithCommas(stats_.accepted), theme::kText);
	StatLine("Global improvements", WithCommas(stats_.global_improvements), theme::kText);
	StatLine("Migrations", WithCommas(stats_.migrations), theme::kText);
	if (stats_.cache_lookups > 0) {
		char ratio[32];
		std::snprintf(ratio, sizeof(ratio), "%.1f%%",
			100.0 * static_cast<double>(stats_.cache_hits)
				/ static_cast<double>(stats_.cache_lookups));
		StatLine("Line cache hit rate", ratio, theme::kText);
	}
}

void Dashboard::DrawConfigPanel()
{
	// Doubles as the run's configuration recap: during a long run it is
	// genuinely useful to see, and copy, exactly what produced this result.
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextUnformatted(stats_.config_recap.c_str());
	ImGui::Spacing();
	ImGui::TextUnformatted(stats_.output_file.c_str());
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
	ImGui::Spacing();
	if (ImGui::Button("Copy command line")) {
		ImGui::SetClipboardText(stats_.command_line.c_str());
		copied_notice_ = "Copied";
		copied_at_ = NowMs();
	}
	if (!copied_notice_.empty()) {
		if (NowMs() - copied_at_ > 2200.0) {
			copied_notice_.clear();
		} else {
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kSuccess));
			ImGui::TextUnformatted(copied_notice_.c_str());
			ImGui::PopStyleColor();
		}
	}
	InlineNote("These cannot change while a run is in progress.", theme::kTextFaint);
}

LiveCommand Dashboard::DrawBottomBar()
{
	LiveCommand command = LiveCommand::None;

	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
	ImGui::Text("%d worker%s", stats_.threads, stats_.threads == 1 ? "" : "s");
	ImGui::SameLine(0.0f, 16.0f);
	ImGui::Text("cache %d MB/thread", stats_.cache_mb);
	if (stats_.last_save_seconds_ago >= 0.0) {
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::Text("saved %s ago", Duration(stats_.last_save_seconds_ago).c_str());
	}
	ImGui::PopStyleColor();
	if (!stats_.message.empty()) {
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kInfo));
		ImGui::TextUnformatted(stats_.message.c_str());
		ImGui::PopStyleColor();
	}

	// Save / Stop / Abort, right-aligned. Splitting stop from abort removes a
	// genuinely risky ambiguity at the end of a multi-hour run.
	const float button_height = ImGui::GetFrameHeight() * 1.3f;
	const float save_width = ImGui::GetFontSize() * 6.5f;
	const float stop_width = ImGui::GetFontSize() * 9.0f;
	const float abort_width = ImGui::GetFontSize() * 5.5f;
	const float actions_width = save_width + stop_width + abort_width
		+ ImGui::GetStyle().ItemSpacing.x * 2.0f;
	const float right = ImGui::GetWindowContentRegionMax().x - actions_width;
	ImGui::SetCursorPosX(std::max(0.0f, right));
	// Keep the row clear of the child's bottom edge so nothing is clipped.
	ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),
		ImGui::GetWindowHeight() - button_height - ImGui::GetStyle().WindowPadding.y));

	if (ImGui::Button("Save now", ImVec2(save_width, button_height)))
		command = LiveCommand::Save;
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Write the current best result without stopping (S).");

	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, theme::ToVec4(theme::kAccent));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.76f, 0.36f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.09f, 0.11f, 1.0f));
	if (ImGui::Button("Stop and save", ImVec2(stop_width, button_height)))
		command = LiveCommand::StopAndSave;
	ImGui::PopStyleColor(3);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Finish the run and write every output file.");

	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kDanger));
	if (ImGui::Button("Abort", ImVec2(abort_width, button_height)))
		ImGui::OpenPopup("confirm_abort");
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Quit without writing anything new.");

	if (ImGui::BeginPopupModal("confirm_abort", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted("Abort without saving?");
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextMuted));
		ImGui::TextUnformatted("Everything since the last save is lost.");
		ImGui::PopStyleColor();
		ImGui::Spacing();
		if (ImGui::Button("Abort", ImVec2(120.0f, 0.0f))) {
			command = LiveCommand::Abort;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Keep running", ImVec2(140.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	return command;
}

void Dashboard::DrawRail()
{
	struct Entry {
		const char* label;
		int index;
	};
	static const Entry kEntries[] = {
		{"Progress", 0},
		{"Mutations", 1},
		{"Dual frame", 2},
		{"Diagnostics", 3},
		{"Configuration", 4},
	};
	for (const Entry& entry : kEntries) {
		if (entry.index == 2 && !stats_.dual_mode)
			continue;
		const bool selected = active_panel_ == entry.index;
		if (ImGui::Selectable(entry.label, selected))
			active_panel_ = entry.index;
	}
}

LiveCommand Dashboard::Draw()
{
	if (content_dirty_) {
		viewer_.SetContent(content_);
		content_dirty_ = false;
	}

	LiveCommand command = LiveCommand::None;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##dashboard", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar();

	const ImVec2 size = ImGui::GetContentRegionAvail();
	const float title_height = ImGui::GetFrameHeight() + 18.0f;
	const float bottom_height = ImGui::GetFrameHeight() * 3.4f;
	// Leave room for the spacing ImGui inserts between the stacked children,
	// otherwise the action row sits flush against the window edge.
	const float body_height = size.y - title_height - bottom_height
		- ImGui::GetStyle().ItemSpacing.y * 3.0f;
	const float rail_width = size.x < 1100.0f ? 0.0f : 190.0f;
	const float panel_width = std::min(430.0f, std::max(300.0f, size.x * 0.3f));

	// Title: input, output and score, which is also what the taskbar shows.
	ImGui::BeginChild("title", ImVec2(size.x, title_height), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar);
	ImGui::SetCursorPos(ImVec2(16.0f, 10.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted(FileName(stats_.input_file).c_str());
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", stats_.input_file.c_str());
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kAccent));
	ImGui::TextUnformatted("->");
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextStrong));
	ImGui::TextUnformatted(FileName(stats_.output_file).c_str());
	ImGui::PopStyleColor();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", stats_.output_file.c_str());
	{
		const char* status = stats_.finished ? "finished"
			: stats_.preprocessing ? "preparing target" : "running";
		const ImU32 color = stats_.finished ? theme::kSuccess
			: stats_.preprocessing ? theme::kInfo : theme::kAccent;
		const float right = ImGui::GetWindowContentRegionMax().x
			- ImGui::CalcTextSize(status).x - 16.0f;
		if (right > ImGui::GetCursorPosX())
			ImGui::SameLine(right);
		else
			ImGui::SameLine(0.0f, 16.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(color));
		ImGui::TextUnformatted(status);
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();

	if (rail_width > 0.0f) {
		ImGui::SetCursorPosX(8.0f);
		ImGui::BeginChild("rail", ImVec2(rail_width, std::max(80.0f, body_height)));
		DrawRail();
		ImGui::EndChild();
		ImGui::SameLine(0.0f, 8.0f);
	} else {
		ImGui::SetCursorPosX(8.0f);
	}

	ImGui::BeginChild("panels", ImVec2(panel_width, std::max(80.0f, body_height)),
		ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding);
	// The rail selects which panel leads, but progress is always present -
	// it is the answer to the question users ask most.
	DrawProgressPanel();
	Divider();
	switch (active_panel_) {
	case 1: DrawMutationPanel(); break;
	case 2: DrawDualPanel(); break;
	case 3: DrawDiagnosticsPanel(); break;
	case 4: DrawConfigPanel(); break;
	default:
		if (stats_.dual_mode) {
			DrawDualPanel();
			Divider();
		}
		DrawConfigPanel();
		break;
	}
	ImGui::EndChild();

	ImGui::SameLine(0.0f, 8.0f);
	ImGui::BeginChild("viewer",
		ImVec2(std::max(80.0f, size.x - rail_width - panel_width - 32.0f),
			std::max(80.0f, body_height)), ImGuiChildFlags_AlwaysUseWindowPadding);
	viewer_.Draw(false, stats_.preprocessing
		? "Building the target picture..."
		: "Waiting for the first result...");
	ImGui::EndChild();

	ImGui::SetCursorPosX(8.0f);
	ImGui::BeginChild("bottom", ImVec2(size.x - 16.0f, bottom_height),
		ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar);
	const LiveCommand bottom_command = DrawBottomBar();
	if (bottom_command != LiveCommand::None)
		command = bottom_command;
	ImGui::EndChild();

	ImGui::End();
	return command;
}

bool Dashboard::wants_keyboard() const
{
	return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace rc_live_ui

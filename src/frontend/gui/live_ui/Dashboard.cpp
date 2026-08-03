#include "Dashboard.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "LiveTheme.h"
#include "TargetPicture.h"
#include "rgb.h"

namespace rc_live_ui {

namespace {

// Enough history for a long run without unbounded growth; older samples are
// dropped from the front.
constexpr size_t kMaxSamples = 600;
// A short enough tail for "Zoom to recent" to reveal current behaviour. Time,
// rather than a sample count, defines it so delayed UI frames do not stretch it.
constexpr double kRecentWindowMs = 2.0 * 60.0 * 1000.0;
constexpr size_t kMaxUndoStrokes = 128;


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

void ProgressTrace::Sample(unsigned long long evaluations, double rate,
	double sampled_at_ms)
{
	if (!(rate > 0.0))
		return;
	if (!recent_.empty() && evaluations == last_sample_evaluations_)
		return;
	last_sample_evaluations_ = evaluations;

	const Point point{evaluations, rate, sampled_at_ms};

	recent_.push_back(point);
	while (recent_.size() > 1
		&& point.sampled_at_ms - recent_.front().sampled_at_ms > kRecentWindowMs)
		recent_.pop_front();

	// The whole-run buffer takes one sample in `stride_`, and the last one
	// always, so the curve ends where the run is rather than up to a stride
	// behind it.
	if (!whole_.empty() && pushes_ % stride_ != 0)
		whole_.back() = point;
	else
		whole_.push_back(point);
	++pushes_;

	if (whole_.size() > kMaxSamples) {
		// Halve it in place: keep every other sample, and double the stride so
		// the buffer takes as long again to fill next time. The run's whole
		// shape survives; only its resolution halves.
		size_t write = 0;
		for (size_t read = 0; read < whole_.size(); read += 2)
			whole_[write++] = whole_[read];
		whole_.resize(write);
		stride_ *= 2;
	}

	while (!events_.empty() && events_.front() < whole_.front().evaluations)
		events_.erase(events_.begin());
}

bool ProgressTrace::has_distinct_recent_view() const
{
	return recent_.size() >= 2 && !whole_.empty()
		&& recent_.front().sampled_at_ms > whole_.front().sampled_at_ms;
}

void ProgressTrace::MarkEvent(unsigned long long evaluations)
{
	if (events_.empty() || events_.back() != evaluations)
		events_.push_back(evaluations);
}

Dashboard::Dashboard(SDL_Renderer* renderer)
	: viewer_(renderer), editor_(renderer)
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
	if (stats.objective_revision != last_objective_revision_) {
		if (last_objective_revision_ != 0 || stats.objective_revision != 0)
			trace_.MarkEvent(stats.evaluations);
		last_objective_revision_ = stats.objective_revision;
	}

	stats_ = stats;
	editor_.SetStats(stats);
	const double now = NowMs();
	if (now - last_sample_ms_ >= kSampleIntervalMs) {
		last_sample_ms_ = now;
		trace_.Sample(stats.evaluations, stats.rate, now);
	}
}

// The throughput chart.
//
// Y starts at zero rather than at the lowest sample, because that is what makes
// a rate chart readable: the height of the line is the speed, a dip is a stall,
// and the shape does not change meaning when the range does. Autoscaling the
// bottom would turn a steady run into dramatic-looking noise.
void Dashboard::DrawRateChart()
{
	const ProgressTrace::View view = chart_whole_run_
		? ProgressTrace::View::WholeRun : ProgressTrace::View::Recent;
	const std::deque<ProgressTrace::Point>& points = trace_.points(view);
	if (points.size() < 2)
		return;

	double peak = 0.0;
	double total = 0.0;
	for (const ProgressTrace::Point& point : points) {
		peak = std::max(peak, point.rate);
		total += point.rate;
	}
	if (!(peak > 0.0))
		return;
	// Enough headroom that a steady run does not paint the line through the
	// labels along the top edge.
	const double headroom = peak * 1.4;
	const double average = total / static_cast<double>(points.size());

	const float height = 64.0f;
	const float width = std::max(80.0f, ImGui::GetContentRegionAvail().x);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##rate", ImVec2(width, height));
	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 min = origin;
	const ImVec2 max(origin.x + width, origin.y + height);
	ImDrawList* draw = ImGui::GetWindowDrawList();

	draw->AddRectFilled(min, max, IM_COL32(0x0F, 0x11, 0x16, 0xFF), 3.0f);
	draw->AddRect(min, max, IM_COL32(0xFF, 0xFF, 0xFF, 0x14), 3.0f);

	const unsigned long long first_eval = points.front().evaluations;
	const unsigned long long last_eval = points.back().evaluations;
	const double eval_span = last_eval > first_eval
		? static_cast<double>(last_eval - first_eval) : 1.0;
	auto x_of = [&](unsigned long long evaluations) {
		return min.x + static_cast<float>(
			(static_cast<double>(evaluations) - first_eval) / eval_span) * width;
	};
	auto y_of = [&](double rate) {
		return max.y - static_cast<float>(rate / headroom) * height;
	};

	// The average, so a glance says whether the current speed is normal.
	const float average_y = y_of(average);
	draw->AddLine(ImVec2(min.x + 1.0f, average_y), ImVec2(max.x - 1.0f, average_y),
		IM_COL32(0xFF, 0xFF, 0xFF, 0x22));

	// Live objective edits: a retarget flushes every cache, so the dip beside
	// the marker is the cost of the edit, which is worth being able to see.
	for (unsigned long long event : trace_.events()) {
		if (event < first_eval || event > last_eval)
			continue;
		const float x = x_of(event);
		draw->AddLine(ImVec2(x, min.y + 1.0f), ImVec2(x, max.y - 1.0f),
			theme::kWarning, 1.5f);
	}

	std::vector<ImVec2>& polyline = chart_scratch_;
	polyline.clear();
	polyline.reserve(points.size() + 2);
	for (const ProgressTrace::Point& point : points)
		polyline.push_back(ImVec2(x_of(point.evaluations), y_of(point.rate)));
	// Fill the non-convex curve as adjacent convex strips. Passing the entire
	// area to AddConvexPolyFilled made ImGui draw fan diagonals from the first
	// sample through the rate line.
	for (size_t i = 1; i < polyline.size(); ++i) {
		const ImVec2 strip[4] = {
			polyline[i - 1], polyline[i],
			ImVec2(polyline[i].x, max.y - 1.0f),
			ImVec2(polyline[i - 1].x, max.y - 1.0f),
		};
		draw->AddConvexPolyFilled(strip, 4, IM_COL32(0xF0, 0xA8, 0x3C, 0x24));
	}
	draw->AddPolyline(polyline.data(), static_cast<int>(polyline.size()),
		theme::kAccent, 0, 1.6f);

	char label[64];
	const float line_height = ImGui::GetTextLineHeight();
	std::snprintf(label, sizeof(label), "%s/s peak",
		Magnitude(static_cast<unsigned long long>(peak)).c_str());
	draw->AddText(ImVec2(min.x + 6.0f, min.y + 4.0f), theme::kTextFaint, label);
	// On the average line itself, right-aligned, rather than in a corner the
	// filled area covers.
	std::snprintf(label, sizeof(label), "%s/s average",
		Magnitude(static_cast<unsigned long long>(average)).c_str());
	const float average_label_width = ImGui::CalcTextSize(label).x;
	draw->AddText(ImVec2(max.x - average_label_width - 6.0f,
		std::min(max.y - line_height - 2.0f, average_y + 2.0f)),
		theme::kTextFaint, label);

	const std::string range = Magnitude(first_eval) + "  ->  "
		+ Magnitude(last_eval) + " evals";
	const float range_width = ImGui::CalcTextSize(range.c_str()).x;
	draw->AddText(ImVec2(max.x - range_width - 6.0f, min.y + 4.0f),
		theme::kTextFaint, range.c_str());

	if (hovered) {
		const float mouse_x = ImGui::GetIO().MousePos.x;
		size_t nearest = 0;
		float best_distance = width;
		for (size_t i = 0; i < polyline.size(); ++i) {
			const float distance = std::abs(polyline[i].x - mouse_x);
			if (distance < best_distance) {
				best_distance = distance;
				nearest = i;
			}
		}
		const ImVec2 point = polyline[nearest];
		draw->AddLine(ImVec2(point.x, min.y + 1.0f), ImVec2(point.x, max.y - 1.0f),
			IM_COL32(0xFF, 0xFF, 0xFF, 0x40));
		draw->AddCircleFilled(point, 3.0f, theme::kAccent);
		ImGui::SetTooltip("%s evaluations per second\nafter %s evaluations",
			Magnitude(static_cast<unsigned long long>(points[nearest].rate)).c_str(),
			WithCommas(points[nearest].evaluations).c_str());
	}

	ImGui::PushStyleColor(ImGuiCol_Text, theme::ToVec4(theme::kTextFaint));
	ImGui::TextUnformatted(chart_whole_run_ ? "whole run" : "last 2 minutes");
	ImGui::PopStyleColor();
	const char* toggle = chart_whole_run_ ? "Zoom to recent" : "Show whole run";
	const float toggle_width = ImGui::CalcTextSize(toggle).x
		+ ImGui::GetStyle().FramePadding.x * 2.0f;
	const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x
		- toggle_width;
	if (right > ImGui::GetCursorPosX())
		ImGui::SameLine(right);
	else
		ImGui::SameLine();
	const bool recent_available = trace_.has_distinct_recent_view();
	ImGui::BeginDisabled(chart_whole_run_ && !recent_available);
	if (ImGui::SmallButton(toggle))
		chart_whole_run_ = !chart_whole_run_;
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		if (chart_whole_run_ && !recent_available)
			ImGui::SetTooltip("The whole run already fits inside the last two minutes.");
		else
			ImGui::SetTooltip(chart_whole_run_
				? "The last two minutes at full resolution."
				: "Every sample since the run started, thinned to fit.");
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

void Dashboard::SetMask(const PreviewImage& mask,
	const std::vector<unsigned char>& editable_values, bool active)
{
	// Kept in the same content block as the pictures. Setting it straight on
	// the viewer instead would be undone by the next image publish, because
	// SetContent re-applies content_.mask - which would still be empty.
	content_.mask = mask;
	editor_.SetMaskLayer(editable_values, mask.width, mask.height);
	mask_active_ = active;
	content_dirty_ = true;
}

void Dashboard::SetDestinationLayer(
	const std::vector<unsigned char>& palette_indices, int width, int height)
{
	editor_.SetDestinationLayer(palette_indices, width, height);
}

bool Dashboard::TakeEditorApply(GuiEditorApply& request)
{
	return editor_.TakeApply(request);
}

bool Dashboard::EditorWantsDestination() const
{
	return editor_.wants_destination();
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

	ImGui::Spacing();
	StatLine("Last improvement", plateau == 0 ? std::string("just now")
		: Magnitude(plateau) + " evals ago", theme::kText);
	if (stats_.rate > 0.0) {
		StatLine("Rate", Magnitude(static_cast<unsigned long long>(stats_.rate)) + "/s",
			theme::kText);
		DrawRateChart();
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
		const std::string position = WithCommas(stats_.dual_block_progress)
			+ " / " + WithCommas(stats_.dual_block_steps);
		StatLine("Block position", position, theme::kText);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Position within the current bootstrap or alternation block.");
	}
	const char* preview = stats_.dual_display == 'A' ? "frame A"
		: stats_.dual_display == 'B' ? "frame B" : "blended A+B";
	StatLine("Preview", preview, theme::kText);
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
	const float edit_width = ImGui::GetFontSize() * 8.5f;
	const float actions_width = edit_width + save_width + stop_width + abort_width
		+ ImGui::GetStyle().ItemSpacing.x * 3.0f;
	const float right = ImGui::GetWindowContentRegionMax().x - actions_width;
	ImGui::SetCursorPosX(std::max(0.0f, right));
	// Keep the row clear of the child's bottom edge so nothing is clipped.
	ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),
		ImGui::GetWindowHeight() - button_height - ImGui::GetStyle().WindowPadding.y));

	// One entry point for changing anything about the run: pause, edit, resume.
	ImGui::BeginDisabled(!stats_.editor_available);
	if (ImGui::Button("Pause & Edit", ImVec2(edit_width, button_height))) {
		editor_.Open(PaintEditor::Target::Mask);
		editor_pending_open_ = true;
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip(stats_.editor_available
			? "Stop the search and open the editor: paint the details mask or\n"
			  "repaint the destination, then apply and resume."
			: "Available once the search is running.");
	}
	ImGui::SameLine();

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
		viewer_.set_mask_active(mask_active_);
		editor_.SetContent(content_);
		content_dirty_ = false;
	}

	// Development hooks: drive one editor session headlessly so CI can prove the
	// pause -> paint -> apply -> resume path end to end. Inert in normal runs.
	if (stats_.evaluations > 0 && stats_.editor_available) {
		const bool test_mask = SDL_getenv("RASTA_TEST_MASK_EDIT") != nullptr;
		const bool test_destination = SDL_getenv("RASTA_TEST_DESTINATION_EDIT") != nullptr
			&& stats_.destination_edit_available;
		const bool test_branch = SDL_getenv("RASTA_TEST_BRANCH") != nullptr;
		if (test_mask || test_destination || test_branch) {
			static int phase = 0;
			if (phase == 0) {
				editor_.Open(test_destination ? PaintEditor::Target::Destination
					: PaintEditor::Target::Mask);
				editor_pending_open_ = true;
				++phase;
			} else if (phase == 1 && stats_.editor_paused
				&& SDL_getenv("RASTA_TEST_EDITOR_HOLD") == nullptr) {
				editor_.TestStroke(test_destination ? 127 : 255);
				editor_.TestRequestApply(test_branch);
				editor_pending_apply_ = true;
				++phase;
			} else if (phase == 2 && !stats_.editor_paused) {
				test_stop_requested_ = true;
				++phase;
			}
		}
	}

	// A test hook can request the apply while the editor is still open; drain
	// that first, or the request would sit behind the editor branch forever.
	if (editor_pending_apply_) {
		editor_pending_apply_ = false;
		editor_.Close();
		return LiveCommand::EditorApply;
	}

	// While a session is open the editor owns the window: every dashboard panel
	// is frozen anyway, and the canvas wants the room.
	if (editor_.active() || editor_pending_open_) {
		const PaintEditor::Action action = editor_.Draw();
		if (editor_pending_open_) {
			editor_pending_open_ = false;
			return LiveCommand::EditorBegin;
		}
		if (action.apply) {
			editor_.Close();
			return LiveCommand::EditorApply;
		}
		if (action.discard) {
			editor_.Close();
			return LiveCommand::EditorDiscard;
		}
		return LiveCommand::None;
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
	if (editor_pending_open_)
		command = LiveCommand::EditorBegin;
	else if (test_stop_requested_) {
		command = LiveCommand::StopAndSave;
		test_stop_requested_ = false;
	}
	ImGui::EndChild();

	ImGui::End();
	return command;
}

bool Dashboard::wants_keyboard() const
{
	return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace rc_live_ui
